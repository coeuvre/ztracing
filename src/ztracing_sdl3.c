#include <stdlib.h>

#include "src/assert.h"
#include "src/canvas_sdl3.h"
#include "src/flick.h"
#include "src/math.h"
#include "src/memory.h"
#include "src/platform.h"
#include "src/platform_sdl3.h"
#include "src/string.h"
#include "src/types.h"
#include "src/ztracing.h"

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static SDL_Window *window;
static SDL_Renderer *renderer;
static bool window_shown;
static bool window_coordinate_is_in_pixels;

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
  Vec2 result = Vec2_Mul(Vec2_FromVec2I(screen_size_in_pixel),
                         1.0f / GetScreenContentScale());
  return result;
}

static Vec2 GetWindowSize(void) {
  int w, h;
  SDL_GetWindowSizeInPixels(window, &w, &h);
  Vec2 size = vec2(w, h);
  if (window_coordinate_is_in_pixels) {
    size = Vec2_Mul(size, 1.0f / GetScreenContentScale());
  }
  return size;
}

typedef struct SDL3LoadingFile {
  LoadingFile file;
  Arena *arena;
  SDL_IOStream *io;
  Str buf;
} SDL3LoadingFile;

static Str SDL3LoadingFile_Read(void *self_) {
  SDL3LoadingFile *self = self_;

  if (self->file.interrupted) {
    return Str_Zero();
  }

  usize nread = SDL_ReadIO(self->io, self->buf.ptr, self->buf.len);
  return (Str){self->buf.ptr, nread};
}

static void SDL3LoadingFile_Close(void *self_) {
  SDL3LoadingFile *self = self_;
  SDL_CloseIO(self->io);
  Arena_Destroy(self->arena);
}

static LoadingFile *SDL3LoadingFile_Open(const char *path, usize buf_len,
                                         Allocator allocator) {
  SDL_IOStream *io = SDL_IOFromFile(path, "r");
  ASSERTF(io, "%s", SDL_GetError());
  Arena *arena = Arena_Create(&(ArenaOptions){
      .allocator = allocator,
  });
  SDL3LoadingFile *file = Arena_PushStruct(arena, SDL3LoadingFile);
  Str name = Str_Dup(arena, Str_FromCStr(path));
  Str buf = Arena_PushStr(arena, buf_len);
  *file = (SDL3LoadingFile){
      .file =
          {
              .name = name,
              .read = SDL3LoadingFile_Read,
              .close = SDL3LoadingFile_Close,
          },
      .arena = arena,
      .io = io,
      .buf = buf,
  };
  return (LoadingFile *)file;
}

static void ParseJson(App *app, const char *path, Allocator allocator) {
  usize buf_len = 1024 * 1024;
  LoadingFile *file = SDL3LoadingFile_Open(path, buf_len, allocator);
  App_LoadFile(app, file);
}

typedef struct Platform_Allocator {
  Platform_Mutex *allocated_bytes_mutex;
  isize allocated_bytes;
} Platform_Allocator;

static void *Platform_Allocator_Alloc(void *ctx, FL_isize size) {
  Platform_Allocator *allocator = ctx;

  isize total_size = size + sizeof(isize);
  isize *p = SDL_malloc(total_size);
  ASSERTF(p, "%s", SDL_GetError());
  *p = size;
  Platform_Mutex_Lock(allocator->allocated_bytes_mutex);
  allocator->allocated_bytes += total_size;
  Platform_Mutex_Unlock(allocator->allocated_bytes_mutex);
  return p + 1;
}

static void Platform_Allocator_Free(void *ctx, void *ptr, FL_isize size) {
  Platform_Allocator *allocator = ctx;

  isize total_size = size + sizeof(isize);
  isize *p = ((isize *)ptr) - 1;
  ASSERTF(*p == size, "free size (%zu) doesn't match alloc size (%zu)", *p,
          size);
  SDL_free(p);
  Platform_Mutex_Lock(allocator->allocated_bytes_mutex);
  allocator->allocated_bytes -= total_size;
  Platform_Mutex_Unlock(allocator->allocated_bytes_mutex);
}

