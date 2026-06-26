#include "src/task.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "src/allocator.h"

// ─── Test Helpers ────────────────────────────────────────────────────────────

// A simple executor that spawns a new thread for each task.
static void thread_executor(void (*work_fn)(void*), void* arg) {
  std::thread(work_fn, arg).detach();
}

// A simple helper to poll for a completion with a timeout to prevent hanging
// tests
static bool wait_for_completion(task_queue_t* queue,
                                task_completion_t* out_comp,
                                double timeout_ms = 1000.0) {
  uint64_t timeout_ns = static_cast<uint64_t>(timeout_ms * 1000000.0);
  return task_queue_wait_completion_timeout(queue, out_comp, timeout_ns);
}

// Inline synchronous executor implementation
static void inline_executor(void (*work_fn)(void*), void* arg) { work_fn(arg); }

// Global dispatch counter and wrapper executor for dispatch-reduction testing
static std::atomic<int> g_mock_dispatch_count{0};
static void mock_dispatch_executor(void (*work_fn)(void*), void* arg) {
  g_mock_dispatch_count.fetch_add(1);
  thread_executor(work_fn, arg);
}

// ─── Fixture for Parameterized Tests ─────────────────────────────────────────

// The core parameterized fixture class for tests runnable on any executor
class task_queue_test : public testing::TestWithParam<task_executor_t> {
 protected:
  task_executor_t get_executor() { return GetParam(); }
};

// Instantiate the suite to run on both multi-threaded and inline executors!
INSTANTIATE_TEST_SUITE_P(any_executor, task_queue_test,
                         testing::Values(thread_executor, inline_executor));

// ─── Category 1: task_queue_test (Runnable on ANY Executor) ──────────────────

// Basic Submission and Completion
TEST_P(task_queue_test, basic_execution) {
  allocator_t alloc = allocator_get_default();
  task_queue_t* queue = task_queue_create(16, get_executor(), alloc);
  EXPECT_NE(queue, nullptr);

  std::atomic<int> run_count{0};

  // Define a simple task
  auto task_fn = [](task_context_t* ctx) {
    std::atomic<int>* counter = static_cast<std::atomic<int>*>(ctx->user_data);
    counter->fetch_add(1);
  };

  // 1. Get submission slot
  task_submission_t* sub = task_queue_get_submission(queue);
  EXPECT_NE(sub, nullptr);
  sub->task = task_fn;
  sub->user_data = &run_count;
  sub->stream = 0;  // Parallel stream

  // 2. Submit
  task_queue_submit(queue);

  // 3. Wait and reap completion
  task_completion_t comp;
  EXPECT_TRUE(wait_for_completion(queue, &comp));
  EXPECT_EQ(comp.user_data, &run_count);
  EXPECT_EQ(comp.status, TASK_STATUS_OK);

  task_queue_remove_completion(queue);

  // Verify the task ran
  EXPECT_EQ(run_count.load(), 1);

  task_queue_destroy(queue);
}

// Serialized Sequential Execution (Stream > 0)
TEST_P(task_queue_test, serialized_execution) {
  allocator_t alloc = allocator_get_default();
  task_queue_t* queue = task_queue_create(16, get_executor(), alloc);
  EXPECT_NE(queue, nullptr);

  std::vector<int> execution_order;
  std::mutex order_mutex;

  // Define a task that appends its ID to the execution order vector
  auto append_task = [](task_context_t* ctx) {
    struct payload_t {
      int id;
      std::vector<int>* order;
      std::mutex* mutex;
      int delay_ms;
    }* p = static_cast<payload_t*>(ctx->user_data);

    // Simulate different workloads by sleeping (task 1 sleeps longer than task
    // 2) Only sleep if running asynchronously to avoid blocking tests
    if (p->delay_ms > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(p->delay_ms));
    }

    std::lock_guard<std::mutex> lock(*p->mutex);
    p->order->push_back(p->id);
  };

  struct payload_t {
    int id;
    std::vector<int>* order;
    std::mutex* mutex;
    int delay_ms;
  };

  // Submit Task 1 (which has a long delay)
  payload_t p1{1, &execution_order, &order_mutex, 50};
  task_submission_t* sub1 = task_queue_get_submission(queue);
  sub1->task = append_task;
  sub1->user_data = &p1;
  sub1->stream = 42;  // Same non-zero stream

  // Submit Task 2 (which has no delay)
  payload_t p2{2, &execution_order, &order_mutex, 0};
  task_submission_t* sub2 = task_queue_get_submission(queue);
  sub2->task = append_task;
  sub2->user_data = &p2;
  sub2->stream = 42;  // Same non-zero stream

  // Submit both
  task_queue_submit(queue);

  // Wait and reap both completions
  task_completion_t comp1, comp2;
  EXPECT_TRUE(wait_for_completion(queue, &comp1));
  task_queue_remove_completion(queue);

  EXPECT_TRUE(wait_for_completion(queue, &comp2));
  task_queue_remove_completion(queue);

  // Even though Task 1 has a delay, because they are on the same serialized
  // stream, they MUST run in order: [1, 2]
  std::lock_guard<std::mutex> lock(order_mutex);
  EXPECT_EQ(execution_order.size(), 2U);
  EXPECT_EQ(execution_order[0], 1);
  EXPECT_EQ(execution_order[1], 2);

  task_queue_destroy(queue);
}

// Cascading Failure
TEST_P(task_queue_test, cascading_failure) {
  allocator_t alloc = allocator_get_default();
  task_queue_t* queue = task_queue_create(16, get_executor(), alloc);
  EXPECT_NE(queue, nullptr);

  std::atomic<int> run_count{0};

  auto failing_task = [](task_context_t* ctx) {
    task_set_failed(ctx);  // Signal failure!
  };

  auto dependent_task = [](task_context_t* ctx) {
    auto* counter = static_cast<std::atomic<int>*>(ctx->user_data);
    counter->fetch_add(1);
  };

  // Submit Task 1: Fails
  task_submission_t* sub1 = task_queue_get_submission(queue);
  sub1->task = failing_task;
  sub1->user_data = nullptr;
  sub1->stream = 7;  // Serialized stream

  // Submit Task 2: Should be aborted automatically due to Task 1 failure
  task_submission_t* sub2 = task_queue_get_submission(queue);
  sub2->task = dependent_task;
  sub2->user_data = &run_count;
  sub2->stream = 7;

  // Submit Task 3: Should also be aborted
  task_submission_t* sub3 = task_queue_get_submission(queue);
  sub3->task = dependent_task;
  sub3->user_data = &run_count;
  sub3->stream = 7;

  // Submit all
  task_queue_submit(queue);

  // 1. Reap Task 1: Should be FAILED
  task_completion_t comp1;
  EXPECT_TRUE(wait_for_completion(queue, &comp1));
  EXPECT_EQ(comp1.status, TASK_STATUS_FAILED);
  task_queue_remove_completion(queue);

  // 2. Reap Task 2: Should be CANCELLED (cascaded abort)
  task_completion_t comp2;
  EXPECT_TRUE(wait_for_completion(queue, &comp2));
  EXPECT_EQ(comp2.status, TASK_STATUS_CANCELLED);
  task_queue_remove_completion(queue);

  // 3. Reap Task 3: Should be CANCELLED (cascaded abort)
  task_completion_t comp3;
  EXPECT_TRUE(wait_for_completion(queue, &comp3));
  EXPECT_EQ(comp3.status, TASK_STATUS_CANCELLED);
  task_queue_remove_completion(queue);

  // Verify that the dependent tasks NEVER ran!
  EXPECT_EQ(run_count.load(), 0);

  task_queue_destroy(queue);
}

// Completion Peeking (Non-destructive Read)
TEST_P(task_queue_test, peek_completion) {
  allocator_t alloc = allocator_get_default();
  task_queue_t* queue = task_queue_create(16, get_executor(), alloc);
  EXPECT_NE(queue, nullptr);

  task_completion_t comp;
  // 1. Verify peeking an empty queue returns false
  EXPECT_FALSE(task_queue_peek_completion(queue, &comp));

  std::atomic<int> run_count{0};
  auto dummy_task = [](task_context_t* ctx) {
    auto* counter = static_cast<std::atomic<int>*>(ctx->user_data);
    counter->fetch_add(1);
  };

  // Submit and execute a single task
  task_submission_t* sub = task_queue_get_submission(queue);
  sub->task = dummy_task;
  sub->user_data = &run_count;
  sub->stream = 0;

  task_queue_submit(queue);

  // Wait for it to complete
  EXPECT_TRUE(wait_for_completion(queue, &comp));

  // 2. Peek the completion (must succeed and yield the correct completion data)
  task_completion_t peeked1;
  EXPECT_TRUE(task_queue_peek_completion(queue, &peeked1));
  EXPECT_EQ(peeked1.user_data, &run_count);
  EXPECT_EQ(peeked1.status, TASK_STATUS_OK);

  // 3. Peek again (must be non-destructive; should yield the exact same
  // completion!)
  task_completion_t peeked2;
  EXPECT_TRUE(task_queue_peek_completion(queue, &peeked2));
  EXPECT_EQ(peeked2.user_data, &run_count);
  EXPECT_EQ(peeked2.status, TASK_STATUS_OK);

  // 4. Remove the completion, and verify peek now returns false!
  task_queue_remove_completion(queue);
  EXPECT_FALSE(task_queue_peek_completion(queue, &comp));

  task_queue_destroy(queue);
}

