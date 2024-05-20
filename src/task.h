typedef void (*TaskFunc)(void *data);

struct Task;

static Task *TaskCreate(TaskFunc func, void *data);
static bool TaskIsDone(Task *task);
// Wait for task to be done and release all its resources.
static void TaskWait(Task *task);
