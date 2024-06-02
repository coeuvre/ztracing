#include "task.h"

Task *
CreateTask(TaskFunc func, void *data) {
    Task *task = BootstrapPushStruct(Task, arena);
    task->func = func;
    task->data = data;
    task->mutex = OsCreateMutex();
    task->cond = OsCreateCond();
    task->done = false;

    OsDispatchTask(task);

    return task;
}

bool
IsTaskDone(Task *task) {
    OsLockMutex(task->mutex);
    bool done = task->done;
    OsUnlockMutex(task->mutex);
    return done;
}

void
CancelTask(Task *task) {
    OsLockMutex(task->mutex);
    task->cancelled = true;
    OsUnlockMutex(task->mutex);
}

bool
IsTaskCancelled(Task *task) {
    OsLockMutex(task->mutex);
    bool cancelled = task->cancelled;
    OsUnlockMutex(task->mutex);
    return cancelled;
}

void
WaitTask(Task *task) {
    OsLockMutex(task->mutex);
    while (!task->done) {
        OsWaitCond(task->cond, task->mutex);
    }
    OsUnlockMutex(task->mutex);

    OsDestroyCond(task->cond);
    OsDestroyMutex(task->mutex);

    ClearArena(&task->arena);
}
