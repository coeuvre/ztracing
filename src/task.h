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

Task *CreateTask(TaskFunc func, void *data);
bool IsTaskDone(Task *task);
void CancelTask(Task *task);
bool IsTaskCancelled(Task *task);
// Wait for task to be done and release all its resources.
void WaitTask(Task *task);
