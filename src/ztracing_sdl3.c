#include "src/assert.h"
#include "src/draw_sdl3.h"
#include "src/types.h"
#include "src/ztracing.h"

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static SDL_Window *window;
static b32 window_shown;

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
  (void)argc, (void)argv;

  ASSERTF(SDL_Init(SDL_INIT_VIDEO), "Failed to init SDL3: %s", SDL_GetError());

  int width = 1280;
  int height = 720;

  window = SDL_CreateWindow(
      "ztracing", 1280, 720,
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
  ASSERTF(window, "Failed to create window: %s", SDL_GetError());

  f32 scale = SDL_GetWindowDisplayScale(window);
  if (scale != 1.0f) {
    width *= scale;
    height *= scale;
    SDL_SetWindowSize(window, width, height);
  }

  SDL_Renderer *renderer = SDL_CreateRenderer(window, 0);
  ASSERTF(renderer, "Failed to create renderer: %s", SDL_GetError());
  SDL_SetRenderVSync(renderer, 1);

  InitDrawSDL3(window, renderer);

  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
  SDL_AppResult result = SDL_APP_CONTINUE;
  if (event->type == SDL_EVENT_QUIT) {
    result = SDL_APP_SUCCESS;
  }
  return result;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
  DoFrame();
  if (!window_shown) {
    SDL_ShowWindow(window);
    window_shown = 1;
  }

  return SDL_APP_CONTINUE; /* carry on with the program! */
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {}
