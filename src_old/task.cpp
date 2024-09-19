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

static Task *CreateTask(TaskFunc func, void *data) {
  Task *task = BootstrapPushStruct(Task, arena);
  task->func = func;
  task->data = data;
  task->mutex = OsCreateMutex();
  task->cond = OsCreateCond();
  task->done = false;

  OsDispatchTask(task);

  return task;
}

static bool IsTaskDone(Task *task) {
  OsLockMutex(task->mutex);
  bool done = task->done;
  OsUnlockMutex(task->mutex);
  return done;
}

static void CancelTask(Task *task) {
  OsLockMutex(task->mutex);
  task->cancelled = true;
  OsUnlockMutex(task->mutex);
}

static bool IsTaskCancelled(Task *task) {
  OsLockMutex(task->mutex);
  bool cancelled = task->cancelled;
  OsUnlockMutex(task->mutex);
  return cancelled;
}

// Wait for task to be done and release all its resources. Return true if task
// is not cancelled.
static bool WaitTask(Task *task) {
  bool result = false;

  OsLockMutex(task->mutex);
  while (!task->done) {
    OsWaitCond(task->cond, task->mutex);
  }
  result = !task->cancelled;
  OsUnlockMutex(task->mutex);

  OsDestroyCond(task->cond);
  OsDestroyMutex(task->mutex);

  ClearArena(&task->arena);

  return result;
}
