#include <stdarg.h>

#include "assets/JetBrainsMono-Regular.h"
#include "src/assert.h"
#include "src/flick.h"
#include "src/log.h"
#include "src/math.h"
#include "src/memory.h"
#include "src/platform.h"
#include "src/string.h"
#include "src/types.h"
#include "src/ztracing.h"

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static SDL_LogPriority kLogLevelToPriority[LogLevel_Count] = {
    SDL_LOG_PRIORITY_DEBUG,
    SDL_LOG_PRIORITY_INFO,
    SDL_LOG_PRIORITY_WARN,
    SDL_LOG_PRIORITY_ERROR,
};

void LogMessage(LogLevel level, const char *fmt, ...) {
  int category = SDL_LOG_CATEGORY_APPLICATION;
  SDL_LogPriority priority = kLogLevelToPriority[level];
  if (priority >= SDL_GetLogPriority(category)) {
    SDL_SetLogPriorityPrefix(priority, "");
    va_list ap;
    va_start(ap, fmt);
    SDL_LogMessageV(category, priority, fmt, ap);
    va_end(ap);
  }
}

u64 Platform_GetPerformanceCounter(void) {
  u64 result = SDL_GetPerformanceCounter();
  return result;
}

u64 Platform_GetPerformanceFrequency(void) {
  u64 result = SDL_GetPerformanceFrequency();
  return result;
}

Platform_Mutex *Platform_Mutex_Create(void) {
  SDL_Mutex *mutex = SDL_CreateMutex();
  ASSERTF(mutex, "%s", SDL_GetError());
  return (Platform_Mutex *)mutex;
}

void Platform_Mutex_Lock(Platform_Mutex *mutex_) {
  SDL_Mutex *mutex = (SDL_Mutex *)mutex_;
  SDL_LockMutex(mutex);
}

void Platform_Mutex_Unlock(Platform_Mutex *mutex_) {
  SDL_Mutex *mutex = (SDL_Mutex *)mutex_;
  SDL_UnlockMutex(mutex);
}

void Platform_Mutex_Destroy(Platform_Mutex *mutex_) {
  SDL_Mutex *mutex = (SDL_Mutex *)mutex_;
  SDL_DestroyMutex(mutex);
}

Platform_Condition *Platform_Condition_Create(void) {
  SDL_Condition *condition = SDL_CreateCondition();
  ASSERTF(condition, "%s", SDL_GetError());
  return (Platform_Condition *)condition;
}

void Platform_Condition_Wait(Platform_Condition *condition_,
                             Platform_Mutex *mutex_) {
  SDL_Condition *condition = (SDL_Condition *)condition_;
  SDL_Mutex *mutex = (SDL_Mutex *)mutex_;
  SDL_WaitCondition(condition, mutex);
}

void Platform_Condition_Signal(Platform_Condition *condition_) {
  SDL_Condition *condition = (SDL_Condition *)condition_;
  SDL_SignalCondition(condition);
}

void Platform_Condition_Broadcast(Platform_Condition *condition_) {
  SDL_Condition *condition = (SDL_Condition *)condition_;
  SDL_BroadcastCondition(condition);
}

void Platform_Condition_Destroy(Platform_Condition *condition_) {
  SDL_Condition *condition = (SDL_Condition *)condition_;
  SDL_DestroyCondition(condition);
}

Platform_Semaphore *Platform_Semaphore_Create(u32 initial_value) {
  SDL_Semaphore *semaphore = SDL_CreateSemaphore(initial_value);
  ASSERTF(semaphore, "%s", SDL_GetError());
  return (Platform_Semaphore *)semaphore;
}

void Platform_Semaphore_Acquire(Platform_Semaphore *semaphore_) {
  SDL_Semaphore *semaphore = (SDL_Semaphore *)semaphore_;
  SDL_WaitSemaphore(semaphore);
}

void Platform_Semaphore_Release(Platform_Semaphore *semaphore_) {
  SDL_Semaphore *semaphore = (SDL_Semaphore *)semaphore_;
  SDL_SignalSemaphore(semaphore);
}

void Platform_Semaphore_Destroy(Platform_Semaphore *semaphore_) {
  SDL_Semaphore *semaphore = (SDL_Semaphore *)semaphore_;
  SDL_DestroySemaphore(semaphore);
}

Platform_Thread *Platform_Thread_Start(Platform_ThreadFunction func,
                                       const char *name, void *data) {
  SDL_Thread *thread = SDL_CreateThread(func, name, data);
  ASSERTF(thread, "%s", SDL_GetError());
  return (Platform_Thread *)thread;
}

int Platform_Thread_Wait(Platform_Thread *thread_) {
  SDL_Thread *thread = (SDL_Thread *)thread_;
  int status;
  SDL_WaitThread(thread, &status);
  return status;
}

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

