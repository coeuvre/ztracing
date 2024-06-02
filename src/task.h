#pragma once

#include "memory.h"
#include "os.h"

struct Task;

typedef void (*TaskFunc)(Task *data);

struct Task {
    Arena arena;
    TaskFunc func;
    void *data;

    OsMutex *mutex;
    OsCond *cond;
    bool done;
    bool cancelled;
};

static Task *CreateTask(TaskFunc func, void *data);
static bool IsTaskDone(Task *task);
static void CancelTask(Task *task);
static bool IsTaskCancelled(Task *task);
// Wait for task to be done and release all its resources.
static void WaitTask(Task *task);
