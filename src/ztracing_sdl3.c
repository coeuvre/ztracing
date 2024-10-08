#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>

#include "src/assert.h"
#include "src/draw.h"
#include "src/draw_sdl3.h"
#include "src/log.h"
#include "src/math.h"
#include "src/types.h"
#include "src/ui.h"
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
  switch (event->type) {
    case SDL_EVENT_QUIT: {
      result = SDL_APP_SUCCESS;
    } break;

    default: {
    } break;
  }
  return result;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
  static u64 last_counter;

  f32 dt = 0.0f;
  u64 current_counter = SDL_GetPerformanceCounter();
  if (last_counter) {
    dt = (f32)((f64)(current_counter - last_counter) /
               (f64)SDL_GetPerformanceFrequency());
  }
  last_counter = current_counter;

  {
    Vec2I window_pos;
    SDL_GetWindowPosition(window, &window_pos.x, &window_pos.y);
    Vec2 mouse_pos;
    SDL_GetGlobalMouseState(&mouse_pos.x, &mouse_pos.y);
    mouse_pos = SubVec2(mouse_pos, Vec2FromVec2I(window_pos));
    mouse_pos = MulVec2(mouse_pos, 1.0f / GetScreenContentScale());
    OnUIMousePos(mouse_pos);
  }

  DoFrame(dt);

  if (!window_shown) {
    SDL_ShowWindow(window);
    window_shown = 1;
  }

  return SDL_APP_CONTINUE; /* carry on with the program! */
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {}
