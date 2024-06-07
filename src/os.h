#pragma once

struct OsCond;
struct OsMutex;

static OsCond *OsCreateCond();
static void OsDestroyCond(OsCond *cond);
static void OsWaitCond(OsCond *cond, OsMutex *mutex);
static void OsSignal(OsCond *cond);
static void OsBroadcast(OsCond *cond);

static OsMutex *OsCreateMutex();
static void OsDestroyMutex(OsMutex *mutex);
static void OsLockMutex(OsMutex *mutex);
static void OsUnlockMutex(OsMutex *mutex);

struct OsLoadingFile;
static OsLoadingFile *OsOpenFile(char *path);
static u32 OsReadFile(OsLoadingFile *file, u8 *buf, u32 len);
static void OsCloseFile(OsLoadingFile *file);
static Buffer OsGetFilePath(OsLoadingFile *file);

struct Task;
static void OsDispatchTask(Task *task);

static u64 OsGetPerformanceCounter();
static u64 OsGetPerformanceFrequency();
