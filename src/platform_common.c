#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stddef.h>

#include "src/platform.h"

#define JOB_QUEUE_CAPACITY 64

constexpr size_t NUM_WORKERS = 2;

typedef struct job {
  platform_job_fn_t fn;
  void* user_data;
} job_t;

typedef struct job_queue {
  job_t jobs[JOB_QUEUE_CAPACITY];
  size_t head;
  size_t tail;
  size_t size;
} job_queue_t;

static job_queue_t g_job_queue = {};
static pthread_mutex_t g_job_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_job_cv = PTHREAD_COND_INITIALIZER;
static pthread_t g_workers[NUM_WORKERS];
static bool g_worker_started = false;
static bool g_worker_should_exit = false;

static void* platform_worker_main(void* arg) {
  (void)arg;
  bool running = true;
  while (running) {
    job_t job = {nullptr, nullptr};

    pthread_mutex_lock(&g_job_mutex);
    while (g_job_queue.size == 0 && !g_worker_should_exit) {
      pthread_cond_wait(&g_job_cv, &g_job_mutex);
    }

    if (g_worker_should_exit && g_job_queue.size == 0) {
      running = false;
    } else if (g_job_queue.size > 0) {
      job = g_job_queue.jobs[g_job_queue.head];
      g_job_queue.head = (g_job_queue.head + 1) % JOB_QUEUE_CAPACITY;
      g_job_queue.size--;
    }
    pthread_mutex_unlock(&g_job_mutex);

    if (running && job.fn) {
      job.fn(job.user_data);
    }
  }
  return nullptr;
}

void platform_submit_job(platform_job_fn_t fn, void* user_data) {
  bool submitted = false;
  while (!submitted) {
    pthread_mutex_lock(&g_job_mutex);

    if (!g_worker_started) {
      for (size_t i = 0; i < NUM_WORKERS; i++) {
        pthread_create(&g_workers[i], nullptr, platform_worker_main, nullptr);
      }
      g_worker_started = true;
    }

    if (g_job_queue.size < JOB_QUEUE_CAPACITY) {
      g_job_queue.jobs[g_job_queue.tail] = (job_t){fn, user_data};
      g_job_queue.tail = (g_job_queue.tail + 1) % JOB_QUEUE_CAPACITY;
      g_job_queue.size++;
      pthread_cond_broadcast(&g_job_cv);
      submitted = true;
    }

    pthread_mutex_unlock(&g_job_mutex);

    if (!submitted) {
      // If the queue is full, we spin-yield instead of waiting on a second
      // condition variable (e.g., g_job_not_full_cv). This is a deliberate
      // design choice:
      // 1. Emscripten Main Thread Constraint: In WASM, the browser's main UI
      //    thread is strictly forbidden from blocking on condition variables
      //    (which use JavaScript Atomics.wait and throw exceptions).
      // 2. Safety Margin: The queue capacity (64) is vastly larger than the
      //    maximum concurrent background jobs (typically 1-2). The queue will
      //    virtually never be full, making this a rare fallback path.
      sched_yield();
    }
  }
}

void platform_teardown_workers() {
  pthread_mutex_lock(&g_job_mutex);
  if (g_worker_started) {
    g_worker_should_exit = true;
    g_job_queue.head = 0;
    g_job_queue.tail = 0;
    g_job_queue.size = 0;
    pthread_cond_broadcast(&g_job_cv);
    pthread_mutex_unlock(&g_job_mutex);

    for (size_t i = 0; i < NUM_WORKERS; i++) {
      pthread_join(g_workers[i], nullptr);
    }

    pthread_mutex_lock(&g_job_mutex);
    g_worker_started = false;
    g_worker_should_exit = false;
  }
  pthread_mutex_unlock(&g_job_mutex);
}
