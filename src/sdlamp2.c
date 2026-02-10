#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/version.h>
#include <libswresample/swresample.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "SDL.h"
#include "SDL_image.h"

#define MAX_FILES 64
#define SEEK_SECONDS 10.0

/* --- Decoder context --- */

typedef struct {
  AVFormatContext* format_ctx;
  AVCodecContext* codec_ctx;
  AVPacket* packet;
  AVFrame* frame;
  SwrContext* swr_ctx;
  uint8_t* swr_buf;
  int swr_buf_size;
  int audio_stream_index;
  int64_t duration;
  int64_t current_pts;
  AVRational time_base;
  SDL_bool eof;
  SDL_Texture* album_art;
  int art_width;
  int art_height;
} Decoder;

/* --- Globals --- */

static SDL_Window* window = NULL;
static SDL_Renderer* renderer = NULL;
static SDL_AudioDeviceID audio_device = 0;
static SDL_AudioStream* stream = NULL;
static SDL_bool paused = SDL_TRUE;

static Decoder decoder = {0};

static char audio_dir[512] = ".";
static char current_file[512] = "";
static char audio_files[MAX_FILES][256];
static int num_audio_files = 0;
static int current_file_index = 0;

/* --- Utility --- */

static void panic_and_abort(const char* title, const char* text) {
  fprintf(stderr, "PANIC: %s ... %s\n", title, text);
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, text, window);
  SDL_Quit();
  exit(1);
}

static double get_current_seconds(void) {
  return decoder.current_pts * av_q2d(decoder.time_base);
}

static double get_duration_seconds(void) {
  if (decoder.duration <= 0) return 0.0;
  return decoder.duration * av_q2d(decoder.time_base);
}

/* --- Position persistence --- */

static void save_position(const char* filename, double seconds) {
  char path[1024];
  snprintf(path, sizeof(path), "%s/positions.txt", audio_dir);

  char lines[MAX_FILES][512];
  int num_lines = 0;
  SDL_bool found = SDL_FALSE;

  FILE* f = fopen(path, "r");
  if (f) {
    char line[512];
    while (fgets(line, sizeof(line), f) && num_lines < MAX_FILES) {
      char* nl = strchr(line, '\n');
      if (nl) *nl = '\0';
      char* tab = strchr(line, '\t');
      if (tab) {
        size_t name_len = (size_t)(tab - line);
        if (name_len == strlen(filename) && strncmp(line, filename, name_len) == 0) {
          snprintf(lines[num_lines], sizeof(lines[0]), "%s\t%.2f", filename, seconds);
          found = SDL_TRUE;
        } else {
          strncpy(lines[num_lines], line, sizeof(lines[0]) - 1);
          lines[num_lines][sizeof(lines[0]) - 1] = '\0';
        }
        num_lines++;
      }
    }
    fclose(f);
  }

  if (!found && num_lines < MAX_FILES) {
    snprintf(lines[num_lines], sizeof(lines[0]), "%s\t%.2f", filename, seconds);
    num_lines++;
  }

  f = fopen(path, "w");
  if (f) {
    for (int i = 0; i < num_lines; i++) {
      fprintf(f, "%s\n", lines[i]);
    }
    fclose(f);
  }
}

static double load_position(const char* filename) {
  char path[1024];
  snprintf(path, sizeof(path), "%s/positions.txt", audio_dir);

  FILE* f = fopen(path, "r");
  if (!f) return 0.0;

  char line[512];
  while (fgets(line, sizeof(line), f)) {
    char* tab = strchr(line, '\t');
    if (tab) {
      size_t name_len = (size_t)(tab - line);
      if (name_len == strlen(filename) && strncmp(line, filename, name_len) == 0) {
        fclose(f);
        return atof(tab + 1);
      }
    }
  }
  fclose(f);
  return 0.0;
}

/* --- File scanning --- */

static int has_audio_extension(const char* name) {
  const char* ext = strrchr(name, '.');
  if (!ext) return 0;
  return (strcasecmp(ext, ".m4a") == 0 || strcasecmp(ext, ".mp3") == 0 ||
          strcasecmp(ext, ".wav") == 0 || strcasecmp(ext, ".ogg") == 0);
}

static int compare_strings(const void* a, const void* b) {
  return strcmp((const char*)a, (const char*)b);
}

static void scan_audio_files(const char* dir) {
  num_audio_files = 0;
  DIR* d = opendir(dir);
  if (!d) return;

  struct dirent* entry;
  while ((entry = readdir(d)) != NULL && num_audio_files < MAX_FILES) {
    if (entry->d_name[0] == '.') continue;
    if (has_audio_extension(entry->d_name)) {
      strncpy(audio_files[num_audio_files], entry->d_name, 255);
      audio_files[num_audio_files][255] = '\0';
      num_audio_files++;
    }
  }
  closedir(d);

  qsort(audio_files, num_audio_files, sizeof(audio_files[0]), compare_strings);
}

