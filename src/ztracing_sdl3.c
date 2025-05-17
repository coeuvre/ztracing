#include <stdlib.h>

#include "src/assert.h"
#include "src/canvas_sdl3.h"
#include "src/flick.h"
#include "src/math.h"
#include "src/memory.h"
#include "src/platform_sdl3.h"
#include "src/string.h"
#include "src/types.h"
#include "src/ztracing.h"

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static SDL_Window *window;
static SDL_Renderer *renderer;
static b32 window_shown;
static b32 window_coordinate_is_in_pixels;

static f32 GetScreenContentScale(void) {
  f32 result = SDL_GetWindowDisplayScale(window);
  if (result == 0) {
    result = 1;
  }
  return result;
}

Vec2 GetScreenSize(void) {
  Vec2I screen_size_in_pixel = {0};
  SDL_GetCurrentRenderOutputSize(renderer, &screen_size_in_pixel.x,
                                 &screen_size_in_pixel.y);
  Vec2 result = vec2_mul(vec2_from_vec2i(screen_size_in_pixel),
                         1.0f / GetScreenContentScale());
  return result;
}

static Vec2 get_window_size(void) {
  int w, h;
  SDL_GetWindowSizeInPixels(window, &w, &h);
  Vec2 size = vec2(w, h);
  if (window_coordinate_is_in_pixels) {
    size = vec2_mul(size, 1.0f / GetScreenContentScale());
  }
  return size;
}

typedef struct ZFileSDL3 {
  ZFile file;
  Arena arena;
  SDL_IOStream *io;
  Str8 buf;
} ZFileSDL3;

static Str8 z_file_sdl3_read(void *self_) {
  ZFileSDL3 *self = self_;

  if (self->file.interrupted) {
    return str8_zero();
  }

  usize nread = SDL_ReadIO(self->io, self->buf.ptr, self->buf.len);
  return str8(self->buf.ptr, nread);
}

static void z_file_sdl3_close(void *self_) {
  ZFileSDL3 *self = self_;
  SDL_CloseIO(self->io);
  arena_free(&self->arena);
}

static ZFile *z_file_sdl3_open(const char *path, usize buf_len) {
  SDL_IOStream *io = SDL_IOFromFile(path, "r");
  ASSERTF(io, "%s", SDL_GetError());
  Arena arena_ = {0};
  ZFileSDL3 *file = arena_push_struct(&arena_, ZFileSDL3);
  Str8 name = str8_dup(&arena_, str8_from_cstr(path));
  Str8 buf = arena_push_str8(&arena_, buf_len);
  *file = (ZFileSDL3){
      .file =
          {
              .name = name,
              .read = z_file_sdl3_read,
              .close = z_file_sdl3_close,
          },
      .arena = arena_,
      .io = io,
      .buf = buf,
  };
  return (ZFile *)file;
}

static void parse_json(const char *path) {
  usize buf_len = 1024 * 1024;
  ZFile *file = z_file_sdl3_open(path, buf_len);
  z_load_file(file);
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
  (void)appstate, (void)argc, (void)argv;

  platform_sdl3_init();

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

  renderer = SDL_CreateRenderer(window, 0);
  ASSERTF(renderer, "Failed to create renderer: %s", SDL_GetError());
  SDL_SetRenderVSync(renderer, 1);

  FL_Init(&(FL_InitOptions){
      .canvas = Canvas_Init(window, renderer),
  });

  return SDL_APP_CONTINUE;
}

static u32 g_sdl_button_to_ui_button[] = {
    [SDL_BUTTON_LEFT] = FL_MOUSE_BUTTON_PRIMARY,
    [SDL_BUTTON_MIDDLE] = FL_MOUSE_BUTTON_TERTIARY,
    [SDL_BUTTON_RIGHT] = FL_MOUSE_BUTTON_SECONDARY,
    [SDL_BUTTON_X1] = FL_MOUSE_BUTTON_FORWARD,
    [SDL_BUTTON_X2] = FL_MOUSE_BUTTON_BACK,
};

static Vec2 mouse_pos_from_sdl(Vec2 pos) {
  Vec2 result = pos;
  if (window_coordinate_is_in_pixels) {
    result = vec2_mul(result, 1.0f / GetScreenContentScale());
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
      // ui_on_focus_lost(get_global_window_relative_mouse_pos());
    } break;

    case SDL_EVENT_MOUSE_BUTTON_UP: {
      Vec2 mouse_pos =
          mouse_pos_from_sdl(vec2(event->button.x, event->button.y));
      FL_OnMouseButtonUp(mouse_pos,
                         g_sdl_button_to_ui_button[event->button.button]);
    } break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
      Vec2 mouse_pos =
          mouse_pos_from_sdl(vec2(event->button.x, event->button.y));
      FL_OnMouseButtonDown(mouse_pos,
                           g_sdl_button_to_ui_button[event->button.button]);
    } break;

    case SDL_EVENT_MOUSE_WHEEL: {
      Vec2 mouse_pos =
          mouse_pos_from_sdl(get_global_window_relative_mouse_pos());
      Vec2 delta = vec2(event->wheel.x, -event->wheel.y);
      FL_OnMouseScroll(mouse_pos, vec2_mul(delta, 10.0f));
    } break;

    default: {
    } break;
  }
  return result;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
  (void)appstate;

  FL_OnMouseMove(get_global_window_relative_mouse_pos());
  z_update(GetScreenSize());
  SDL_RenderPresent(renderer);

  if (!window_shown) {
    SDL_ShowWindow(window);
    window_shown = 1;
  }

  return SDL_APP_CONTINUE; /* carry on with the program! */
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
  (void)appstate, (void)result;
  z_quit();
}