// SQ Full Boundary Condition
TEST_P(task_queue_test, sq_full_boundary) {
  allocator_t alloc = allocator_get_default();
  // Create a queue of capacity 2
  task_queue_t* queue = task_queue_create(2, get_executor(), alloc);
  EXPECT_NE(queue, nullptr);

  auto dummy_task = [](task_context_t*) {};

  // 1. Get submission 1 (prepared = 1)
  task_submission_t* sub1 = task_queue_get_submission(queue);
  EXPECT_NE(sub1, nullptr);
  sub1->task = dummy_task;

  // 2. Get submission 2 (prepared = 2, SQ is now full!)
  task_submission_t* sub2 = task_queue_get_submission(queue);
  EXPECT_NE(sub2, nullptr);
  sub2->task = dummy_task;

  // 3. Try to get a 3rd submission (should fail and return nullptr since cap is
  // 2)
  task_submission_t* sub3 = task_queue_get_submission(queue);
  EXPECT_EQ(sub3, nullptr);

  // Submit both, reap them, and clean up
  task_queue_submit(queue);

  task_completion_t comp;
  EXPECT_TRUE(wait_for_completion(queue, &comp));
  task_queue_remove_completion(queue);
  EXPECT_TRUE(wait_for_completion(queue, &comp));
  task_queue_remove_completion(queue);

  task_queue_destroy(queue);
}

// Indefinite Blocking Wait (CPU-free)
TEST_P(task_queue_test, wait_completion_indefinite) {
  allocator_t alloc = allocator_get_default();
  task_queue_t* queue = task_queue_create(16, get_executor(), alloc);
  EXPECT_NE(queue, nullptr);

  std::atomic<bool> ran{false};
  auto task_fn = [](task_context_t* ctx) {
    auto* r = static_cast<std::atomic<bool>*>(ctx->user_data);
    r->store(true);
  };

  task_submission_t* sub = task_queue_get_submission(queue);
  sub->task = task_fn;
  sub->user_data = &ran;
  sub->stream = 0;

  task_queue_submit(queue);

  // Indefinite blocking wait (blocks the thread with 0% CPU until task
  // completes)
  task_completion_t comp;
  task_queue_wait_completion(queue, &comp);

  EXPECT_EQ(comp.user_data, &ran);
  EXPECT_EQ(comp.status, TASK_STATUS_OK);
  EXPECT_TRUE(ran.load());

  task_queue_remove_completion(queue);
  task_queue_destroy(queue);
}

// Verify that sequential safety is strictly preserved under rapid submission
// and potential race windows: a task in a serialized stream must never execute
// if its predecessor in the same stream has failed.
TEST_P(task_queue_test, cascading_failure_sequential_safety) {
  allocator_t alloc = allocator_get_default();

  // Helper RAII class to ensure queue is destroyed even if assertions fail
  struct scoped_queue {
    task_queue_t* q;
    ~scoped_queue() {
      if (q) task_queue_destroy(q);
    }
  };

  for (int run = 0; run < 1000; ++run) {
    task_queue_t* queue = task_queue_create(2, get_executor(), alloc);
    ASSERT_NE(queue, nullptr);
    scoped_queue sq{queue};

    std::atomic<bool> task2_ran{false};

    auto failing_task = [](task_context_t* ctx) { task_set_failed(ctx); };

    auto task2_fn = [](task_context_t* ctx) {
      auto* ran = static_cast<std::atomic<bool>*>(ctx->user_data);
      ran->store(true);
    };

    // 1. Submit Task 1 (Stream 1) - designed to fail
    task_submission_t* sub1 = task_queue_get_submission(queue);
    ASSERT_NE(sub1, nullptr);
    sub1->task = failing_task;
    sub1->user_data = nullptr;
    sub1->stream = 1;

    // 2. Submit Task 2 (Stream 1) - dependent task, should be aborted
    task_submission_t* sub2 = task_queue_get_submission(queue);
    ASSERT_NE(sub2, nullptr);
    sub2->task = task2_fn;
    sub2->user_data = &task2_ran;
    sub2->stream = 1;

    task_queue_submit(queue);

    // 3. Wait for Task 1 to complete (and fail)
    task_completion_t comp1;
    task_queue_wait_completion(queue, &comp1);
    ASSERT_EQ(comp1.status, TASK_STATUS_FAILED);

    // 4. IMMEDIATELY call submit to trigger dispatch_pending.
    // If the race condition exists, this might dispatch Task 2 before W1
    // cancels it.
    task_queue_submit(queue);

    task_queue_remove_completion(queue);

    // 5. Wait for Task 2 completion (should be cancelled)
    task_completion_t comp2;
    bool got_comp2 = wait_for_completion(queue, &comp2, 50.0);

    // Assert we got the completion, it was cancelled, and it never started
    // executing
    ASSERT_TRUE(got_comp2) << "Iteration " << run
                           << ": Timeout waiting for Task 2 completion";
    EXPECT_EQ(comp2.status, TASK_STATUS_CANCELLED)
        << "Iteration " << run << ": Task 2 was not cancelled!";
    EXPECT_FALSE(task2_ran.load())
        << "Iteration " << run
        << ": Subsequent task executed after predecessor failed!";

    task_queue_remove_completion(queue);
  }
}

// ─── Category 2: task_queue_concurrent_test (Requires Background Threads) ────

// Instant SQ Reuse
TEST(task_queue_concurrent_test, instant_sq_reuse) {
  allocator_t alloc = allocator_get_default();
  // Create a queue of capacity 1
  task_queue_t* queue = task_queue_create(1, thread_executor, alloc);
  EXPECT_NE(queue, nullptr);

  // Define a payload struct to pass state to the stateless lambda
  struct block_payload_t {
    std::atomic<bool> running{false};
    std::atomic<bool> proceed{false};
  } payload;

  auto blocking_task = [](task_context_t* ctx) {
    auto* p = static_cast<block_payload_t*>(ctx->user_data);
    p->running.store(true);
    // Spin until signaled to proceed
    while (!p->proceed.load()) {
      std::this_thread::yield();
    }
  };

  // Submit the blocking task (occupying the only SQ slot)
  task_submission_t* sub = task_queue_get_submission(queue);
  EXPECT_NE(sub, nullptr);
  sub->task = blocking_task;
  sub->user_data = &payload;
  sub->stream = 0;

  task_queue_submit(queue);

  // Wait until the background thread starts running it
  while (!payload.running.load()) {
    std::this_thread::yield();
  }

  // The task is actively running in the background.
  // Because SQ slots are decoupled, we should be able to instantly get the SQ
  // slot again!
  task_submission_t* second_sub = task_queue_get_submission(queue);
  EXPECT_NE(second_sub, nullptr);  // Should succeed immediately!

  // Signal the blocking task to finish
  payload.proceed.store(true);

  // Reap the completion
  task_completion_t comp;
  EXPECT_TRUE(wait_for_completion(queue, &comp));
  task_queue_remove_completion(queue);

  task_queue_destroy(queue);
}

// CQ Backpressure Blocking
TEST(task_queue_concurrent_test, cq_backpressure_blocking) {
  allocator_t alloc = allocator_get_default();
  // Create a queue of capacity 1
  task_queue_t* queue = task_queue_create(1, thread_executor, alloc);
  EXPECT_NE(queue, nullptr);

  std::atomic<int> run_count{0};
  auto task_fn = [](task_context_t* ctx) {
    auto* counter = static_cast<std::atomic<int>*>(ctx->user_data);
    counter->fetch_add(1);
  };

  // 1. Submit Task 1 (occupying the only slot in the CQ when finished)
  task_submission_t* sub1 = task_queue_get_submission(queue);
  EXPECT_NE(sub1, nullptr);
  sub1->task = task_fn;
  sub1->user_data = &run_count;
  sub1->stream = 0;

  task_queue_submit(queue);

  // Wait until Task 1 completes. The CQ is now full (1/1 slots).
  task_completion_t comp1;
  EXPECT_TRUE(wait_for_completion(queue, &comp1));
  // Note: We intentionally DO NOT call task_queue_remove_completion here!
  // The CQ remains 100% full!

  // 2. Submit Task 2. Since Task 1's slot was marked vacant, Task 2 can be
  // dispatched. However, when Task 2 finishes execution, its worker thread MUST
  // block because the CQ is full!
  task_submission_t* sub2 = task_queue_get_submission(queue);
  EXPECT_NE(sub2, nullptr);
  sub2->task = task_fn;
  sub2->user_data = &run_count;
  sub2->stream = 0;

  task_queue_submit(queue);

  // Determinisically wait until Task 2 has executed its body!
  // This completely eliminates flakiness under heavy CPU load!
  while (run_count.load() < 2) {
    std::this_thread::yield();
  }

  // Give the worker thread a tiny window to transition from task return to
  // blocking sleep
  std::this_thread::sleep_for(std::chrono::milliseconds(5));

  // Verify that Task 2 has executed (run_count should be 2),
  // but it is currently blocked waiting to write to the CQ.
  EXPECT_EQ(run_count.load(), 2);

  // Verify that the CQ still only contains Task 1's completion
  // (Task 2 hasn't overwritten or dropped anything, it is sleeping)
  EXPECT_EQ(comp1.user_data, &run_count);

  // 3. Now, reap Task 1 from the CQ. This makes space and MUST wake up Task 2's
  // worker!
  task_queue_remove_completion(queue);

  // Now, Task 2's worker should wake up, write its completion, and exit.
  // We should be able to reap Task 2's completion successfully!
  task_completion_t comp2;
  EXPECT_TRUE(wait_for_completion(queue, &comp2));
  EXPECT_EQ(comp2.user_data, &run_count);
  EXPECT_EQ(comp2.status, TASK_STATUS_OK);

  task_queue_remove_completion(queue);

  task_queue_destroy(queue);
}

