#include <emscripten/emscripten.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "src/assert.h"
#include "src/flick.h"
#include "src/flick_web.h"
#include "src/log.h"
#include "src/platform.h"
#include "src/types.h"
#include "src/ztracing.h"

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

static void *Platform_Thread_Wrap(void *args_) {
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
  int err = pthread_create(&thread->raw, 0, Platform_Thread_Wrap, args);
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

static App *global_app;

static void Update(void) {
  App *app = global_app;

  static FL_Vec2 canvas_size;
  FLJS_ResizeCanvas(canvas_size.x, canvas_size.y, &canvas_size);

  App_Update(app, canvas_size, 0);
}

int main(int argc, const char *argv[]) {
  FLJS_Init();
  FL_Allocator allocator = FL_Allocator_GetDefault();

  FL_Init(&(FL_InitOptions){
      .allocator = allocator,
      .canvas = FLJS_Canvas_Get(),
  });

  global_app = App_Create(allocator);

  emscripten_set_main_loop(Update, 0, false);

  return 0;
}
