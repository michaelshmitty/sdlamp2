#include "SDL.h"

static SDL_AudioDeviceID audio_device = 0;
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;

static void panic_and_abort(const char *title, const char *text) {
  fprintf(stderr, "PANIC: %s ... %s\n", title, text);
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, text, window);
  SDL_Quit();
  exit(1);
}

int main(int argc, char **argv) {
  Uint8 *wavbuf = NULL;
  Uint32 wavlen = 0;
  SDL_AudioSpec wavspec;

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
    panic_and_abort("SDL_Init failed!", SDL_GetError());
  }

  window = SDL_CreateWindow("SDLamp2", SDL_WINDOWPOS_UNDEFINED,
                            SDL_WINDOWPOS_UNDEFINED, 640, 480, 0);
  if (!window) {
    panic_and_abort("SDL_CreateWindow failed!", SDL_GetError());
  }

  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
  if (!renderer) {
    panic_and_abort("SDL_CreateRenderer failed!", SDL_GetError());
  }

  if (SDL_LoadWAV("music.wav", &wavspec, &wavbuf, &wavlen) == NULL) {
    panic_and_abort("Couldn't load wav file!", SDL_GetError());
  }
  audio_device = SDL_OpenAudioDevice(NULL, 0, &wavspec, NULL, 0);
  if (audio_device == 0) {
    panic_and_abort("Could not open audio device!", SDL_GetError());
  }
  SDL_QueueAudio(audio_device, wavbuf, wavlen);

  SDL_bool paused = SDL_TRUE;
  const SDL_Rect rewind_rect = {100, 100, 100, 100};
  const SDL_Rect pause_rect = {400, 100, 100, 100};

  SDL_bool running = SDL_TRUE;
  SDL_Event e;

  while (running) {
    while (SDL_PollEvent(&e)) {
      switch (e.type) {
      case SDL_QUIT:
        running = SDL_FALSE;
        break;

      case SDL_MOUSEBUTTONDOWN: {
        const SDL_Point pt = {e.button.x, e.button.y};
        if (SDL_PointInRect(&pt, &rewind_rect)) {
          SDL_ClearQueuedAudio(audio_device);
          SDL_QueueAudio(audio_device, wavbuf, wavlen);
        } else if (SDL_PointInRect(&pt, &pause_rect)) {
          paused = paused ? SDL_FALSE : SDL_TRUE;
          SDL_PauseAudioDevice(audio_device, paused);
        }
        break;
      }
      }
    }

    SDL_SetRenderDrawColor(renderer, 0, 0xFF, 0, 0xFF);
    SDL_RenderClear(renderer);

    SDL_SetRenderDrawColor(renderer, 0xFF, 0xFF, 0xFF, 0xFF);
    SDL_RenderFillRect(renderer, &rewind_rect);
    SDL_RenderFillRect(renderer, &pause_rect);

    SDL_RenderPresent(renderer);
  }

  SDL_FreeWAV(wavbuf);
  SDL_CloseAudioDevice(audio_device);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}
