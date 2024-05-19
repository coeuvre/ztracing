#pragma once

struct OsLoadingFile;
static OsLoadingFile *OsLoadingFileOpen(char *path);
static u32 OsLoadingFileNext(OsLoadingFile *file, u8 *buf, u32 len);
static void OsLoadingFileClose(OsLoadingFile *file);

typedef int (*OsThreadFunction)(void *data);

struct OsThread;

static OsThread *OsThreadCreate(OsThreadFunction fn, void *data);
static void OsThreadJoin(OsThread *thread);

static u64 OsGetPerformanceCounter();
static u64 OsGetPerformanceFrequency();