// SQ Overflow Auto-Dispatch (Self-Regulation Verification)
TEST(task_queue_concurrent_test, sq_overflow_auto_dispatch) {
  allocator_t alloc = allocator_get_default();
  // Create a queue of capacity 2 (maximum 2 concurrent executions)
  task_queue_t* queue = task_queue_create(2, thread_executor, alloc);
  EXPECT_NE(queue, nullptr);

  // Coordinating handshake state
  struct handshake_payload_t {
    std::atomic<int> running_count{0};
    std::atomic<int> completed_count{0};
    std::atomic<bool> proceed{false};
  } payload;

  auto task_fn = [](task_context_t* ctx) {
    auto* p = static_cast<handshake_payload_t*>(ctx->user_data);

    // Phase 1: Signal that this task has started running
    p->running_count.fetch_add(1);

    // Phase 2: Spin-yield until the main thread releases us
    while (!p->proceed.load()) {
      std::this_thread::yield();
    }

    // Phase 3: Increment completion count on exit
    p->completed_count.fetch_add(1);
  };

  // 1. Submit Batch 1 (2 tasks, occupying the execution slots immediately)
  for (int i = 0; i < 2; ++i) {
    task_submission_t* sub = task_queue_get_submission(queue);
    EXPECT_NE(sub, nullptr);
    sub->task = task_fn;
    sub->user_data = &payload;
    sub->stream = 0;
  }
  task_queue_submit(queue);

  // Wait deterministically until both Batch 1 tasks are running and blocking
  // the slots!
  while (payload.running_count.load() < 2) {
    std::this_thread::yield();
  }

  // 2. Submit Batch 2 (Another 2 tasks. Since Batch 1 is running, these are
  // guaranteed to stay in the SQ!)
  for (int i = 0; i < 2; ++i) {
    task_submission_t* sub = task_queue_get_submission(queue);
    EXPECT_NE(sub, nullptr);  // Succeeds because submit() cleared the SQ slots!
    sub->task = task_fn;
    sub->user_data = &payload;
    sub->stream = 0;
  }
  task_queue_submit(queue);

  // 3. Release the Batch 1 block, allowing them to complete and trigger
  // auto-dispatch!
  payload.proceed.store(true);

  // When Batch 1 completes, their worker threads must automatically
  // dispatch Batch 2. We should reap all 4 completions successfully, without
  // calling submit again!
  for (int i = 0; i < 4; ++i) {
    task_completion_t comp;
    EXPECT_TRUE(wait_for_completion(queue, &comp));
    EXPECT_EQ(comp.status, TASK_STATUS_OK);
    EXPECT_EQ(comp.user_data, &payload);
    task_queue_remove_completion(queue);
  }

  // Verify all 4 ran and completed
  EXPECT_EQ(payload.completed_count.load(), 4);

  task_queue_destroy(queue);
}

// Timed Blocking Wait Expiration (CPU-free timeout)
TEST(task_queue_concurrent_test, wait_completion_timeout_expire) {
  allocator_t alloc = allocator_get_default();
  task_queue_t* queue = task_queue_create(16, thread_executor, alloc);
  EXPECT_NE(queue, nullptr);

  task_completion_t comp;
  // Wait for 5ms (5,000,000 ns). Since no task was submitted, this MUST timeout
  // and return false!
  bool success = task_queue_wait_completion_timeout(queue, &comp, 5000000ULL);
  EXPECT_FALSE(success);  // Must return false indicating timeout

  task_queue_destroy(queue);
}

// Verify that the persistent worker loop reduces executor dispatches by reusing
// threads in-place
TEST(task_queue_concurrent_test, executor_dispatch_reduction) {
  allocator_t alloc = allocator_get_default();

  // Reset the global mock dispatch counter
  g_mock_dispatch_count.store(0);

  // Create a queue of capacity 2 using the mock dispatch executor
  task_queue_t* queue = task_queue_create(2, mock_dispatch_executor, alloc);
  EXPECT_NE(queue, nullptr);

  // Coordinating handshake state
  struct handshake_payload_t {
    std::atomic<int> running_count{0};
    std::atomic<int> completed_count{0};
    std::atomic<bool> proceed{false};
  } payload;

  auto task_fn = [](task_context_t* ctx) {
    auto* p = static_cast<handshake_payload_t*>(ctx->user_data);
    p->running_count.fetch_add(1);
    while (!p->proceed.load()) {
      std::this_thread::yield();
    }
    p->completed_count.fetch_add(1);
  };

  // 1. Submit Batch 1 (2 tasks, occupying the execution slots immediately)
  for (int i = 0; i < 2; ++i) {
    task_submission_t* sub = task_queue_get_submission(queue);
    EXPECT_NE(sub, nullptr);
    sub->task = task_fn;
    sub->user_data = &payload;
    sub->stream = 0;
  }
  task_queue_submit(queue);

  // Wait deterministically until both Batch 1 tasks are running and blocking
  while (payload.running_count.load() < 2) {
    std::this_thread::yield();
  }

  // Verify that the executor was called exactly 2 times (once per running task)
  EXPECT_EQ(g_mock_dispatch_count.load(), 2);

  // 2. Submit Batch 2 (Another 2 tasks. These must stage in the SQ due to
  // overflow)
  for (int i = 0; i < 2; ++i) {
    task_submission_t* sub = task_queue_get_submission(queue);
    EXPECT_NE(sub, nullptr);
    sub->task = task_fn;
    sub->user_data = &payload;
    sub->stream = 0;
  }
  task_queue_submit(queue);

  // 3. Release the Batch 1 block, allowing them to exit and in-place reuse
  // threads for Batch 2!
  payload.proceed.store(true);

  // Reap all 4 completions
  for (int i = 0; i < 4; ++i) {
    task_completion_t comp;
    EXPECT_TRUE(wait_for_completion(queue, &comp));
    task_queue_remove_completion(queue);
  }

  // Verify all completed
  EXPECT_EQ(payload.completed_count.load(), 4);

  // 4. THE CRITICAL VERIFICATION:
  // Even though we executed 4 tasks in total, the executor MUST have been
  // called exactly 2 times! Tasks 3 & 4 were executed in-place by the looping
  // worker threads without ever making any additional dispatches to the
  // platform thread pool!
  EXPECT_EQ(g_mock_dispatch_count.load(), 2);

  task_queue_destroy(queue);
}

