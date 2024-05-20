#pragma once

#include "core.h"

struct OsCond;
struct OsMutex;

static OsCond *OsCondCreate();
static void OsCondDestroy(OsCond *cond);
static void OsCondWait(OsCond *cond, OsMutex *mutex);
static void OsCondSingal(OsCond *cond);
static void OsCondBroadcast(OsCond *cond);

static OsMutex *OsMutexCreate();
static void OsMutexDestroy(OsMutex *mutex);
static void OsMutexLock(OsMutex *mutex);
static void OsMutexUnlock(OsMutex *mutex);

struct OsLoadingFile;
static OsLoadingFile *OsLoadingFileOpen(char *path);
static u32 OsLoadingFileNext(OsLoadingFile *file, u8 *buf, u32 len);
static void OsLoadingFileClose(OsLoadingFile *file);
static char *OsLoadingFileGetPath(OsLoadingFile *file);

typedef int (*OsThreadFunction)(void *data);

struct OsThread;

static OsThread *OsThreadCreate(OsThreadFunction fn, void *data);
static void OsThreadJoin(OsThread *thread);

static u64 OsGetPerformanceCounter();
static u64 OsGetPerformanceFrequency();