/* --- Decoder --- */

static void decoder_close(void) {
  if (decoder.album_art) {
    SDL_DestroyTexture(decoder.album_art);
  }
  if (decoder.swr_buf) {
    av_free(decoder.swr_buf);
  }
  if (decoder.swr_ctx) {
    swr_free(&decoder.swr_ctx);
  }
  if (decoder.frame) {
    av_frame_free(&decoder.frame);
  }
  if (decoder.packet) {
    av_packet_free(&decoder.packet);
  }
  if (decoder.codec_ctx) {
    avcodec_free_context(&decoder.codec_ctx);
  }
  if (decoder.format_ctx) {
    avformat_close_input(&decoder.format_ctx);
  }
  memset(&decoder, 0, sizeof(decoder));
}

static SDL_bool decoder_open(const char* path) {
  decoder_close();

  if (avformat_open_input(&decoder.format_ctx, path, NULL, NULL) != 0) {
    fprintf(stderr, "Could not open file: %s\n", path);
    return SDL_FALSE;
  }

  if (avformat_find_stream_info(decoder.format_ctx, NULL) < 0) {
    fprintf(stderr, "Could not find stream info: %s\n", path);
    decoder_close();
    return SDL_FALSE;
  }

  /* Find first audio stream (not hardcoding 0 since album art may be stream 0) */
  decoder.audio_stream_index = -1;
  for (unsigned int i = 0; i < decoder.format_ctx->nb_streams; i++) {
    if (decoder.format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      decoder.audio_stream_index = (int)i;
      break;
    }
  }
  if (decoder.audio_stream_index < 0) {
    fprintf(stderr, "No audio stream found: %s\n", path);
    decoder_close();
    return SDL_FALSE;
  }

  AVStream* audio_stream = decoder.format_ctx->streams[decoder.audio_stream_index];
  AVCodecParameters* codecpar = audio_stream->codecpar;

  const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
  if (!codec) {
    fprintf(stderr, "Unsupported codec in: %s\n", path);
    decoder_close();
    return SDL_FALSE;
  }

  decoder.codec_ctx = avcodec_alloc_context3(codec);
  if (avcodec_parameters_to_context(decoder.codec_ctx, codecpar) < 0) {
    fprintf(stderr, "Could not copy codec params: %s\n", path);
    decoder_close();
    return SDL_FALSE;
  }
  if (avcodec_open2(decoder.codec_ctx, codec, NULL) < 0) {
    fprintf(stderr, "Could not open codec for: %s\n", path);
    decoder_close();
    return SDL_FALSE;
  }

  decoder.packet = av_packet_alloc();
  decoder.frame = av_frame_alloc();

  /* Set up SwrContext: convert from decoder's native format to interleaved float stereo 48kHz */
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100)
  AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
  int swr_err = swr_alloc_set_opts2(&decoder.swr_ctx, &out_ch_layout, AV_SAMPLE_FMT_FLT, 48000,
                                    &decoder.codec_ctx->ch_layout, decoder.codec_ctx->sample_fmt,
                                    decoder.codec_ctx->sample_rate, 0, NULL);
  if (swr_err < 0 || !decoder.swr_ctx || swr_init(decoder.swr_ctx) < 0) {
#else
  int64_t in_ch_layout = decoder.codec_ctx->channel_layout;
  if (!in_ch_layout) {
    in_ch_layout = av_get_default_channel_layout(decoder.codec_ctx->channels);
  }
  decoder.swr_ctx =
      swr_alloc_set_opts(NULL, AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_FLT, 48000, in_ch_layout,
                         decoder.codec_ctx->sample_fmt, decoder.codec_ctx->sample_rate, 0, NULL);
  if (!decoder.swr_ctx || swr_init(decoder.swr_ctx) < 0) {
#endif
    fprintf(stderr, "Could not initialize resampler for: %s\n", path);
    decoder_close();
    return SDL_FALSE;
  }

  /* Store timing info */
  decoder.time_base = audio_stream->time_base;
  if (audio_stream->duration != AV_NOPTS_VALUE) {
    decoder.duration = audio_stream->duration;
  } else if (decoder.format_ctx->duration != AV_NOPTS_VALUE) {
    decoder.duration =
        av_rescale_q(decoder.format_ctx->duration, AV_TIME_BASE_Q, decoder.time_base);
  } else {
    decoder.duration = 0;
  }
  decoder.current_pts = 0;
  decoder.eof = SDL_FALSE;

  /* Create SDL audio stream (FIFO — swr does all format conversion) */
  SDL_FreeAudioStream(stream);
  stream = NULL;
  stream = SDL_NewAudioStream(AUDIO_F32, 2, 48000, AUDIO_F32, 2, 48000);
  if (!stream) {
    fprintf(stderr, "Could not create audio stream: %s\n", SDL_GetError());
    decoder_close();
    return SDL_FALSE;
  }

  /* Extract album art if present */
  for (unsigned int i = 0; i < decoder.format_ctx->nb_streams; i++) {
    if (decoder.format_ctx->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC) {
      AVPacket* pic = &decoder.format_ctx->streams[i]->attached_pic;
      SDL_RWops* rw = SDL_RWFromConstMem(pic->data, pic->size);
      if (rw) {
        SDL_Surface* surface = IMG_Load_RW(rw, 1);
        if (surface) {
          decoder.album_art = SDL_CreateTextureFromSurface(renderer, surface);
          decoder.art_width = surface->w;
          decoder.art_height = surface->h;
          SDL_FreeSurface(surface);
        }
      }
      break;
    }
  }

#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100)
  printf("Opened: %s (%s, %d ch, %d Hz, fmt=%s)\n", path, codec->name,
         codecpar->ch_layout.nb_channels, codecpar->sample_rate,
         av_get_sample_fmt_name(decoder.codec_ctx->sample_fmt));
#else
  printf("Opened: %s (%s, %d ch, %d Hz, fmt=%s)\n", path, codec->name, codecpar->channels,
         codecpar->sample_rate, av_get_sample_fmt_name(decoder.codec_ctx->sample_fmt));
#endif

  return SDL_TRUE;
}