// Verify that the scheduler prioritizes tasks with stream affinity over older
// FIFO tasks
TEST(task_queue_concurrent_test, stream_affinity_prioritization) {
  allocator_t alloc = allocator_get_default();

  // Create a queue of capacity 2 (enables 2 parallel slots, leaving space in
  // the SQ)
  task_queue_t* queue = task_queue_create(2, thread_executor, alloc);
  EXPECT_NE(queue, nullptr);

  struct affinity_payload_t {
    std::vector<int> execution_order;
    std::mutex mutex;
    std::atomic<bool> task1_proceed{false};
    std::atomic<bool> task1_running{false};
    std::atomic<bool> task1b_proceed{false};
    std::atomic<bool> task1b_running{false};
  } payload;

  // Task 1 (Stream 1): Blocks until signaled, then appends 1
  auto task1_fn = [](task_context_t* ctx) {
    auto* p = static_cast<affinity_payload_t*>(ctx->user_data);
    p->task1_running.store(true);
    while (!p->task1_proceed.load()) {
      std::this_thread::yield();
    }
    std::lock_guard<std::mutex> lock(p->mutex);
    p->execution_order.push_back(1);
  };

  // Task 1b (Stream 3): Blocks until signaled, does not append (just occupies
  // Slot 2)
  auto task1b_fn = [](task_context_t* ctx) {
    auto* p = static_cast<affinity_payload_t*>(ctx->user_data);
    p->task1b_running.store(true);
    while (!p->task1b_proceed.load()) {
      std::this_thread::yield();
    }
  };

  // Task 2 (Stream 2): Appends 2
  auto task2_fn = [](task_context_t* ctx) {
    auto* p = static_cast<affinity_payload_t*>(ctx->user_data);
    std::lock_guard<std::mutex> lock(p->mutex);
    p->execution_order.push_back(2);
  };

  // Task 3 (Stream 1): Appends 3
  auto task3_fn = [](task_context_t* ctx) {
    auto* p = static_cast<affinity_payload_t*>(ctx->user_data);
    std::lock_guard<std::mutex> lock(p->mutex);
    p->execution_order.push_back(3);
  };

  // 1. Submit Task 1 (Stream 1) and Task 1b (Stream 3) to occupy both execution
  // slots (2/2)
  task_submission_t* sub1 = task_queue_get_submission(queue);
  EXPECT_NE(sub1, nullptr);
  sub1->task = task1_fn;
  sub1->user_data = &payload;
  sub1->stream = 1;

  task_submission_t* sub1b = task_queue_get_submission(queue);
  EXPECT_NE(sub1b, nullptr);
  sub1b->task = task1b_fn;
  sub1b->user_data = &payload;
  sub1b->stream = 3;

  task_queue_submit(queue);

  // Wait until both initial tasks are actively running in the background
  while (!payload.task1_running.load() || !payload.task1b_running.load()) {
    std::this_thread::yield();
  }

  // 2. Submit Task 2 on Stream 2 (FIFO oldest in SQ)
  task_submission_t* sub2 = task_queue_get_submission(queue);
  EXPECT_NE(sub2, nullptr);  // Succeeds because the SQ is empty!
  sub2->task = task2_fn;
  sub2->user_data = &payload;
  sub2->stream = 2;
  task_queue_submit(queue);

  // 3. Submit Task 3 on Stream 1 (Stream Affinity match, younger than Task 2)
  task_submission_t* sub3 = task_queue_get_submission(queue);
  EXPECT_NE(
      sub3,
      nullptr);  // Succeeds because SQ capacity is 2 and we only have 1 staged!
  sub3->task = task3_fn;
  sub3->user_data = &payload;
  sub3->stream = 1;
  task_queue_submit(queue);

  // 4. Release Task 1 first, keeping Task 1b blocked.
  // Thread A (running Task 1) will complete, scan the SQ under stream affinity,
  // prioritize Task 3 (Stream 1) over the older Task 2 (Stream 2), and execute
  // it in-place.
  payload.task1_proceed.store(true);

  // Wait until both Task 1 and Task 3 are finished (execution order should have
  // 1 and 3) This ensures deterministic execution order before releasing the
  // final block!
  while (true) {
    std::lock_guard<std::mutex> lock(payload.mutex);
    if (payload.execution_order.size() >= 2) break;
    std::this_thread::yield();
  }

  // 5. Release Task 1b
  payload.task1b_proceed.store(true);

  // Reap all 4 completions (Task 1, Task 1b, Task 2, Task 3)
  for (int i = 0; i < 4; ++i) {
    task_completion_t comp;
    EXPECT_TRUE(wait_for_completion(queue, &comp));
    task_queue_remove_completion(queue);
  }

  // Verify the execution order!
  // The order MUST be: [1, 3, 2]
  // (Task 3 was executed immediately after Task 1 due to Stream Affinity,
  // skipping Task 2!)
  std::lock_guard<std::mutex> lock(payload.mutex);
  EXPECT_EQ(payload.execution_order.size(), 3U);
  EXPECT_EQ(payload.execution_order[0], 1);
  EXPECT_EQ(payload.execution_order[1], 3);
  EXPECT_EQ(payload.execution_order[2], 2);

  task_queue_destroy(queue);
}

// Verify that a blocked serialized stream does not prevent independent streams
// from executing when there are idle workers (no Head-of-Line blocking).
TEST(task_queue_concurrent_test, no_head_of_line_blocking) {
  allocator_t alloc = allocator_get_default();

  // Capacity 2: allows 2 parallel tasks
  task_queue_t* queue = task_queue_create(2, thread_executor, alloc);
  ASSERT_NE(queue, nullptr);

  struct hol_payload_t {
    std::atomic<bool> task1_1_running{false};
    std::atomic<bool> task1_1_proceed{false};
    std::atomic<bool> task2_1_ran{false};
  } payload;

  // RAII cleanup to ensure we never leave background threads hanging or queues
  // leaked even if assertions fail and abort the test.
  struct scoped_cleanup {
    task_queue_t* q;
    hol_payload_t* p;
    ~scoped_cleanup() {
      if (p) p->task1_1_proceed.store(true);
      if (q) task_queue_destroy(q);
    }
  };
  scoped_cleanup cleanup{queue, &payload};

  // Task 1.1 (Stream 1): Blocks until signaled
  auto task1_1_fn = [](task_context_t* ctx) {
    auto* p = static_cast<hol_payload_t*>(ctx->user_data);
    p->task1_1_running.store(true);
    while (!p->task1_1_proceed.load()) {
      std::this_thread::yield();
    }
  };

  // Task 1.2 (Stream 1): Dummy task
  auto task1_2_fn = [](task_context_t*) {};

  // Task 2.1 (Stream 2): Normal task on independent stream
  auto task2_1_fn = [](task_context_t* ctx) {
    auto* p = static_cast<hol_payload_t*>(ctx->user_data);
    p->task2_1_ran.store(true);
  };

  // 1. Submit Task 1.1 (Stream 1)
  task_submission_t* sub1_1 = task_queue_get_submission(queue);
  ASSERT_NE(sub1_1, nullptr);
  sub1_1->task = task1_1_fn;
  sub1_1->user_data = &payload;
  sub1_1->stream = 1;
  task_queue_submit(queue);

  // Wait until Task 1.1 is running
  while (!payload.task1_1_running.load()) {
    std::this_thread::yield();
  }

  // 2. Submit Task 1.2 (Stream 1) -> stages in SQ
  task_submission_t* sub1_2 = task_queue_get_submission(queue);
  ASSERT_NE(sub1_2, nullptr);
  sub1_2->task = task1_2_fn;
  sub1_2->user_data = nullptr;
  sub1_2->stream = 1;

  // 3. Submit Task 2.1 (Stream 2) -> stages in SQ
  task_submission_t* sub2_1 = task_queue_get_submission(queue);
  ASSERT_NE(sub2_1, nullptr);
  sub2_1->task = task2_1_fn;
  sub2_1->user_data = &payload;
  sub2_1->stream = 2;

  // Submit both Task 1.2 and Task 2.1
  task_queue_submit(queue);

  // Give it some time to see if Task 2.1 gets dispatched and runs.
  // Since Stream 2 is idle and we have a free slot, it SHOULD run immediately.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Verify if Task 2.1 ran
  bool ran = payload.task2_1_ran.load();

  // Signal Task 1.1 to proceed so we can clean up safely (also handled by RAII
  // as fallback)
  payload.task1_1_proceed.store(true);

  // Reap all 3 completions to ensure threads exit before destruction.
  // Verify that all tasks actually finished successfully.
  for (int i = 0; i < 3; ++i) {
    task_completion_t comp;
    ASSERT_TRUE(wait_for_completion(queue, &comp, 500.0))
        << "Timeout waiting for completion " << i;
    EXPECT_EQ(comp.status, TASK_STATUS_OK)
        << "Task completion " << i << " did not succeed!";
    task_queue_remove_completion(queue);
  }

  EXPECT_TRUE(ran) << "Head-of-Line blocking detected! Task on idle Stream 2 "
                      "was blocked by pending task on active Stream 1.";
}

