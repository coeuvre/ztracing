#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>

#include "src/platform.h"

extern "C" {

struct Job {
  PlatformJobFn fn;
  void* user_data;
};

static std::queue<Job> g_job_queue;
static std::mutex g_job_mutex;
static std::condition_variable g_job_cv;
static std::thread g_workers[2];
static bool g_worker_started = false;
static bool g_worker_should_exit = false;

static void platform_worker_main() {
  while (true) {
    Job job = {nullptr, nullptr};
    {
      std::unique_lock<std::mutex> lock(g_job_mutex);
      g_job_cv.wait(lock, [] { return !g_job_queue.empty() || g_worker_should_exit; });
      if (g_worker_should_exit && g_job_queue.empty()) break;
      if (!g_job_queue.empty()) {
        job = g_job_queue.front();
        g_job_queue.pop();
      }
    }
    if (job.fn) {
      job.fn(job.user_data);
    }
  }
}

void platform_submit_job(PlatformJobFn fn, void* user_data) {
  std::lock_guard<std::mutex> lock(g_job_mutex);
  if (!g_worker_started) {
    for (int i = 0; i < 2; i++) {
      g_workers[i] = std::thread(platform_worker_main);
    }
    g_worker_started = true;
  }
  g_job_queue.push({fn, user_data});
  g_job_cv.notify_all();
}

} // extern "C"
