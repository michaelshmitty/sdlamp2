#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>

#include "SDL.h"
#include "SDL_image.h"

static SDL_Window* window = NULL;
static SDL_Renderer* renderer = NULL;

static SDL_AudioDeviceID audio_device = 0;
static Uint8* wavbuf = NULL;
static Uint32 wavlen = 0;
static SDL_AudioSpec wavspec;
static SDL_AudioStream* stream = NULL;
// static SDL_bool paused = SDL_TRUE;
static SDL_bool paused = SDL_FALSE;

static void panic_and_abort(const char* title, const char* text) {
  fprintf(stderr, "PANIC: %s ... %s\n", title, text);
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, text, window);
  SDL_Quit();
  exit(1);
}

static SDL_bool load_audio_file(const char* fname) {
  SDL_FreeAudioStream(stream);
  stream = NULL;
  SDL_FreeWAV(wavbuf);
  wavbuf = NULL;
  wavlen = 0;

  if (SDL_LoadWAV(fname, &wavspec, &wavbuf, &wavlen) == NULL) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Couldn't load wav file!", SDL_GetError(),
                             window);
    return SDL_FALSE;
  }

  stream = SDL_NewAudioStream(wavspec.format, wavspec.channels, wavspec.freq, AUDIO_F32, 2, 48000);
  if (!stream) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Couldn't create audio stream!", SDL_GetError(),
                             window);
    SDL_FreeWAV(wavbuf);
    wavbuf = NULL;
    wavlen = 0;
  }

  if (SDL_AudioStreamPut(stream, wavbuf, wavlen) == -1) {  // FIXME(m): graceful handling.
    panic_and_abort("Audio stream put failed!", SDL_GetError());
  }
  SDL_AudioStreamFlush(stream);  // FIXME(m): error handling.
  SDL_PauseAudioDevice(audio_device, paused);

  return SDL_TRUE;
}

static SDL_bool decode_audio() {
  printf("libavutil version: %s\n", av_version_info());

  // Trying to open and decode AAC / ALAC (.m4a file) with ffmpeg libraries
  AVFormatContext* pFormatContext = avformat_alloc_context();
  if (avformat_open_input(&pFormatContext, "./build/music.m4a", NULL, NULL) != 0) {
    printf("ERROR: could not open the file!\n");
    return SDL_FALSE;
  }
  printf("Format: %s\n", pFormatContext->iformat->long_name);

  avformat_find_stream_info(pFormatContext, NULL);
  if (pFormatContext->nb_streams < 1) {
    printf("ERROR: no valid streams found in file!\n");
    return SDL_FALSE;
  }

  AVCodecParameters* pLocalCodecParameters = pFormatContext->streams[0]->codecpar;
  if (pLocalCodecParameters->codec_type != AVMEDIA_TYPE_AUDIO) {
    printf("ERROR: no audio stream found in file!\n");
    return SDL_FALSE;
  }

  printf("Audio stream: %d channels, sample rate %d\n",
         pLocalCodecParameters->channels, pLocalCodecParameters->sample_rate);

  const AVCodec* pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);
  printf("Codec: %s ID %d bit_rate %lld\n", pLocalCodec->long_name, pLocalCodec->id,
         pLocalCodecParameters->bit_rate);

  AVCodecContext* pCodecContext = avcodec_alloc_context3(pLocalCodec);
  avcodec_parameters_to_context(pCodecContext, pLocalCodecParameters);
  avcodec_open2(pCodecContext, pLocalCodec, NULL);

  AVPacket* pPacket = av_packet_alloc();
  AVFrame* pFrame = av_frame_alloc();

  SDL_FreeAudioStream(stream);
  stream = NULL;
  wavlen = 0;

  stream = SDL_NewAudioStream(AUDIO_F32, pLocalCodecParameters->channels,
                              pLocalCodecParameters->sample_rate, AUDIO_F32, 2, 48000);
  if (!stream) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Couldn't create audio stream!", SDL_GetError(),
                             window);
  }

  while (av_read_frame(pFormatContext, pPacket) >= 0) {
    if (!avcodec_send_packet(pCodecContext, pPacket)) {
      avcodec_receive_frame(pCodecContext, pFrame);
      if (pFrame->linesize[0] > 0) {
        if (SDL_AudioStreamPut(stream, pFrame->data[0], pFrame->linesize[0]) ==
            -1) {  // FIXME(m): graceful handling.
          panic_and_abort("Audio stream put failed!", SDL_GetError());
        }
      }
    }

    // printf("Frame %c (%lld) pts %lld dts %lld\n", av_get_picture_type_char(pFrame->pict_type),
    //        pCodecContext->frame_num, pFrame->pts, pFrame->pkt_dts);
  }

  SDL_AudioStreamFlush(stream);  // FIXME(m): error handling.
  SDL_PauseAudioDevice(audio_device, paused);

  return SDL_TRUE;
}