// Verify that cancelling a pending task while the CQ is completely full
// does not silently drop the completion event (resolving lost completions).
TEST(task_queue_concurrent_test, lost_completion_on_cancellation_when_cq_full) {
  allocator_t alloc = allocator_get_default();

  // Capacity 2: allows 2 parallel tasks, CQ size 2
  task_queue_t* queue = task_queue_create(2, thread_executor, alloc);
  ASSERT_NE(queue, nullptr);

  struct test_payload_t {
    std::atomic<bool> task1_done{false};
    std::atomic<bool> task2_done{false};

    std::atomic<bool> gate{false};
    std::atomic<bool> task3_running{false};
    std::atomic<bool> task4_running{false};
  } payload;

  // RAII cleanup
  struct scoped_cleanup {
    task_queue_t* q;
    test_payload_t* p;
    ~scoped_cleanup() {
      if (p) p->gate.store(true);
      if (q) task_queue_destroy(q);
    }
  };
  scoped_cleanup cleanup{queue, &payload};

  // Task 1 and 2: Quick tasks that complete immediately to fill the CQ
  auto quick_task1 = [](task_context_t* ctx) {
    auto* p = static_cast<test_payload_t*>(ctx->user_data);
    p->task1_done.store(true);
  };
  auto quick_task2 = [](task_context_t* ctx) {
    auto* p = static_cast<test_payload_t*>(ctx->user_data);
    p->task2_done.store(true);
  };

  // Task 3 and 4: Tasks that occupy the execution slots and block
  auto blocking_task3 = [](task_context_t* ctx) {
    auto* p = static_cast<test_payload_t*>(ctx->user_data);
    p->task3_running.store(true);
    while (!p->gate.load()) {
      std::this_thread::yield();
    }
  };
  auto blocking_task4 = [](task_context_t* ctx) {
    auto* p = static_cast<test_payload_t*>(ctx->user_data);
    p->task4_running.store(true);
    while (!p->gate.load()) {
      std::this_thread::yield();
    }
  };

  // 1. Submit Task 1 and 2
  task_submission_t* sub1 = task_queue_get_submission(queue);
  ASSERT_NE(sub1, nullptr);
  sub1->task = quick_task1;
  sub1->user_data = &payload;
  sub1->stream = 0;

  task_submission_t* sub2 = task_queue_get_submission(queue);
  ASSERT_NE(sub2, nullptr);
  sub2->task = quick_task2;
  sub2->user_data = &payload;
  sub2->stream = 0;

  task_queue_submit(queue);

  // Wait for them to finish
  while (!payload.task1_done.load() || !payload.task2_done.load()) {
    std::this_thread::yield();
  }
  // Give a tiny window for completions to be posted to CQ
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  // CQ is now full (2/2 completions). Executions are vacant.

  // 2. Submit Task 3 and 4 to occupy the execution slots
  task_submission_t* sub3 = task_queue_get_submission(queue);
  ASSERT_NE(sub3, nullptr);
  sub3->task = blocking_task3;
  sub3->user_data = &payload;
  sub3->stream = 0;

  task_submission_t* sub4 = task_queue_get_submission(queue);
  ASSERT_NE(sub4, nullptr);
  sub4->task = blocking_task4;
  sub4->user_data = &payload;
  sub4->stream = 0;

  task_queue_submit(queue);

  // Wait until they are running (occupying the 2 execution slots)
  while (!payload.task3_running.load() || !payload.task4_running.load()) {
    std::this_thread::yield();
  }

  // CQ is full, Executions are full.

  // 3. Submit Task 5 (must stage in pending list because executions are full)
  int task5_marker = 5;
  task_submission_t* sub5 = task_queue_get_submission(queue);
  ASSERT_NE(sub5, nullptr);
  sub5->task = [](task_context_t*) {};
  sub5->user_data = &task5_marker;
  sub5->stream = 0;

  task_queue_submit(queue);

  // 4. Cancel Task 5 while it is pending and CQ is full
  task_queue_cancel_submission(queue, &task5_marker);

  // 5. Open the gate to let Task 3 and 4 finish
  payload.gate.store(true);

  // 6. Reap all completions. We expect 5 completions:
  // - Task 1 (OK)
  // - Task 2 (OK)
  // - Task 5 (CANCELLED)
  // - Task 3 (OK)
  // - Task 4 (OK)
  // If the bug is present, Task 5's completion was lost, and we will only get 4
  // completions.
  int reaped_count = 0;
  bool got_task5_completion = false;

  for (int i = 0; i < 5; ++i) {
    task_completion_t comp;
    // Use a decent timeout to allow workers to wake up and post after we reap
    if (wait_for_completion(queue, &comp, 500.0)) {
      reaped_count++;
      if (comp.user_data == &task5_marker) {
        got_task5_completion = true;
        EXPECT_EQ(comp.status, TASK_STATUS_CANCELLED);
      }
      task_queue_remove_completion(queue);
    } else {
      // Timeout
      break;
    }
  }

  EXPECT_EQ(reaped_count, 5)
      << "Lost completions detected! Only reaped " << reaped_count << " tasks.";
  EXPECT_TRUE(got_task5_completion) << "Task 5 completion was lost!";
}

// ─── Category 3: task_queue_inline_test (Requires Synchronous Executor) ─────

// Synchronous Inline Executor (Deadlock-Free Verification)
TEST(task_queue_inline_test, inline_executor_synchronous) {
  allocator_t alloc = allocator_get_default();

  // Create the queue using the inline executor
  task_queue_t* queue = task_queue_create(16, inline_executor, alloc);
  EXPECT_NE(queue, nullptr);

  std::vector<int> execution_order;

  auto task_fn = [](task_context_t* ctx) {
    auto* order = static_cast<std::vector<int>*>(ctx->user_data);
    order->push_back(1);
  };

  task_submission_t* sub = task_queue_get_submission(queue);
  EXPECT_NE(sub, nullptr);
  sub->task = task_fn;
  sub->user_data = &execution_order;
  sub->stream = 0;

  // Submit. Since the executor is synchronous, the task MUST execute and write
  // its completion immediately *inside* the submit call, without deadlocking!
  task_queue_submit(queue);

  // The task should have completed immediately. Let's peek and verify!
  task_completion_t comp;
  EXPECT_TRUE(task_queue_peek_completion(queue, &comp));
  EXPECT_EQ(comp.user_data, &execution_order);
  EXPECT_EQ(comp.status, TASK_STATUS_OK);

  task_queue_remove_completion(queue);

  // Verify it executed correctly
  EXPECT_EQ(execution_order.size(), 1U);
  EXPECT_EQ(execution_order[0], 1);

  task_queue_destroy(queue);
}

// Verify that cancelling a task that has already finished executing its body
// but is blocked waiting to post its completion (due to CQ backpressure)
// does NOT retroactively mark it as CANCELLED. It should complete as OK.
TEST(task_queue_concurrent_test,
     cancel_completed_task_blocked_on_cq_fails_to_cancel) {
  allocator_t alloc = allocator_get_default();
  // Capacity 1: CQ size 1, maximum 1 execution slot
  task_queue_t* queue = task_queue_create(1, thread_executor, alloc);
  ASSERT_NE(queue, nullptr);

  std::atomic<bool> task1_ran{false};
  std::atomic<bool> task2_body_ran{false};

  auto quick_task = [](task_context_t* ctx) {
    auto* ran = static_cast<std::atomic<bool>*>(ctx->user_data);
    ran->store(true);
  };

  auto blocking_task = [](task_context_t* ctx) {
    auto* ran = static_cast<std::atomic<bool>*>(ctx->user_data);
    ran->store(true);
  };

  // 1. Submit Task 1 (occupying the only slot in the CQ when finished)
  task_submission_t* sub1 = task_queue_get_submission(queue);
  ASSERT_NE(sub1, nullptr);
  sub1->task = quick_task;
  sub1->user_data = &task1_ran;
  sub1->stream = 0;

  task_queue_submit(queue);

  // Wait until Task 1 completes and occupies the CQ
  while (!task1_ran.load()) {
    std::this_thread::yield();
  }
  // Give a tiny window for completion to be posted to CQ
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  // CQ is now full (1/1 slots).

  // 2. Submit Task 2. It will run (since executions are vacant),
  // but will block when trying to post completion because CQ is full.
  task_submission_t* sub2 = task_queue_get_submission(queue);
  ASSERT_NE(sub2, nullptr);
  sub2->task = blocking_task;
  sub2->user_data = &task2_body_ran;
  sub2->stream = 0;

  task_queue_submit(queue);

  // Wait until Task 2 has executed its body
  while (!task2_body_ran.load()) {
    std::this_thread::yield();
  }
  // Give a tiny window to ensure the worker thread is blocked on cond_space
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  // Task 2 has fully executed its body, but is blocked waiting for CQ space.
  // 3. Attempt to cancel Task 2.
  task_queue_cancel_submission(queue, &task2_body_ran);

  // 4. Reap Task 1's completion to make space in the CQ
  task_completion_t comp1;
  EXPECT_TRUE(task_queue_peek_completion(queue, &comp1));
  EXPECT_EQ(comp1.user_data, &task1_ran);
  task_queue_remove_completion(queue);

  // 5. Task 2's worker should wake up, post its completion, and exit.
  // We reap Task 2's completion and assert that its status is TASK_STATUS_OK,
  // NOT TASK_STATUS_CANCELLED, because it had already finished executing before
  // the cancel.
  task_completion_t comp2;
  EXPECT_TRUE(wait_for_completion(queue, &comp2));
  EXPECT_EQ(comp2.user_data, &task2_body_ran);

  // THIS IS THE CRITICAL ASSERTION THAT WE EXPECT TO FAIL
  EXPECT_EQ(comp2.status, TASK_STATUS_OK)
      << "Task was retroactively marked as CANCELLED even though it ran to "
         "completion!";

  task_queue_remove_completion(queue);
  task_queue_destroy(queue);
}

