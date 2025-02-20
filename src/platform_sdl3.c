#include <SDL3/SDL.h>

#include "src/assert.h"
#include "src/memory.h"
#include "src/platform.h"
#include "src/types.h"

static PlatformMutex *g_allocated_bytes_mutex;
static usize g_allocated_bytes;

static void *platform_memory_alloc(usize size) {
  platform_mutex_lock(g_allocated_bytes_mutex);
  g_allocated_bytes += size;
  platform_mutex_unlock(g_allocated_bytes_mutex);
  return SDL_malloc(size);
}

static void platform_memory_free(void *ptr, usize size) {
  platform_mutex_lock(g_allocated_bytes_mutex);
  g_allocated_bytes -= size;
  platform_mutex_unlock(g_allocated_bytes_mutex);
  SDL_free(ptr);
}

void platform_sdl3_init(void) {
  g_allocated_bytes_mutex = platform_mutex_alloc();
  memory_set_callback(platform_memory_alloc, platform_memory_free);
}

usize platform_get_allocated_bytes(void) { return g_allocated_bytes; }

u64 platform_get_perf_counter(void) {
  u64 result = SDL_GetPerformanceCounter();
  return result;
}

u64 platform_get_perf_freq(void) {
  u64 result = SDL_GetPerformanceFrequency();
  return result;
}

PlatformMutex *platform_mutex_alloc(void) {
  SDL_Mutex *mutex = SDL_CreateMutex();
  ASSERTF(mutex, "%s", SDL_GetError());
  return (PlatformMutex *)mutex;
}

void platform_mutex_lock(PlatformMutex *mutex_) {
  SDL_Mutex *mutex = (SDL_Mutex *)mutex_;
  SDL_LockMutex(mutex);
}

void platform_mutex_unlock(PlatformMutex *mutex_) {
  SDL_Mutex *mutex = (SDL_Mutex *)mutex_;
  SDL_UnlockMutex(mutex);
}

void platform_mutex_free(PlatformMutex *mutex_) {
  SDL_Mutex *mutex = (SDL_Mutex *)mutex_;
  SDL_DestroyMutex(mutex);
}

PlatformCondition *platform_condition_alloc(void) {
  SDL_Condition *condition = SDL_CreateCondition();
  ASSERTF(condition, "%s", SDL_GetError());
  return (PlatformCondition *)condition;
}

void platform_condition_wait(PlatformCondition *condition_,
                             PlatformMutex *mutex_) {
  SDL_Condition *condition = (SDL_Condition *)condition_;
  SDL_Mutex *mutex = (SDL_Mutex *)mutex_;
  SDL_WaitCondition(condition, mutex);
}

void platform_condition_signal(PlatformCondition *condition_) {
  SDL_Condition *condition = (SDL_Condition *)condition_;
  SDL_SignalCondition(condition);
}

void platform_condition_broadcast(PlatformCondition *condition_) {
  SDL_Condition *condition = (SDL_Condition *)condition_;
  SDL_BroadcastCondition(condition);
}

void platform_condition_free(PlatformCondition *condition_) {
  SDL_Condition *condition = (SDL_Condition *)condition_;
  SDL_DestroyCondition(condition);
}

PlatformSemaphore *platform_semaphore_alloc(u32 initial_value) {
  SDL_Semaphore *semaphore = SDL_CreateSemaphore(initial_value);
  ASSERTF(semaphore, "%s", SDL_GetError());
  return (PlatformSemaphore *)semaphore;
}

void platform_semaphore_acquire(PlatformSemaphore *semaphore_) {
  SDL_Semaphore *semaphore = (SDL_Semaphore *)semaphore_;
  SDL_WaitSemaphore(semaphore);
}

void platform_semaphore_release(PlatformSemaphore *semaphore_) {
  SDL_Semaphore *semaphore = (SDL_Semaphore *)semaphore_;
  SDL_SignalSemaphore(semaphore);
}

void platform_semaphore_free(PlatformSemaphore *semaphore_) {
  SDL_Semaphore *semaphore = (SDL_Semaphore *)semaphore_;
  SDL_DestroySemaphore(semaphore);
}

PlatformThread *platform_thread_start(PlatformThreadFunction func,
                                      const char *name, void *data) {
  SDL_Thread *thread = SDL_CreateThread(func, name, data);
  ASSERTF(thread, "%s", SDL_GetError());
  return (PlatformThread *)thread;
}

int platform_thread_wait(PlatformThread *thread_) {
  SDL_Thread *thread = (SDL_Thread *)thread_;
  int status;
  SDL_WaitThread(thread, &status);
  return status;
}