static FL_AllocatorOps Platform_Allocator_Ops = {
    .alloc = Platform_Allocator_Alloc,
    .free = Platform_Allocator_Free,
};

static Platform_Allocator global_allocator;

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
  global_allocator.allocated_bytes_mutex = Platform_Mutex_Create();

  FL_Allocator allocator = {.ptr = &global_allocator,
                            .ops = &Platform_Allocator_Ops};

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
      .allocator = allocator,
      .canvas = Canvas_Init(window, renderer, allocator),
  });

  App *app = App_Create(allocator);
  *appstate = app;

  if (argc > 1) {
    ParseJson(app, argv[1], allocator);
  }

  return SDL_APP_CONTINUE;
}

static u32 g_sdl_button_to_ui_button[] = {
    [SDL_BUTTON_LEFT] = FL_MOUSE_BUTTON_PRIMARY,
    [SDL_BUTTON_MIDDLE] = FL_MOUSE_BUTTON_TERTIARY,
    [SDL_BUTTON_RIGHT] = FL_MOUSE_BUTTON_SECONDARY,
    [SDL_BUTTON_X1] = FL_MOUSE_BUTTON_FORWARD,
    [SDL_BUTTON_X2] = FL_MOUSE_BUTTON_BACK,
};

static Vec2 ConvertMousePos(Vec2 pos) {
  Vec2 result = pos;
  if (window_coordinate_is_in_pixels) {
    result = Vec2_Mul(result, 1.0f / GetScreenContentScale());
  }
  return result;
}

static Vec2 GetGlobalMousePosRelativeToWindow(void) {
  Vec2I window_pos;
  SDL_GetWindowPosition(window, &window_pos.x, &window_pos.y);
  Vec2 abs_mouse_pos;
  SDL_GetGlobalMouseState(&abs_mouse_pos.x, &abs_mouse_pos.y);
  Vec2 result = Vec2_Sub(abs_mouse_pos, Vec2_FromVec2I(window_pos));
  return ConvertMousePos(result);
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
  (void)appstate;

  SDL_AppResult result = SDL_APP_CONTINUE;
  switch (event->type) {
    case SDL_EVENT_QUIT: {
      result = SDL_APP_SUCCESS;
    } break;

    case SDL_EVENT_WINDOW_FOCUS_LOST: {
      // ui_on_focus_lost(GetGlobalMousePosRelativeToWindow());
    } break;

    case SDL_EVENT_MOUSE_BUTTON_UP: {
      Vec2 mouse_pos = ConvertMousePos(vec2(event->button.x, event->button.y));
      FL_OnMouseButtonUp(mouse_pos,
                         g_sdl_button_to_ui_button[event->button.button]);
    } break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
      Vec2 mouse_pos = ConvertMousePos(vec2(event->button.x, event->button.y));
      FL_OnMouseButtonDown(mouse_pos,
                           g_sdl_button_to_ui_button[event->button.button]);
    } break;

    case SDL_EVENT_MOUSE_WHEEL: {
      Vec2 mouse_pos = ConvertMousePos(GetGlobalMousePosRelativeToWindow());
      Vec2 delta = vec2(event->wheel.x, -event->wheel.y);
      FL_OnMouseScroll(mouse_pos, Vec2_Mul(delta, 10.0f));
    } break;

    default: {
    } break;
  }
  return result;
}

SDL_AppResult SDL_AppIterate(void *app) {
  FL_OnMouseMove(GetGlobalMousePosRelativeToWindow());
  App_Update(app, GetScreenSize(), global_allocator.allocated_bytes);
  SDL_RenderPresent(renderer);

  if (!window_shown) {
    SDL_ShowWindow(window);
    window_shown = 1;
  }

  return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *app, SDL_AppResult result) { App_Destroy(app); }
