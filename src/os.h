#pragma once

struct OsLoadingFile;
static OsLoadingFile *os_loading_file_open(char *path);
static u32 os_loading_file_next(OsLoadingFile *file, u8 *buf, u32 len);
static void os_loading_file_close(OsLoadingFile *file);

typedef int (*OsThreadFunction)(void *data);

struct OsThread;

static OsThread *os_thread_create(OsThreadFunction fn, void *data);
static void os_thread_join(OsThread *thread);