// Verify that an independent stream can still make progress after the internal
// node pool is temporarily exhausted by another stream, once resources are
// freed.
TEST(task_queue_concurrent_test,
     independent_stream_progress_under_node_pressure) {
  allocator_t alloc = allocator_get_default();
  const size_t cap = 4;
  task_queue_t* queue = task_queue_create(cap, thread_executor, alloc);
  ASSERT_NE(queue, nullptr);

  struct block_payload {
    std::atomic<bool> started{false};
    std::atomic<bool> block{true};
  } stream1_payload;

  std::atomic<int> stream1_ran{0};
  std::atomic<int> stream2_ran{0};

  auto blocking_task = [](task_context_t* ctx) {
    auto* p = static_cast<block_payload*>(ctx->user_data);
    p->started.store(true);
    while (p->block.load()) {
      std::this_thread::yield();
    }
  };

  auto count_task = [](task_context_t* ctx) {
    auto* counter = static_cast<std::atomic<int>*>(ctx->user_data);
    counter->fetch_add(1);
  };

  // 1. Submit Stream 1 tasks to occupy the running slot and fill the pending
  // list
  {
    task_submission_t* sub = task_queue_get_submission(queue);
    ASSERT_NE(sub, nullptr);
    sub->task = blocking_task;
    sub->user_data = &stream1_payload;
    sub->stream = 1;
  }
  for (int i = 0; i < 3; ++i) {
    task_submission_t* sub = task_queue_get_submission(queue);
    ASSERT_NE(sub, nullptr);
    sub->task = count_task;
    sub->user_data = &stream1_ran;
    sub->stream = 1;
  }
  task_queue_submit(queue);

  // Wait for the blocking task to start
  while (!stream1_payload.started.load()) {
    std::this_thread::yield();
  }

  // 2. Submit more Stream 1 tasks to completely exhaust the node pool.
  // One will occupy the last free node in the pending list; others remain in
  // SQ.
  for (int i = 0; i < 4; ++i) {
    task_submission_t* sub = task_queue_get_submission(queue);
    ASSERT_NE(sub, nullptr);
    sub->task = count_task;
    sub->user_data = &stream1_ran;
    sub->stream = 1;
  }
  task_queue_submit(queue);

  // 3. Submit a task on an independent Stream 2.
  // Due to node pool exhaustion, this task is temporarily deferred in the SQ.
  task_submission_t* sub_b1 = task_queue_get_submission(queue);
  ASSERT_NE(sub_b1, nullptr);
  sub_b1->task = count_task;
  sub_b1->user_data = &stream2_ran;
  sub_b1->stream = 2;
  task_queue_submit(queue);

  // Verify Stream 2 task has not run yet (blocked by node exhaustion)
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_EQ(stream2_ran.load(), 0);

  // 4. Unblock Stream 1. As its tasks complete, they free nodes.
  // The engine must automatically recover and execute the deferred tasks
  // (including the Stream 2 task) without requiring another manual submit().
  stream1_payload.block.store(false);

  // We expect a total of 9 completions: 8 from Stream 1, 1 from Stream 2.
  int completions_reaped = 0;
  task_completion_t comp;
  for (int i = 0; i < 9; ++i) {
    if (wait_for_completion(queue, &comp, 200.0)) {
      completions_reaped++;
      task_queue_remove_completion(queue);
    }
  }
  EXPECT_EQ(completions_reaped, 9);
  EXPECT_EQ(stream1_ran.load(), 7);  // Tasks 2 to 8 ran
  EXPECT_EQ(stream2_ran.load(), 1);  // Stream 2 task ran

  task_queue_destroy(queue);
}

// Stress test to expose data races between main thread preparing submissions
// and background/other threads triggering stream cancellation.
TEST(task_queue_concurrent_test, cancellation_race_stress) {
  allocator_t alloc = allocator_get_default();
  task_queue_t* queue = task_queue_create(8, thread_executor, alloc);
  ASSERT_NE(queue, nullptr);

  std::atomic<bool> stop{false};

  // Thread to constantly trigger cancellation on Stream 1
  std::thread cancel_thread([queue, &stop]() {
    while (!stop.load()) {
      task_queue_cancel_stream(queue, 1);
      std::this_thread::yield();
    }
  });

  auto dummy_task = [](task_context_t*) {};

  // Main thread constantly prepares and submits tasks on Stream 1
  auto start_time = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - start_time <
         std::chrono::milliseconds(100)) {
    task_submission_t* sub = task_queue_get_submission(queue);
    if (sub) {
      // Race window: these writes are outside the lock, while cancel_thread
      // is reading/writing them under lock (but accessing the same memory).
      sub->stream = 1;
      sub->task = dummy_task;
      sub->user_data = nullptr;
      task_queue_submit(queue);
    }

    // Reap completions to avoid queue full
    task_completion_t comp;
    while (task_queue_peek_completion(queue, &comp)) {
      task_queue_remove_completion(queue);
    }
  }

  stop.store(true);
  cancel_thread.join();

  // Clean up remaining completions
  task_completion_t comp;
  while (wait_for_completion(queue, &comp, 10.0)) {
    task_queue_remove_completion(queue);
  }

  task_queue_destroy(queue);
}

// Cascading Cancellation in a concurrent environment.
// We use a blocking task to hold the serialized stream, submit the tasks to be
// cancelled, trigger cancellation, and then release the blocker.
struct BlockerContext {
  std::mutex mutex;
  std::condition_variable cv;
  bool released = false;
};

TEST(task_queue_concurrent_test, cascading_cancellation) {
  allocator_t alloc = allocator_get_default();
  task_queue_t* queue = task_queue_create(16, thread_executor, alloc);
  ASSERT_NE(queue, nullptr);

  BlockerContext blocker;
  auto blocking_task = [](task_context_t* ctx) {
    auto* bc = static_cast<BlockerContext*>(ctx->user_data);
    std::unique_lock<std::mutex> lock(bc->mutex);
    bc->cv.wait(lock, [bc] { return bc->released; });
  };

  std::atomic<int> run_count{0};
  auto dummy_task = [](task_context_t* ctx) {
    auto* counter = static_cast<std::atomic<int>*>(ctx->user_data);
    counter->fetch_add(1);
  };

  // 1. Submit blocking task on Stream 99
  task_submission_t* sub_block = task_queue_get_submission(queue);
  ASSERT_NE(sub_block, nullptr);
  sub_block->task = blocking_task;
  sub_block->user_data = &blocker;
  sub_block->stream = 99;

  // 2. Submit 3 tasks on the same Stream 99
  for (int i = 0; i < 3; ++i) {
    task_submission_t* sub = task_queue_get_submission(queue);
    ASSERT_NE(sub, nullptr);
    sub->task = dummy_task;
    sub->user_data = &run_count;
    sub->stream = 99;
  }

  // 3. Submit the batch. The blocker starts executing, others are queued in
  // pending list
  task_queue_submit(queue);

  // 4. Cancel the entire stream! This should cancel the active blocker and the
  // 3 pending tasks
  task_queue_cancel_stream(queue, 99);

  // 5. Release the blocker so it can wake up and complete (as cancelled)
  {
    std::unique_lock<std::mutex> lock(blocker.mutex);
    blocker.released = true;
  }
  blocker.cv.notify_all();

  // 6. Reap blocker: Should be CANCELLED
  task_completion_t comp_block;
  EXPECT_TRUE(wait_for_completion(queue, &comp_block));
  EXPECT_EQ(comp_block.status, TASK_STATUS_CANCELLED);
  task_queue_remove_completion(queue);

  // 7. Reap the 3 dummy tasks: All should be CANCELLED
  for (int i = 0; i < 3; ++i) {
    task_completion_t comp;
    EXPECT_TRUE(wait_for_completion(queue, &comp));
    EXPECT_EQ(comp.status, TASK_STATUS_CANCELLED);
    task_queue_remove_completion(queue);
  }

  // None of the dummy tasks should have executed
  EXPECT_EQ(run_count.load(), 0);

  task_queue_destroy(queue);
}

// Individual Submission Cancellation in a concurrent environment.
// We cancel a specific pending task in a serialized stream, which should
// trigger a cascading cancellation of the entire stream (including the
// active blocker and other pending tasks).
TEST(task_queue_concurrent_test, cancel_submission) {
  allocator_t alloc = allocator_get_default();
  task_queue_t* queue = task_queue_create(16, thread_executor, alloc);
  ASSERT_NE(queue, nullptr);

  BlockerContext blocker;
  auto blocking_task = [](task_context_t* ctx) {
    auto* bc = static_cast<BlockerContext*>(ctx->user_data);
    std::unique_lock<std::mutex> lock(bc->mutex);
    bc->cv.wait(lock, [bc] { return bc->released; });
  };

  std::atomic<int> run_count{0};
  auto dummy_task = [](task_context_t* ctx) {
    auto* counter = static_cast<std::atomic<int>*>(ctx->user_data);
    counter->fetch_add(1);
  };

  // 1. Submit blocker on Stream 5
  task_submission_t* sub_block = task_queue_get_submission(queue);
  ASSERT_NE(sub_block, nullptr);
  sub_block->task = blocking_task;
  sub_block->user_data = &blocker;
  sub_block->stream = 5;

  // 2. Submit Task 1 on Stream 5
  task_submission_t* sub1 = task_queue_get_submission(queue);
  ASSERT_NE(sub1, nullptr);
  sub1->task = dummy_task;
  sub1->user_data = &run_count;
  sub1->stream = 5;

  // 3. Submit Task 2 on Stream 5 (dependent)
  task_submission_t* sub2 = task_queue_get_submission(queue);
  ASSERT_NE(sub2, nullptr);
  sub2->task = dummy_task;
  sub2->user_data = nullptr;
  sub2->stream = 5;

  // 4. Submit the batch. Blocker runs, Task 1 and 2 are pending
  task_queue_submit(queue);

  // 5. Cancel Task 1 specifically by its user_data.
  // This must cancel Task 1, and since Stream 5 is serialized, cascade to
  // the blocker and Task 2.
  task_queue_cancel_submission(queue, &run_count);

  // 6. Release the blocker so it can wake up and complete (as cancelled)
  {
    std::unique_lock<std::mutex> lock(blocker.mutex);
    blocker.released = true;
  }
  blocker.cv.notify_all();

  // 7. Reap blocker: Should be CANCELLED (due to cascading cancellation
  // aborting the active task of the stream)
  task_completion_t comp_block;
  EXPECT_TRUE(wait_for_completion(queue, &comp_block));
  EXPECT_EQ(comp_block.status, TASK_STATUS_CANCELLED);
  task_queue_remove_completion(queue);

  // 8. Reap Task 1: Should be CANCELLED
  task_completion_t comp1;
  EXPECT_TRUE(wait_for_completion(queue, &comp1));
  EXPECT_EQ(comp1.status, TASK_STATUS_CANCELLED);
  EXPECT_EQ(comp1.user_data, &run_count);
  task_queue_remove_completion(queue);

  // 9. Reap Task 2: Should be CANCELLED (cascaded)
  task_completion_t comp2;
  EXPECT_TRUE(wait_for_completion(queue, &comp2));
  EXPECT_EQ(comp2.status, TASK_STATUS_CANCELLED);
  EXPECT_EQ(comp2.user_data, nullptr);
  task_queue_remove_completion(queue);

  // None of the dummy tasks should have executed
  EXPECT_EQ(run_count.load(), 0);

  task_queue_destroy(queue);
}

