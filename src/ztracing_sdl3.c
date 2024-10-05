#include "src/assert.h"
#include "src/draw_sdl3.h"
#include "src/ztracing.h"

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
  (void)argc, (void)argv;

  ASSERTF(SDL_Init(SDL_INIT_VIDEO), "Failed to init SDL3: %s", SDL_GetError());

  SDL_Window *window;
  SDL_Renderer *renderer;
  ASSERTF(SDL_CreateWindowAndRenderer(
              "ztracing", 1280, 720,
              SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &window,
              &renderer),
          "Failed to create window/renderer: %s", SDL_GetError());
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
  return SDL_APP_CONTINUE; /* carry on with the program! */
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {}
