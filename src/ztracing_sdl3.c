#include <stdlib.h>

#include "src/assert.h"
#include "src/draw.h"
#include "src/draw_sdl3.h"
#include "src/json.h"
#include "src/math.h"
#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"
#include "src/ui.h"
#include "src/ztracing.h"

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static SDL_Window *window;
static b32 window_shown;
static b32 window_coordinate_is_in_pixels;

u64 get_perf_counter(void) {
  u64 result = SDL_GetPerformanceCounter();
  return result;
}

u64 get_perf_freq(void) {
  u64 result = SDL_GetPerformanceFrequency();
  return result;
}

static Vec2 get_window_size(void) {
  int w, h;
  SDL_GetWindowSizeInPixels(window, &w, &h);
  Vec2 size = vec2(w, h);
  if (window_coordinate_is_in_pixels) {
    size = vec2_mul(size, 1.0f / get_screen_content_scale());
  }
  return size;
}

typedef struct GetInputContext {
  SDL_IOStream *io;
  Str8 buf;
  u64 nread;
} GetInputContext;

static Str8 get_input(void *c) {
  GetInputContext *context = c;
  usize nread = SDL_ReadIO(context->io, context->buf.ptr, context->buf.len);
  context->nread += nread;
  return str8(context->buf.ptr, nread);
}

static void parse_json(const char *path) {
  SDL_IOStream *input = SDL_IOFromFile(path, "r");
  ASSERTF(input, "%s", SDL_GetError());

  Scratch scratch = scratch_begin(0, 0);
  Str8 buf = arena_push_str8(scratch.arena, 100 * 1024);
  GetInputContext context = {
      .io = input,
      .buf = buf,
  };
  JsonParser parser = json_parser(get_input, &context);
  u64 before = SDL_GetPerformanceCounter();
  JsonToken t;
  do {
    Arena checkpoint = *scratch.arena;
    t = json_parser_parse_token(&parser, scratch.arena);
    *scratch.arena = checkpoint;
    ASSERTF(t.type != JSON_TOKEN_ERROR, "%.*s", (int)t.value.len, t.value.ptr);
  } while (t.type != JSON_TOKEN_EOF);
  u64 after = SDL_GetPerformanceCounter();

  f64 mb = (f64)context.nread / 1024.0 / 1024.0;
  f64 secs = (f64)(after - before) / (f64)SDL_GetPerformanceFrequency();
  INFO("Loaded %.1f MiB, %.1f MiB / s.", mb, mb / secs);

  scratch_end(scratch);

  exit(0);
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
  (void)appstate, (void)argc, (void)argv;

  if (argc > 1) {
    parse_json(argv[1]);
  }

  ASSERTF(SDL_Init(SDL_INIT_VIDEO), "Failed to init SDL3: %s", SDL_GetError());

  int width = 1280;
  int height = 720;
  window = SDL_CreateWindow(
      "ztracing", 1280, 720,
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
  ASSERTF(window, "Failed to create window: %s", SDL_GetError());

  int width_pixel, height_pixel;
  SDL_GetWindowSizeInPixels(window, &width_pixel, &height_pixel);
  f32 scale = SDL_GetWindowDisplayScale(window);
  if (scale != 1.0f && width_pixel == width) {
    window_coordinate_is_in_pixels = 1;
    width *= scale;
    height *= scale;
    SDL_SetWindowSize(window, width, height);
  }

  SDL_Renderer *renderer = SDL_CreateRenderer(window, 0);
  ASSERTF(renderer, "Failed to create renderer: %s", SDL_GetError());
  SDL_SetRenderVSync(renderer, 1);

  init_draw_sdl3(window, renderer);
  // ui_init();

  return SDL_APP_CONTINUE;
}

static u32 g_sdl_button_to_ui_button[] = {
    [SDL_BUTTON_LEFT] = UI_MOUSE_BUTTON_PRIMARY,
    [SDL_BUTTON_MIDDLE] = UI_MOUSE_BUTTON_TERTIARY,
    [SDL_BUTTON_RIGHT] = UI_MOUSE_BUTTON_SECONDARY,
    [SDL_BUTTON_X1] = UI_MOUSE_BUTTON_FORWARD,
    [SDL_BUTTON_X2] = UI_MOUSE_BUTTON_BACK,
};

static Vec2 mouse_pos_from_sdl(Vec2 pos) {
  Vec2 result = pos;
  if (window_coordinate_is_in_pixels) {
    result = vec2_mul(result, 1.0f / get_screen_content_scale());
  }
  return result;
}

static Vec2 get_global_window_relative_mouse_pos(void) {
  Vec2I window_pos;
  SDL_GetWindowPosition(window, &window_pos.x, &window_pos.y);
  Vec2 abs_mouse_pos;
  SDL_GetGlobalMouseState(&abs_mouse_pos.x, &abs_mouse_pos.y);
  Vec2 result = vec2_sub(abs_mouse_pos, vec2_from_vec2i(window_pos));
  return mouse_pos_from_sdl(result);
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
  (void)appstate;

  SDL_AppResult result = SDL_APP_CONTINUE;
  switch (event->type) {
    case SDL_EVENT_QUIT: {
      result = SDL_APP_SUCCESS;
    } break;

    case SDL_EVENT_WINDOW_FOCUS_LOST: {
      ui_on_focus_lost(get_global_window_relative_mouse_pos());
    } break;

    case SDL_EVENT_MOUSE_BUTTON_UP: {
      Vec2 mouse_pos =
          mouse_pos_from_sdl(vec2(event->button.x, event->button.y));
      ui_on_mouse_button_up(mouse_pos,
                            g_sdl_button_to_ui_button[event->button.button]);
    } break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
      Vec2 mouse_pos =
          mouse_pos_from_sdl(vec2(event->button.x, event->button.y));
      ui_on_mouse_button_down(mouse_pos,
                              g_sdl_button_to_ui_button[event->button.button]);
    } break;

    case SDL_EVENT_MOUSE_WHEEL: {
      Vec2 mouse_pos =
          mouse_pos_from_sdl(get_global_window_relative_mouse_pos());
      Vec2 delta = vec2(event->wheel.x, -event->wheel.y);
      ui_on_mouse_scroll(mouse_pos, vec2_mul(delta, 10.0f));
    } break;

    default: {
    } break;
  }
  return result;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
  (void)appstate;

  ui_set_viewport(vec2_zero(), get_screen_size());
  ui_on_mouse_move(get_global_window_relative_mouse_pos());
  do_frame();

  if (!window_shown) {
    SDL_ShowWindow(window);
    window_shown = 1;
  }

  return SDL_APP_CONTINUE; /* carry on with the program! */
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
  (void)appstate, (void)result;
  // ui_quit();
}