// Test that a running task can cooperatively detect cancellation using
// task_should_abort()
TEST(task_queue_concurrent_test, cooperative_cancellation_polling) {
  allocator_t alloc = allocator_get_default();
  task_queue_t* queue = task_queue_create(8, thread_executor, alloc);
  ASSERT_NE(queue, nullptr);

  struct PollContext {
    std::mutex mutex;
    std::condition_variable cv;
    bool started = false;
    std::atomic<bool> detected_abort{false};
  } pc;

  auto polling_task = [](task_context_t* ctx) {
    auto* context = static_cast<PollContext*>(ctx->user_data);

    // 1. Signal that the task has started executing
    {
      std::unique_lock<std::mutex> lock(context->mutex);
      context->started = true;
    }
    context->cv.notify_all();

    // 2. Poll task_should_abort() until it transitions to true
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
      if (task_should_abort(ctx)) {
        context->detected_abort.store(true);
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  };

  // Submit the polling task on Stream 42
  task_submission_t* sub = task_queue_get_submission(queue);
  ASSERT_NE(sub, nullptr);
  sub->task = polling_task;
  sub->user_data = &pc;
  sub->stream = 42;

  task_queue_submit(queue);

  // Wait for the task to start executing
  {
    std::unique_lock<std::mutex> lock(pc.mutex);
    pc.cv.wait(lock, [&] { return pc.started; });
  }

  // Task is running and polling. Trigger cancellation!
  task_queue_cancel_stream(queue, 42);

  // Reap completion: Should be CANCELLED
  task_completion_t comp;
  EXPECT_TRUE(wait_for_completion(queue, &comp));
  EXPECT_EQ(comp.status, TASK_STATUS_CANCELLED);
  task_queue_remove_completion(queue);

  // Verify that the task successfully detected the abort via
  // task_should_abort()!
  EXPECT_TRUE(pc.detected_abort.load());

  task_queue_destroy(queue);
}

// Verify that submitting a new task to a serialized stream AFTER a cancellation
// request has been processed, but BEFORE the active cancelled task has
// finished, does NOT result in the new task being cancelled.
TEST(task_queue_concurrent_test, late_submission_to_cancelled_stream_runs) {
  allocator_t alloc = allocator_get_default();
  task_queue_t* queue = task_queue_create(4, thread_executor, alloc);
  ASSERT_NE(queue, nullptr);

  struct test_context {
    std::mutex mutex;
    std::condition_variable cv;
    bool task1_started = false;
    bool task1_proceed = false;
    std::atomic<bool> task3_ran{false};
  } tc;

  auto blocking_task = [](task_context_t* ctx) {
    auto* p = static_cast<test_context*>(ctx->user_data);
    {
      std::unique_lock<std::mutex> lock(p->mutex);
      p->task1_started = true;
    }
    p->cv.notify_all();

    std::unique_lock<std::mutex> lock(p->mutex);
    p->cv.wait(lock, [p] { return p->task1_proceed; });
  };

  auto dummy_task = [](task_context_t* ctx) {
    auto* p = static_cast<test_context*>(ctx->user_data);
    p->task3_ran.store(true);
  };

  // 1. Submit Task 1 (Stream 1) - will block
  task_submission_t* sub1 = task_queue_get_submission(queue);
  ASSERT_NE(sub1, nullptr);
  sub1->task = blocking_task;
  sub1->user_data = &tc;
  sub1->stream = 1;

  task_queue_submit(queue);

  // Wait for Task 1 to start
  {
    std::unique_lock<std::mutex> lock(tc.mutex);
    tc.cv.wait(lock, [&] { return tc.task1_started; });
  }

  // 2. Cancel Stream 1. This should cancel Task 1 (active).
  task_queue_cancel_stream(queue, 1);

  // 3. IMMEDIATELY submit Task 3 (Stream 1).
  // Since Task 1 is still running (blocked in our mutex), this submission
  // happens AFTER the cancel_stream call but BEFORE Task 1 completes.
  task_submission_t* sub3 = task_queue_get_submission(queue);
  ASSERT_NE(sub3, nullptr);
  sub3->task = dummy_task;
  sub3->user_data = &tc;
  sub3->stream = 1;

  task_queue_submit(queue);

  // 4. Release Task 1
  {
    std::unique_lock<std::mutex> lock(tc.mutex);
    tc.task1_proceed = true;
  }
  tc.cv.notify_all();

  // 5. Reap completions.
  // We expect:
  // - Task 1: CANCELLED
  // - Task 3: OK (should run!)

  task_completion_t comp1;
  EXPECT_TRUE(wait_for_completion(queue, &comp1));
  EXPECT_EQ(comp1.status, TASK_STATUS_CANCELLED);
  task_queue_remove_completion(queue);

  task_completion_t comp3;
  EXPECT_TRUE(wait_for_completion(queue, &comp3));

  // If the bug is present, comp3.status will be TASK_STATUS_CANCELLED.
  // We expect it to be TASK_STATUS_OK.
  EXPECT_EQ(comp3.status, TASK_STATUS_OK)
      << "Task 3 was unexpectedly cancelled!";
  EXPECT_TRUE(tc.task3_ran.load());

  task_queue_remove_completion(queue);
  task_queue_destroy(queue);
}

// Verify that if a task is cancelled, and subsequently calls task_set_failed(),
// its final status is still reported as TASK_STATUS_CANCELLED, not FAILED.
TEST(task_queue_concurrent_test, cancellation_precedence_over_failure) {
  allocator_t alloc = allocator_get_default();
  task_queue_t* queue = task_queue_create(4, thread_executor, alloc);
  ASSERT_NE(queue, nullptr);

  struct test_context {
    std::mutex mutex;
    std::condition_variable cv;
    bool started = false;
    bool proceed = false;
  } tc;

  auto failing_cancelled_task = [](task_context_t* ctx) {
    auto* p = static_cast<test_context*>(ctx->user_data);
    {
      std::unique_lock<std::mutex> lock(p->mutex);
      p->started = true;
    }
    p->cv.notify_all();

    // Block until signaled to proceed (giving time for cancel to arrive)
    std::unique_lock<std::mutex> lock(p->mutex);
    p->cv.wait(lock, [p] { return p->proceed; });

    // Even though we were cancelled, we call task_set_failed
    task_set_failed(ctx);
  };

  // 1. Submit the task
  task_submission_t* sub = task_queue_get_submission(queue);
  ASSERT_NE(sub, nullptr);
  sub->task = failing_cancelled_task;
  sub->user_data = &tc;
  sub->stream = 1;

  task_queue_submit(queue);

  // Wait for task to start
  {
    std::unique_lock<std::mutex> lock(tc.mutex);
    tc.cv.wait(lock, [&] { return tc.started; });
  }

  // 2. Cancel the stream (which cancels the active task)
  task_queue_cancel_stream(queue, 1);

  // 3. Release the task
  {
    std::unique_lock<std::mutex> lock(tc.mutex);
    tc.proceed = true;
  }
  tc.cv.notify_all();

  // 4. Reap completion. It MUST be CANCELLED, not FAILED.
  task_completion_t comp;
  EXPECT_TRUE(wait_for_completion(queue, &comp));
  EXPECT_EQ(comp.status, TASK_STATUS_CANCELLED)
      << "Cancellation did not take precedence over failure!";

  task_queue_remove_completion(queue);
  task_queue_destroy(queue);
}

// Test that creating a queue with zero capacity causes a crash.
TEST(task_queue_death_test, zero_capacity) {
  allocator_t alloc = allocator_get_default();
  // This must terminate abnormally (either via segfault or CHECK failure)
  EXPECT_DEATH(task_queue_create(0, thread_executor, alloc), "");
}

// Verify that using a synchronous executor and exceeding CQ capacity
// without reaping triggers the deadlock detector and crashes (fail-fast).
TEST(task_queue_death_test, sync_executor_cq_deadlock_detection) {
  allocator_t alloc = allocator_get_default();
  // Capacity 1: CQ size 1
  task_queue_t* queue = task_queue_create(1, inline_executor, alloc);
  ASSERT_NE(queue, nullptr);

  auto dummy_task = [](task_context_t*) {};

  // 1. Submit Task 1. It will run and complete immediately, filling the CQ.
  task_submission_t* sub1 = task_queue_get_submission(queue);
  ASSERT_NE(sub1, nullptr);
  sub1->task = dummy_task;
  sub1->stream = 0;
  task_queue_submit(queue);

  // CQ is now full (1/1).

  // 2. Try to submit Task 2. This must trigger the deadlock detector and crash
  // because we are running synchronously on the owner thread.
  task_submission_t* sub2 = task_queue_get_submission(queue);
  ASSERT_NE(sub2, nullptr);
  sub2->task = dummy_task;
  sub2->stream = 0;

  // We expect this to crash with the deadlock message
  EXPECT_DEATH(task_queue_submit(queue),
               "Proactor deadlock: sync task blocked on full CQ");

  // Note: We can't destroy the queue because the process dies, but that's fine
  // for death test.
}

// ─── Custom Single-Threaded Executor for Starvation Testing ──────────────────

class SingleThreadedExecutor {
 public:
  SingleThreadedExecutor() : stop_(false) {
    thread_ = std::thread([this]() {
      while (true) {
        std::function<void()> work;
        {
          std::unique_lock<std::mutex> lock(mutex_);
          cv_.wait(lock, [this]() { return !queue_.empty() || stop_; });
          if (stop_ && queue_.empty()) {
            break;
          }
          work = std::move(queue_.front());
          queue_.pop();
        }
        work();
      }
    });
  }

  ~SingleThreadedExecutor() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  void submit(void (*work_fn)(void*), void* arg) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      queue_.push([work_fn, arg]() { work_fn(arg); });
    }
    cv_.notify_all();
  }

 private:
  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::queue<std::function<void()>> queue_;
  bool stop_;
};

