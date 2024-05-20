#include "task.h"

#include "os.h"

struct Task {
    TaskFunc func;
    void *data;

    OsMutex *mutex;
    OsCond *cond;
    bool done;
};

static Task *TaskCreate(TaskFunc func, void *data) {
    Task *task = (Task *)MemAlloc(sizeof(Task));
    ASSERT(task, "");
    task->func = func;
    task->data = data;
    task->mutex = OsMutexCreate();
    task->cond = OsCondCreate();
    task->done = false;

    bool dispatched = OsDispatchTask(task);
    ASSERT(dispatched, "");

    return task;
}

static bool TaskIsDone(Task *task) {
    OsMutexLock(task->mutex);
    bool done = task->done;
    OsMutexUnlock(task->mutex);
    return done;
}

static void TaskWait(Task *task) {
    OsMutexLock(task->mutex);
    while (!task->done) {
        OsCondWait(task->cond, task->mutex);
    }
    OsMutexUnlock(task->mutex);

    OsCondDestroy(task->cond);
    OsMutexDestroy(task->mutex);
    MemFree(task);
}
