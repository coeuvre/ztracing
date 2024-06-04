#pragma once

#include "core.h"

struct OsCond;
struct OsMutex;

OsCond *OsCreateCond();
void OsDestroyCond(OsCond *cond);
void OsWaitCond(OsCond *cond, OsMutex *mutex);
void OsSignal(OsCond *cond);
void OsBroadcast(OsCond *cond);

OsMutex *OsCreateMutex();
void OsDestroyMutex(OsMutex *mutex);
void OsLockMutex(OsMutex *mutex);
void OsUnlockMutex(OsMutex *mutex);

struct OsLoadingFile;
OsLoadingFile *OsOpenFile(char *path);
u32 OsReadFile(OsLoadingFile *file, u8 *buf, u32 len);
void OsCloseFile(OsLoadingFile *file);
Buffer OsGetFilePath(OsLoadingFile *file);

struct Task;
void OsDispatchTask(Task *task);

u64 OsGetPerformanceCounter();
u64 OsGetPerformanceFrequency();