int main(int argc, char** argv) {
  SDL_AudioSpec desired;
  SDL_bool running = SDL_TRUE;
  const SDL_Rect rewind_rect = {10, 10, 100, 100};
  const SDL_Rect stop_rect = {120, 10, 100, 100};
  const SDL_Rect play_pause_rect = {230, 10, 100, 100};
  const SDL_Rect fast_forward_rect = {340, 10, 100, 100};

  const SDL_Rect rewind_symbol_rect = {0, 0, 200, 200};
  const SDL_Rect play_symbol_rect = {220, 0, 200, 200};
  const SDL_Rect fast_forward_symbol_rect = {440, 0, 200, 200};
  const SDL_Rect stop_symbol_rect = {0, 220, 200, 200};
  const SDL_Rect pause_symbol_rect = {220, 220, 200, 200};

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
    panic_and_abort("SDL_Init failed!", SDL_GetError());
  }

  window =
      SDL_CreateWindow("SDLamp2", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, 0);
  if (!window) {
    panic_and_abort("SDL_CreateWindow failed!", SDL_GetError());
  }

  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
  if (!renderer) {
    panic_and_abort("SDL_CreateRenderer failed!", SDL_GetError());
  }

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

  // load_audio_file("music.wav");
  decode_audio();

  SDL_Surface* controls_surface = IMG_Load("controls.png");
  if (controls_surface == NULL) {
    panic_and_abort("Could not load controls asset!", SDL_GetError());
  }

  SDL_Texture* controls_texture = SDL_CreateTextureFromSurface(renderer, controls_surface);
  SDL_FreeSurface(controls_surface);
  if (controls_texture == NULL) {
    panic_and_abort("Could not load controls asset!", SDL_GetError());
  }

  SDL_Event e;
  while (running) {
    // Event loop
    while (SDL_PollEvent(&e)) {
      switch (e.type) {
        case SDL_QUIT:
          running = SDL_FALSE;
          break;

        case SDL_MOUSEBUTTONDOWN: {
          const SDL_Point pt = {e.button.x, e.button.y};
          if (SDL_PointInRect(&pt,
                              &rewind_rect)) {  // Pressed the "rewind" button
            SDL_ClearQueuedAudio(audio_device);
            SDL_AudioStreamClear(stream);
            if (SDL_AudioStreamPut(stream, wavbuf, wavlen) == -1) {  // FIXME(m): graceful handling.
              panic_and_abort("Audio stream put failed!", SDL_GetError());
            }
            SDL_AudioStreamFlush(stream);  // FIXME(m): error handling.
          } else if (SDL_PointInRect(&pt,
                                     &play_pause_rect)) {  // Pressed the "pause" button
            paused = paused ? SDL_FALSE : SDL_TRUE;
            SDL_PauseAudioDevice(audio_device, paused);
          }
          break;
        }
      }
    }

    // Deal with audio
    if (SDL_GetQueuedAudioSize(audio_device) < 8192) {
      const int bytes_remaining = SDL_AudioStreamAvailable(stream);
      if (bytes_remaining > 0) {
        const int new_bytes = SDL_min(bytes_remaining, 32 * 1024);
        static Uint8 converted_buffer[32 * 1024];
        SDL_AudioStreamGet(stream, converted_buffer,
                           new_bytes);  // FIXME(m): error handling.
        SDL_QueueAudio(audio_device, converted_buffer, new_bytes);
      }
    }

    // Deal with drawing to screen
    SDL_SetRenderDrawColor(renderer, 0xFF, 0xFF, 0xFF, 0xFF);
    SDL_RenderClear(renderer);

    SDL_SetRenderDrawColor(renderer, 0xFF, 0xFF, 0xFF, 0xFF);
    SDL_RenderFillRect(renderer, &rewind_rect);
    SDL_RenderFillRect(renderer, &stop_rect);
    SDL_RenderFillRect(renderer, &play_pause_rect);
    SDL_RenderFillRect(renderer, &fast_forward_rect);

    SDL_RenderCopy(renderer, controls_texture, &rewind_symbol_rect, &rewind_rect);
    SDL_RenderCopy(renderer, controls_texture, &stop_symbol_rect, &stop_rect);
    if (paused) {
      SDL_RenderCopy(renderer, controls_texture, &play_symbol_rect, &play_pause_rect);
    } else {
      SDL_RenderCopy(renderer, controls_texture, &pause_symbol_rect, &play_pause_rect);
    }
    SDL_RenderCopy(renderer, controls_texture, &fast_forward_symbol_rect, &fast_forward_rect);

    SDL_RenderPresent(renderer);
  }

  // Clean up
  SDL_FreeWAV(wavbuf);
  SDL_CloseAudioDevice(audio_device);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}