static void *Platform_Allocator_Impl(void *ctx, void *ptr, FL_isize old_size,
                                     FL_isize new_size) {
  Platform_Allocator *allocator = ctx;

  void *result = 0;
  if (!old_size) {
    isize total_size = new_size + sizeof(isize);
    isize *p = SDL_malloc(total_size);
    ASSERTF(p, "%s", SDL_GetError());
    *p = new_size;
    Platform_Mutex_Lock(allocator->allocated_bytes_mutex);
    allocator->allocated_bytes += total_size;
    Platform_Mutex_Unlock(allocator->allocated_bytes_mutex);
    result = p + 1;
  } else if (!new_size) {
    isize total_size = old_size + sizeof(isize);
    isize *p = ((isize *)ptr) - 1;
    ASSERTF(*p == old_size,
            "old_size (%td) doesn't match allocation size (%td)", *p, old_size);
    SDL_free(p);
    Platform_Mutex_Lock(allocator->allocated_bytes_mutex);
    allocator->allocated_bytes -= total_size;
    Platform_Mutex_Unlock(allocator->allocated_bytes_mutex);
  } else {
    isize total_size = new_size + sizeof(isize);
    isize *p = ((isize *)ptr) - 1;
    ASSERTF(*p == old_size,
            "old_size (%td) doesn't match allocation size (%td)", *p, old_size);
    p = SDL_realloc(p, total_size);
    ASSERTF(p, "%s", SDL_GetError());
    *p = new_size;
    Platform_Mutex_Lock(allocator->allocated_bytes_mutex);
    allocator->allocated_bytes += new_size - old_size;
    Platform_Mutex_Unlock(allocator->allocated_bytes_mutex);
    result = p + 1;
  }
  return result;
}

static Platform_Allocator global_allocator_state;
static FL_Allocator global_allocator = {
    .ctx = &global_allocator_state,
    .alloc = &Platform_Allocator_Impl,
};

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
  global_allocator_state.allocated_bytes_mutex = Platform_Mutex_Create();

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

  FL_f32 pixels_per_point = GetScreenContentScale();
  SDL_SetRenderScale(renderer, pixels_per_point, pixels_per_point);

  FL_Init(&(FL_InitOptions){
      .allocator = global_allocator,
  });
  FL_SetPixelsPerPoint(pixels_per_point);

  FL_Font_Load(&(FL_FontOptions){
      .data = JetBrainsMono_Regular_ttf,
  });

  App *app = App_Create(global_allocator);
  *appstate = app;

  if (argc > 1) {
    ParseJson(app, argv[1], global_allocator);
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

static void RunTextureCommand(FL_TextureCommand *command) {
  if (command->texture->id) {
    bool ok = SDL_UpdateTexture(command->texture->id,
                                &(SDL_Rect){
                                    .x = command->x,
                                    .y = command->y,
                                    .w = command->width,
                                    .h = command->height,
                                },
                                command->pixels, sizeof(u32) * command->width);
    ASSERTF(ok, "%s", SDL_GetError());
  } else {
    command->texture->id = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                             SDL_TEXTUREACCESS_STREAMING,
                                             command->width, command->height);
    ASSERTF(command->texture->id, "%s", SDL_GetError());
  }
}

SDL_AppResult SDL_AppIterate(void *app) {
  FL_OnMouseMove(GetGlobalMousePosRelativeToWindow());

  FL_DrawList draw_list =
      App_Update(app, GetScreenSize(), global_allocator_state.allocated_bytes);

  for (isize i = 0; i < draw_list.texture_command_count; i++) {
    RunTextureCommand(draw_list.texture_commands + i);
  }

  SDL_FColor *colors = FL_Allocator_Alloc(
      global_allocator, sizeof(SDL_FColor) * draw_list.vertex_count);
  for (isize i = 0; i < draw_list.vertex_count; i++) {
    FL_DrawVertex *vertex = draw_list.vertex_buffer + i;
    colors[i].r = vertex->color.r / 255.0f;
    colors[i].g = vertex->color.g / 255.0f;
    colors[i].b = vertex->color.b / 255.0f;
    colors[i].a = vertex->color.a / 255.0f;
  }

  for (isize i = 0; i < draw_list.draw_command_count; i++) {
    FL_DrawCommand *draw = draw_list.draw_commands + i;
    SDL_SetRenderClipRect(renderer,
                          &(SDL_Rect){
                              .x = draw->clip_rect.left,
                              .y = draw->clip_rect.top,
                              .w = draw->clip_rect.right - draw->clip_rect.left,
                              .h = draw->clip_rect.bottom - draw->clip_rect.top,
                          });

    FL_DrawVertex *vertices = draw_list.vertex_buffer + draw->vertex_offset;
    FL_isize num_vertices = draw_list.vertex_count - draw->vertex_offset;
    FL_DrawIndex *indices = draw_list.index_buffer + draw->index_offset;

    bool ok = SDL_RenderGeometryRaw(
        renderer, draw->texture->id, (float *)&vertices->pos, sizeof(*vertices),
        colors + draw->vertex_offset, sizeof(*colors), (float *)&vertices->uv,
        sizeof(*vertices), num_vertices, indices, draw->index_count,
        sizeof(*indices));
    ASSERTF(ok, "%s", SDL_GetError());
  }

  FL_Allocator_Free(global_allocator, colors,
                    sizeof(*colors) * draw_list.vertex_count);

  SDL_RenderPresent(renderer);

  if (!window_shown) {
    SDL_ShowWindow(window);
    window_shown = 1;
  }

  return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *app, SDL_AppResult result) { App_Destroy(app); }