static SDL_bool decoder_pump(int max_bytes) {
  if (decoder.eof || !decoder.format_ctx) return SDL_FALSE;

  int bytes_fed = 0;

  while (bytes_fed < max_bytes) {
    int ret = av_read_frame(decoder.format_ctx, decoder.packet);
    if (ret < 0) {
      decoder.eof = SDL_TRUE;
      return SDL_FALSE;
    }

    if (decoder.packet->stream_index != decoder.audio_stream_index) {
      av_packet_unref(decoder.packet);
      continue;
    }

    ret = avcodec_send_packet(decoder.codec_ctx, decoder.packet);
    av_packet_unref(decoder.packet);
    if (ret < 0) continue;

    while (avcodec_receive_frame(decoder.codec_ctx, decoder.frame) == 0) {
      if (decoder.frame->pts != AV_NOPTS_VALUE) {
        decoder.current_pts = decoder.frame->pts;
      }

      int out_samples = swr_get_out_samples(decoder.swr_ctx, decoder.frame->nb_samples);
      int out_size = out_samples * 2 * (int)sizeof(float);

      if (out_size > decoder.swr_buf_size) {
        av_free(decoder.swr_buf);
        decoder.swr_buf = (uint8_t*)av_malloc(out_size);
        if (!decoder.swr_buf) {
          decoder.swr_buf_size = 0;
          continue;
        }
        decoder.swr_buf_size = out_size;
      }

      uint8_t* out_planes[1] = {decoder.swr_buf};
      int converted =
          swr_convert(decoder.swr_ctx, out_planes, out_samples,
                      (const uint8_t**)decoder.frame->extended_data, decoder.frame->nb_samples);

      if (converted > 0) {
        int converted_bytes = converted * 2 * (int)sizeof(float);
        if (SDL_AudioStreamPut(stream, decoder.swr_buf, converted_bytes) < 0) {
          fprintf(stderr, "SDL_AudioStreamPut failed: %s\n", SDL_GetError());
        }
        bytes_fed += converted_bytes;
      }
    }
  }

  return SDL_TRUE;
}

static void decoder_seek(double target_seconds) {
  if (!decoder.format_ctx) return;

  double duration_secs = get_duration_seconds();
  if (target_seconds < 0.0) target_seconds = 0.0;
  if (duration_secs > 0.0 && target_seconds > duration_secs) target_seconds = duration_secs;

  int64_t target_ts = (int64_t)(target_seconds / av_q2d(decoder.time_base));

  av_seek_frame(decoder.format_ctx, decoder.audio_stream_index, target_ts, AVSEEK_FLAG_BACKWARD);
  avcodec_flush_buffers(decoder.codec_ctx);

  SDL_AudioStreamClear(stream);
  SDL_ClearQueuedAudio(audio_device);

  decoder.current_pts = target_ts;
  decoder.eof = SDL_FALSE;
}

