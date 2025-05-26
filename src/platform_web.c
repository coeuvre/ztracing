#include <emscripten/emscripten.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "assets/JetBrainsMono-Regular.h"
#include "src/assert.h"
#include "src/channel.h"
#include "src/flick.h"
#include "src/flick_web.h"
#include "src/log.h"
#include "src/memory.h"
#include "src/platform.h"
#include "src/string.h"
#include "src/types.h"
#include "src/ztracing.h"

#ifndef EMSCRIPTEN_KEEPALIVE
#define EMSCRIPTEN_KEEPALIVE
#endif

u64 Platform_GetPerformanceCounter(void) {
  return (u64)(emscripten_get_now() * 1000000.0);
}

u64 Platform_GetPerformanceFrequency(void) { return 1000000000; }

struct Platform_Mutex {
  pthread_mutex_t raw;
};

Platform_Mutex *Platform_Mutex_Create(void) {
  Platform_Mutex *mutex = malloc(sizeof(Platform_Mutex));
  int err = pthread_mutex_init(&mutex->raw, 0);
  ASSERT(err == 0);
  return mutex;
}

void Platform_Mutex_Lock(Platform_Mutex *mutex) {
  int err = pthread_mutex_lock(&mutex->raw);
  ASSERT(err == 0);
}

void Platform_Mutex_Unlock(Platform_Mutex *mutex) {
  int err = pthread_mutex_unlock(&mutex->raw);
  ASSERT(err == 0);
}

void Platform_Mutex_Destroy(Platform_Mutex *mutex) {
  pthread_mutex_destroy(&mutex->raw);
  free(mutex);
}

struct Platform_Condition {
  pthread_cond_t raw;
};

Platform_Condition *Platform_Condition_Create(void) {
  Platform_Condition *cond = malloc(sizeof(Platform_Condition));
  int err = pthread_cond_init(&cond->raw, 0);
  ASSERT(err == 0);
  return cond;
}

void Platform_Condition_Wait(Platform_Condition *condition,
                             Platform_Mutex *mutex) {
  int err = pthread_cond_wait(&condition->raw, &mutex->raw);
  ASSERT(err == 0);
}

void Platform_Condition_Signal(Platform_Condition *condition) {
  int err = pthread_cond_signal(&condition->raw);
  ASSERT(err == 0);
}

void Platform_Condition_Broadcast(Platform_Condition *condition) {
  int err = pthread_cond_broadcast(&condition->raw);
  ASSERT(err == 0);
}

void Platform_Condition_Destroy(Platform_Condition *condition) {
  int err = pthread_cond_destroy(&condition->raw);
  ASSERT(err == 0);
  free(condition);
}

struct Platform_Semaphore {
  sem_t raw;
};

Platform_Semaphore *Platform_Semaphore_Create(u32 initial_value) {
  Platform_Semaphore *sem = malloc(sizeof(Platform_Semaphore));
  int err = sem_init(&sem->raw, 0, initial_value);
  ASSERT(err == 0);
  return sem;
}

void Platform_Semaphore_Acquire(Platform_Semaphore *semaphore) {
  int err = sem_wait(&semaphore->raw);
  ASSERT(err == 0);
}

void Platform_Semaphore_Release(Platform_Semaphore *semaphore) {
  int err = sem_post(&semaphore->raw);
  ASSERT(err == 0);
}

void Platform_Semaphore_Destroy(Platform_Semaphore *semaphore) {
  int err = sem_destroy(&semaphore->raw);
  ASSERT(err == 0);
  free(semaphore);
}

struct Platform_Thread {
  pthread_t raw;
};

typedef struct Platform_Thread_Args {
  Platform_ThreadFunction *func;
  void *data;
} Platform_Thread_Args;

static void *Platform_ThreadWrap(void *args_) {
  Platform_Thread_Args *args = args_;
  Platform_ThreadFunction *func = args->func;
  void *data = args->data;
  free(args);

  return (void *)func(data);
}
typedef void *(Platform_Thread_Func)(void *);

Platform_Thread *Platform_Thread_Start(Platform_ThreadFunction func,
                                       const char *name, void *data) {
  Platform_Thread *thread = malloc(sizeof(Platform_Thread));
  Platform_Thread_Args *args = malloc(sizeof(Platform_Thread_Args));
  *args = (Platform_Thread_Args){
      .func = func,
      .data = data,
  };
  int err = pthread_create(&thread->raw, 0, Platform_ThreadWrap, args);
  ASSERT(err == 0);
  return thread;
}

int Platform_Thread_Wait(Platform_Thread *thread) {
  void *retval;
  int err = pthread_join(thread->raw, &retval);
  ASSERT(err == 0);
  return (int)retval;
}

void LogMessage(LogLevel level, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  printf("\n");
}

typedef struct GlobalAllocator {
  Allocator parent;
  Platform_Mutex *allocated_bytes_mutex;
  isize allocated_bytes;
} GlobalAllocator;

static void *GlobalAllocator_Impl(void *ctx, void *ptr, FL_isize old_size,
                                  FL_isize new_size) {
  GlobalAllocator *allocator = ctx;
  void *result =
      allocator->parent.alloc(allocator->parent.ctx, ptr, old_size, new_size);
  Platform_Mutex_Lock(allocator->allocated_bytes_mutex);
  allocator->allocated_bytes += new_size - old_size;
  Platform_Mutex_Unlock(allocator->allocated_bytes_mutex);
  return result;
}

static void GlobalAllocator_Init(GlobalAllocator *a, Allocator parent) {
  *a = (GlobalAllocator){
      .parent = parent,
      .allocated_bytes_mutex = Platform_Mutex_Create(),
  };
}

