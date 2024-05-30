#include "task.h"

#include "os.h"

static Task *
CreateTask(TaskFunc func, void *data) {
    Task *task = BootstrapPushStruct(Task, arena);
    task->func = func;
    task->data = data;
    task->mutex = OsMutexCreate();
    task->cond = OsCondCreate();
    task->done = false;

    OsDispatchTask(task);

    return task;
}

static bool
IsTaskDone(Task *task) {
    OsMutexLock(task->mutex);
    bool done = task->done;
    OsMutexUnlock(task->mutex);
    return done;
}

static void
CancelTask(Task *task) {
    OsMutexLock(task->mutex);
    task->cancelled = true;
    OsMutexUnlock(task->mutex);
}

static bool
IsTaskCancelled(Task *task) {
    OsMutexLock(task->mutex);
    bool cancelled = task->cancelled;
    OsMutexUnlock(task->mutex);
    return cancelled;
}

static void
WaitTask(Task *task) {
    OsMutexLock(task->mutex);
    while (!task->done) {
        OsCondWait(task->cond, task->mutex);
    }
    OsMutexUnlock(task->mutex);

    OsCondDestroy(task->cond);
    OsMutexDestroy(task->mutex);

    Clear(&task->arena);
}
