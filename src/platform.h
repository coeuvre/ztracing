#ifndef ZTRACING_SRC_PLATFORM_H_
#define ZTRACING_SRC_PLATFORM_H_

#include "src/types.h"

u64 platform_get_perf_counter(void);
u64 platform_get_perf_freq(void);

typedef struct PlatformMutex PlatformMutex;
PlatformMutex *platform_mutex_alloc(void);
void platform_mutex_lock(PlatformMutex *mutex);
void platform_mutex_unlock(PlatformMutex *mutex);
void platform_mutex_free(PlatformMutex *mutex);

typedef struct PlatformCondition PlatformCondition;
PlatformCondition *platform_condition_alloc(void);
void platform_condition_wait(PlatformCondition *condition,
                             PlatformMutex *mutex);
void platform_condition_signal(PlatformCondition *condition);
void platform_condition_broadcast(PlatformCondition *condition);
void platform_condition_free(PlatformCondition *condition);

typedef struct PlatformSemaphore PlatformSemaphore;
PlatformSemaphore *platform_semaphore_alloc(u32 initial_value);
void platform_semaphore_acquire(PlatformSemaphore *semaphore);
void platform_semaphore_release(PlatformSemaphore *semaphore);
void platform_semaphore_free(PlatformSemaphore *semaphore);

typedef int PlatformThreadFunction(void *data);
typedef struct PlatformThread PlatformThread;
PlatformThread *platform_thread_start(PlatformThreadFunction func,
                                      const char *name, void *data);
int platform_thread_wait(PlatformThread *thread);

#endif  // ZTRACING_SRC_PLATFORM_H_