static SingleThreadedExecutor* g_single_threaded_executor = nullptr;

static void single_threaded_executor_dispatch(void (*work_fn)(void*),
                                              void* arg) {
  g_single_threaded_executor->submit(work_fn, arg);
}

// Verify that the stream affinity optimization can lead to indefinite
// starvation of independent streams if a stream has a continuous flow of tasks.
TEST(task_queue_concurrent_test, stream_affinity_starvation) {
  allocator_t alloc = allocator_get_default();

  // Setup the single-threaded executor
  g_single_threaded_executor = new SingleThreadedExecutor();

  // Create a queue of capacity 16 using the single-threaded executor
  task_queue_t* queue =
      task_queue_create(16, single_threaded_executor_dispatch, alloc);
  ASSERT_NE(queue, nullptr);

  struct StarvationContext {
    std::mutex mutex;
    std::condition_variable cv_started;
    std::condition_variable cv_gate;
    bool s1_started = false;
    bool s1_gate = false;
    std::atomic<int> s1_run_count{0};
    std::atomic<int> s2_run_count{0};
  } sc;

  auto s1_task = [](task_context_t* ctx) {
    auto* p = static_cast<StarvationContext*>(ctx->user_data);

    // Signal that we started
    {
      std::lock_guard<std::mutex> lock(p->mutex);
      p->s1_started = true;
    }
    p->cv_started.notify_all();

    // Block on the gate
    {
      std::unique_lock<std::mutex> lock(p->mutex);
      p->cv_gate.wait(lock, [p]() { return p->s1_gate; });
      p->s1_gate = false;  // Reset for next task
    }

    p->s1_run_count.fetch_add(1);
  };

  auto s2_task = [](task_context_t* ctx) {
    auto* p = static_cast<StarvationContext*>(ctx->user_data);
    p->s2_run_count.fetch_add(1);
  };

  // 1. Submit S1_T1 (Stream 1). It will run immediately.
  task_submission_t* sub1 = task_queue_get_submission(queue);
  ASSERT_NE(sub1, nullptr);
  sub1->task = s1_task;
  sub1->user_data = &sc;
  sub1->stream = 1;
  task_queue_submit(queue);

  // Wait for S1_T1 to start and block on the gate
  {
    std::unique_lock<std::mutex> lock(sc.mutex);
    sc.cv_started.wait(lock, [&sc]() { return sc.s1_started; });
    sc.s1_started = false;  // Reset
  }

  // S1_T1 is now running (blocked on gate).
  // 2. Submit S2_TA (Stream 2) -> goes to pending list
  task_submission_t* sub_s2 = task_queue_get_submission(queue);
  ASSERT_NE(sub_s2, nullptr);
  sub_s2->task = s2_task;
  sub_s2->user_data = &sc;
  sub_s2->stream = 2;
  task_queue_submit(queue);

  // 3. Submit S1_T2 (Stream 1) -> goes to pending list
  task_submission_t* sub2 = task_queue_get_submission(queue);
  ASSERT_NE(sub2, nullptr);
  sub2->task = s1_task;
  sub2->user_data = &sc;
  sub2->stream = 1;
  task_queue_submit(queue);

  // Now pending list has: [S2_TA, S1_T2]

  // 4. Release S1_T1.
  // When S1_T1 completes, the worker thread will look at the pending list.
  // Because it has affinity for Stream 1, it must prioritize S1_T2 over S2_TA,
  // even though S2_TA is older.
  {
    std::lock_guard<std::mutex> lock(sc.mutex);
    sc.s1_gate = true;
  }
  sc.cv_gate.notify_all();

  // Wait for S1_T2 to start and block on the gate
  {
    std::unique_lock<std::mutex> lock(sc.mutex);
    sc.cv_started.wait(lock, [&sc]() { return sc.s1_started; });
    sc.s1_started = false;
  }

  // S1_T2 is now running. S2_TA is STILL pending!
  EXPECT_EQ(sc.s1_run_count.load(), 1);  // S1_T1 finished
  EXPECT_EQ(sc.s2_run_count.load(), 0);  // S2_TA did NOT run!

  // 5. Submit S1_T3 (Stream 1) while S1_T2 is running.
  task_submission_t* sub3 = task_queue_get_submission(queue);
  ASSERT_NE(sub3, nullptr);
  sub3->task = s1_task;
  sub3->user_data = &sc;
  sub3->stream = 1;
  task_queue_submit(queue);

  // Release S1_T2
  {
    std::lock_guard<std::mutex> lock(sc.mutex);
    sc.s1_gate = true;
  }
  sc.cv_gate.notify_all();

  // Wait for S1_T3 to start
  {
    std::unique_lock<std::mutex> lock(sc.mutex);
    sc.cv_started.wait(lock, [&sc]() { return sc.s1_started; });
    sc.s1_started = false;
  }

  // S1_T3 is now running. S2_TA is STILL pending!
  EXPECT_EQ(sc.s1_run_count.load(), 2);  // S1_T1 and S1_T2 finished
  EXPECT_EQ(sc.s2_run_count.load(), 0);  // S2_TA did NOT run!

  // 6. Submit S1_T4 (Stream 1) while S1_T3 is running.
  task_submission_t* sub4 = task_queue_get_submission(queue);
  ASSERT_NE(sub4, nullptr);
  sub4->task = s1_task;
  sub4->user_data = &sc;
  sub4->stream = 1;
  task_queue_submit(queue);

  // Release S1_T3
  {
    std::lock_guard<std::mutex> lock(sc.mutex);
    sc.s1_gate = true;
  }
  sc.cv_gate.notify_all();

  // Wait for S1_T4 to start
  {
    std::unique_lock<std::mutex> lock(sc.mutex);
    sc.cv_started.wait(lock, [&sc]() { return sc.s1_started; });
    sc.s1_started = false;
  }

  // S1_T4 is now running. S2_TA is STILL pending!
  EXPECT_EQ(sc.s1_run_count.load(), 3);
  EXPECT_EQ(sc.s2_run_count.load(), 0);

  // 7. Finally, release S1_T4 and do NOT submit any more Stream 1 tasks.
  // The worker should now fall back to FIFO and execute S2_TA.
  {
    std::lock_guard<std::mutex> lock(sc.mutex);
    sc.s1_gate = true;
  }
  sc.cv_gate.notify_all();

  // Wait for all completions (4 from Stream 1, 1 from Stream 2)
  int reaped = 0;
  for (int i = 0; i < 5; ++i) {
    task_completion_t comp;
    if (wait_for_completion(queue, &comp, 500.0)) {
      reaped++;
      task_queue_remove_completion(queue);
    }
  }

  // S2_TA must have run and completed
  EXPECT_EQ(reaped, 5);
  EXPECT_EQ(sc.s1_run_count.load(), 4);
  EXPECT_EQ(sc.s2_run_count.load(), 1);  // S2_TA finally ran!

  task_queue_destroy(queue);
  delete g_single_threaded_executor;
  g_single_threaded_executor = nullptr;
}