static inline Allocator GlobalAllocator_AsAllocator(GlobalAllocator *a) {
  return (Allocator){
      .ctx = a,
      .alloc = &GlobalAllocator_Impl,
  };
}

static GlobalAllocator global_allocator;
static App *global_app;

EMSCRIPTEN_KEEPALIVE
void *JS_Alloc(isize size) {
  Allocator allocator = GlobalAllocator_AsAllocator(&global_allocator);
  return Allocator_Alloc(allocator, size);
}

EMSCRIPTEN_KEEPALIVE
void JS_Free(void *ptr, isize size) {
  Allocator allocator = GlobalAllocator_AsAllocator(&global_allocator);
  Allocator_Free(allocator, ptr, size);
}

typedef struct FLJS_Renderer_Vertex {
  FL_Vec2 pos;
  FL_Vec2 uv;
  FL_u32 color;
} FLJS_Renderer_Vertex;

extern FL_isize FLJS_Renderer_CreateTexture(int width, int height,
                                            void *pixels);
extern FL_isize FLJS_Renderer_UpdateTexture(FL_isize texture, int x, int y,
                                            int width, int height,
                                            void *pixels);
extern void FLJS_Renderer_SetBufferData(void *vtx_buffer_ptr,
                                        FL_isize vtx_buffer_len,
                                        void *idx_buffer_ptr,
                                        FL_isize idx_buffer_len);
extern void FLJS_Renderer_Draw(FL_isize texture, FL_i32 idx_count,
                               FL_i32 idx_offset);

static void RunTextureCommand(FL_TextureCommand *command) {
  if (command->texture->id) {
    FLJS_Renderer_UpdateTexture((FL_isize)command->texture->id, command->x,
                                command->y, command->width, command->height,
                                command->pixels);
  } else {
    command->texture->id = (void *)FLJS_Renderer_CreateTexture(
        command->width, command->height, command->pixels);
  }
}

static void RunDrawCommand(FL_DrawCommand *command) {
  FLJS_Renderer_Draw((FL_isize)command->texture->id, command->index_count,
                     command->index_offset);
}

static void Update(void) {
  App *app = global_app;

  FL_Vec2 canvas_size = FLJS_BeginFrame();

  FL_DrawList draw_list =
      App_Update(app, canvas_size, global_allocator.allocated_bytes);

  FLJS_EndFrame(&draw_list);
}

extern void JS_Init(void);

typedef struct JS_LoadingFileChunk {
  u8 *ptr;
  isize len;
} JS_LoadingFileChunk;

static void JS_LoadingFileChunk_Deinit(JS_LoadingFileChunk *chunk) {
  // Free the memory allocated by the JS side.
  JS_Free(chunk->ptr, chunk->len);
  *chunk = (JS_LoadingFileChunk){0};
}

typedef struct JS_LoadingFile {
  LoadingFile file;
  Arena *arena;
  Channel *channel;
  JS_LoadingFileChunk chunk;
} JS_LoadingFile;

static Str JS_LoadingFile_Read(void *file_) {
  JS_LoadingFile *file = file_;

  JS_LoadingFileChunk *chunk = &file->chunk;
  if (chunk->len) {
    JS_LoadingFileChunk_Deinit(chunk);
  }

  Channel_Receive(file->channel, chunk);

  Str result = {(char *)chunk->ptr, chunk->len};
  return result;
}

static void JS_LoadingFile_Close(void *file_) {
  JS_LoadingFile *file = file_;

  JS_LoadingFileChunk *chunk = &file->chunk;
  if (chunk->len) {
    JS_LoadingFileChunk_Deinit(chunk);
  }

  Channel_CloseRx(file->channel);

  Arena_Destroy(file->arena);
}

EMSCRIPTEN_KEEPALIVE
JS_LoadingFile *JS_LoadingFile_Begin(char *name_ptr, isize name_len) {
  Str name = {name_ptr, name_len};
  Allocator allocator = GlobalAllocator_AsAllocator(&global_allocator);
  Arena *arena = Arena_Create(&(ArenaOptions){.allocator = allocator});
  JS_LoadingFile *file = Arena_PushStruct(arena, JS_LoadingFile);
  *file = (JS_LoadingFile){
      .file =
          {
              .name = Str_Dup(arena, name),
              .read = JS_LoadingFile_Read,
              .close = JS_LoadingFile_Close,
          },
      .arena = arena,
      .channel = Channel_Create(2, sizeof(JS_LoadingFileChunk), allocator),
  };

  App_LoadFile(global_app, &file->file);

  return file;
}

EMSCRIPTEN_KEEPALIVE
bool JS_LoadingFile_OnChunk(JS_LoadingFile *file, u8 *ptr, isize len) {
  JS_LoadingFileChunk chunk = {.ptr = ptr, .len = len};
  return Channel_TrySend(file->channel, &chunk);
}

EMSCRIPTEN_KEEPALIVE
void JS_LoadingFile_End(JS_LoadingFile *file) {
  Channel_CloseTx(file->channel);
}

int main(int argc, const char *argv[]) {
  FLJS_Init();
  JS_Init();

  GlobalAllocator_Init(&global_allocator, FL_Allocator_GetDefault());
  Allocator allocator = GlobalAllocator_AsAllocator(&global_allocator);

  FL_Init(&(FL_InitOptions){
      .allocator = allocator,
  });

  FL_Font_Load(&(FL_FontOptions){
      .data = JetBrainsMono_Regular_ttf,
  });

  global_app = App_Create(allocator);

  emscripten_set_main_loop(Update, 0, false);

  return 0;
}
