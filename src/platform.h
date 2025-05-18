#ifndef ZTRACING_SRC_PLATFORM_H_
#define ZTRACING_SRC_PLATFORM_H_

#include "src/types.h"

u64 Platform_GetPerformanceCounter(void);
u64 Platform_GetPerformanceFrequency(void);

typedef struct PlatformMutex PlatformMutex;
PlatformMutex *Platform_Mutex_Create(void);
void Platform_Mutex_Lock(PlatformMutex *mutex);
void Platform_Mutex_Unlock(PlatformMutex *mutex);
void Platform_Mutex_Destroy(PlatformMutex *mutex);

typedef struct Platform_Condition Platform_Condition;
Platform_Condition *Platform_Condition_Create(void);
void Platform_Condition_Wait(Platform_Condition *condition,
                             PlatformMutex *mutex);
void Platform_Condition_Signal(Platform_Condition *condition);
void Platform_Condition_Broadcast(Platform_Condition *condition);
void Platform_Condition_Destroy(Platform_Condition *condition);

typedef struct Platform_Semaphore Platform_Semaphore;
Platform_Semaphore *Platform_Semaphore_Create(u32 initial_value);
void Platform_Semaphore_Acquire(Platform_Semaphore *semaphore);
void Platform_Semaphore_Release(Platform_Semaphore *semaphore);
void Platform_Semaphore_Destroy(Platform_Semaphore *semaphore);

typedef int Platform_ThreadFunction(void *data);
typedef struct Platform_Thread Platform_Thread;
Platform_Thread *Platform_Thread_Start(Platform_ThreadFunction func,
                                       const char *name, void *data);
int Platform_Thread_Wait(Platform_Thread *thread);

#endif  // ZTRACING_SRC_PLATFORM_H_