/* --- File switching --- */

static void switch_file(int index) {
  /* Save position of current file before switching */
  if (current_file[0] && decoder.format_ctx) {
    save_position(current_file, get_current_seconds());
  }

  /* Try to open the requested file, skipping corrupt ones */
  for (int attempts = 0; attempts < num_audio_files; attempts++) {
    int try_index = (index + attempts) % num_audio_files;

    current_file_index = try_index;
    strncpy(current_file, audio_files[try_index], sizeof(current_file) - 1);
    current_file[sizeof(current_file) - 1] = '\0';

    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", audio_dir, audio_files[try_index]);

    if (decoder_open(path)) {
      double pos = load_position(current_file);
      if (pos > 1.0) {
        decoder_seek(pos);
      }

      char title[256];
      snprintf(title, sizeof(title), "SDLamp2 - %s", current_file);
      SDL_SetWindowTitle(window, title);

      paused = SDL_FALSE;
      SDL_PauseAudioDevice(audio_device, 0);
      return;
    }

    fprintf(stderr, "Failed to open: %s, trying next\n", path);
  }

  /* All files failed */
  current_file[0] = '\0';
  SDL_SetWindowTitle(window, "SDLamp2 - No playable files");
}

/* --- Main --- */

int main(int argc, char** argv) {
  if (argc > 1) {
    strncpy(audio_dir, argv[1], sizeof(audio_dir) - 1);
    audio_dir[sizeof(audio_dir) - 1] = '\0';
  }

  /* Button positions (bottom of window, centered) */
  const SDL_Rect rewind_btn = {80, 390, 80, 80};
  const SDL_Rect stop_btn = {180, 390, 80, 80};
  const SDL_Rect play_btn = {280, 390, 80, 80};
  const SDL_Rect ff_btn = {380, 390, 80, 80};
  const SDL_Rect next_btn = {480, 390, 80, 80};

  /* Sprite sheet source rects */
  const SDL_Rect rewind_sprite = {0, 0, 200, 200};
  const SDL_Rect play_sprite = {220, 0, 200, 200};
  const SDL_Rect ff_sprite = {440, 0, 200, 200};
  const SDL_Rect stop_sprite = {0, 220, 200, 200};
  const SDL_Rect next_sprite = {440, 220, 200, 200};

  /* Progress bar area */
  const SDL_Rect progress_bg = {20, 360, 600, 15};

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
    panic_and_abort("SDL_Init failed!", SDL_GetError());
  }

  window = SDL_CreateWindow("SDLamp2", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480,
                            0);
  if (!window) {
    panic_and_abort("SDL_CreateWindow failed!", SDL_GetError());
  }

  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
  if (!renderer) {
    panic_and_abort("SDL_CreateRenderer failed!", SDL_GetError());
  }

  SDL_AudioSpec desired;
  SDL_zero(desired);
  desired.freq = 48000;
  desired.format = AUDIO_F32;
  desired.channels = 2;
  desired.samples = 4096;
  desired.callback = NULL;

  audio_device = SDL_OpenAudioDevice(NULL, 0, &desired, NULL, 0);
  if (audio_device == 0) {
    panic_and_abort("Could not open audio device!", SDL_GetError());
  }

  SDL_Surface* controls_surface = IMG_Load("controls.png");
  if (!controls_surface) {
    panic_and_abort("Could not load controls asset!", SDL_GetError());
  }

  SDL_Texture* controls_texture = SDL_CreateTextureFromSurface(renderer, controls_surface);
  SDL_FreeSurface(controls_surface);
  if (!controls_texture) {
    panic_and_abort("Could not create controls texture!", SDL_GetError());
  }

  /* Scan directory and open first file */
  scan_audio_files(audio_dir);
  if (num_audio_files > 0) {
    switch_file(0);
  } else {
    SDL_SetWindowTitle(window, "SDLamp2 - No audio files found");
  }

  SDL_bool running = SDL_TRUE;
  SDL_Event e;

  while (running) {
    /* --- Event handling --- */
    while (SDL_PollEvent(&e)) {
      switch (e.type) {
        case SDL_QUIT:
          running = SDL_FALSE;
          break;

        case SDL_MOUSEBUTTONDOWN: {
          const SDL_Point pt = {e.button.x, e.button.y};

          if (SDL_PointInRect(&pt, &rewind_btn)) {
            double pos = get_current_seconds() - SEEK_SECONDS;
            decoder_seek(pos < 0.0 ? 0.0 : pos);
          } else if (SDL_PointInRect(&pt, &stop_btn)) {
            if (!paused) {
              paused = SDL_TRUE;
              SDL_PauseAudioDevice(audio_device, 1);
              if (current_file[0]) {
                save_position(current_file, get_current_seconds());
              }
            }
          } else if (SDL_PointInRect(&pt, &play_btn)) {
            if (paused && num_audio_files > 0) {
              paused = SDL_FALSE;
              SDL_PauseAudioDevice(audio_device, 0);
            }
          } else if (SDL_PointInRect(&pt, &ff_btn)) {
            double pos = get_current_seconds() + SEEK_SECONDS;
            double dur = get_duration_seconds();
            decoder_seek((dur > 0.0 && pos > dur) ? dur : pos);
          } else if (SDL_PointInRect(&pt, &next_btn)) {
            if (num_audio_files > 0) {
              int next = (current_file_index + 1) % num_audio_files;
              switch_file(next);
            }
          }
          break;
        }
      }
    }

    /* --- Feed decoder into SDL audio stream when buffer is low --- */
    if (!paused && !decoder.eof) {
      int threshold = 48000 * 2 * (int)sizeof(float);  /* ~1 second of audio */
      if (SDL_AudioStreamAvailable(stream) < threshold) {
        decoder_pump(threshold);
      }
    }

    /* --- Drain audio stream into device queue --- */
    if (SDL_GetQueuedAudioSize(audio_device) < 8192) {
      int available = SDL_AudioStreamAvailable(stream);
      if (available > 0) {
        int to_get = available < 32768 ? available : 32768;
        static Uint8 transfer_buf[32768];
        int got = SDL_AudioStreamGet(stream, transfer_buf, to_get);
        if (got > 0) {
          SDL_QueueAudio(audio_device, transfer_buf, got);
        }
      }
    }

    /* --- Handle EOF: let queue drain, then auto-stop and reset --- */
    if (decoder.eof && !paused) {
      if (SDL_AudioStreamAvailable(stream) == 0 && SDL_GetQueuedAudioSize(audio_device) == 0) {
        paused = SDL_TRUE;
        SDL_PauseAudioDevice(audio_device, 1);
        decoder_seek(0.0);
      }
    }

    /* --- Rendering --- */
    SDL_SetRenderDrawColor(renderer, 0xFF, 0xFF, 0xFF, 0xFF);
    SDL_RenderClear(renderer);

    /* Album art (centered, aspect-preserving, in upper area) */
    if (decoder.album_art && decoder.art_width > 0 && decoder.art_height > 0) {
      int max_w = 600, max_h = 340;
      float scale_w = (float)max_w / decoder.art_width;
      float scale_h = (float)max_h / decoder.art_height;
      float scale = scale_w < scale_h ? scale_w : scale_h;
      int draw_w = (int)(decoder.art_width * scale);
      int draw_h = (int)(decoder.art_height * scale);
      SDL_Rect art_rect = {(640 - draw_w) / 2, (350 - draw_h) / 2, draw_w, draw_h};
      SDL_RenderCopy(renderer, decoder.album_art, NULL, &art_rect);
    }

    /* Progress bar */
    SDL_SetRenderDrawColor(renderer, 0xC0, 0xC0, 0xC0, 0xFF);
    SDL_RenderFillRect(renderer, &progress_bg);

    double dur = get_duration_seconds();
    if (dur > 0.0) {
      double cur = get_current_seconds();
      double frac = cur / dur;
      if (frac > 1.0) frac = 1.0;
      if (frac < 0.0) frac = 0.0;
      SDL_Rect fill = {progress_bg.x, progress_bg.y, (int)(progress_bg.w * frac), progress_bg.h};
      SDL_SetRenderDrawColor(renderer, 0x50, 0x50, 0x50, 0xFF);
      SDL_RenderFillRect(renderer, &fill);
    }

    /* Buttons */
    SDL_RenderCopy(renderer, controls_texture, &rewind_sprite, &rewind_btn);
    SDL_RenderCopy(renderer, controls_texture, &stop_sprite, &stop_btn);
    SDL_RenderCopy(renderer, controls_texture, &play_sprite, &play_btn);
    SDL_RenderCopy(renderer, controls_texture, &ff_sprite, &ff_btn);
    SDL_RenderCopy(renderer, controls_texture, &next_sprite, &next_btn);

    SDL_RenderPresent(renderer);
  }

  /* --- Cleanup --- */
  if (current_file[0] && decoder.format_ctx) {
    save_position(current_file, get_current_seconds());
  }
  decoder_close();
  SDL_FreeAudioStream(stream);
  SDL_DestroyTexture(controls_texture);
  SDL_CloseAudioDevice(audio_device);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}
