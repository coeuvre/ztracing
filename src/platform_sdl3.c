#include <SDL3/SDL.h>

#include "src/assert.h"
#include "src/platform.h"
#include "src/types.h"

u64 Platform_GetPerformanceCounter(void) {
  u64 result = SDL_GetPerformanceCounter();
  return result;
}

u64 Platform_GetPerformanceFrequency(void) {
  u64 result = SDL_GetPerformanceFrequency();
  return result;
}

PlatformMutex *Platform_Mutex_Create(void) {
  SDL_Mutex *mutex = SDL_CreateMutex();
  ASSERTF(mutex, "%s", SDL_GetError());
  return (PlatformMutex *)mutex;
}

void Platform_Mutex_Lock(PlatformMutex *mutex_) {
  SDL_Mutex *mutex = (SDL_Mutex *)mutex_;
  SDL_LockMutex(mutex);
}

void Platform_Mutex_Unlock(PlatformMutex *mutex_) {
  SDL_Mutex *mutex = (SDL_Mutex *)mutex_;
  SDL_UnlockMutex(mutex);
}

void Platform_Mutex_Destroy(PlatformMutex *mutex_) {
  SDL_Mutex *mutex = (SDL_Mutex *)mutex_;
  SDL_DestroyMutex(mutex);
}

Platform_Condition *Platform_Condition_Create(void) {
  SDL_Condition *condition = SDL_CreateCondition();
  ASSERTF(condition, "%s", SDL_GetError());
  return (Platform_Condition *)condition;
}

void Platform_Condition_Wait(Platform_Condition *condition_,
                             PlatformMutex *mutex_) {
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
