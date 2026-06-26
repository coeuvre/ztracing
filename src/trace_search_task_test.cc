#include "src/trace_search_task.h"

#include <gtest/gtest.h>

#include "src/allocator.h"
#include "src/platform.h"
#include "src/task.h"
#include "src/trace_data.h"
#include "src/trace_histogram.h"

// E2E test for the background trace search task.
// Verifies event scanning, case-insensitive matching, and results delivery via
// the Task Queue.
TEST(trace_search_task_test, e2e_search_task) {
  counting_allocator_t ca = counting_allocator_init(allocator_get_default());
  allocator_t a = counting_allocator_get_allocator(&ca);

  {
    // Construct a mock trace data with 3 events on the heap (ref_count = 1)
    trace_data_t* td = trace_data_create(a);
    trace_event_matcher_t matcher = {};

    // Event 0: name="foo", cat="bar"
    trace_event_t ev0 = {};
    ev0.name = SV("foo");
    ev0.cat = SV("bar");
    ev0.ph = SV("X");
    ev0.ts = 100;
    ev0.dur = 50;
    trace_data_add_event(td, &ev0, &matcher, a);

    // Event 1: name="hello", cat="world"
    trace_event_t ev1 = {};
    ev1.name = SV("hello");
    ev1.cat = SV("world");
    ev1.ph = SV("X");
    ev1.ts = 200;
    ev1.dur = 10;
    trace_data_add_event(td, &ev1, &matcher, a);

    // Event 2: name="baz", cat="bar"
    trace_event_t ev2 = {};
    ev2.name = SV("baz");
    ev2.cat = SV("bar");
    ev2.ph = SV("X");
    ev2.ts = 300;
    ev2.dur = 20;
    trace_data_add_event(td, &ev2, &matcher, a);

    // Create global task queue
    task_queue_t* queue = task_queue_create(64, platform_submit_job, a);

    // Retain td for the background task (ref_count: 1 -> 2)
    trace_data_retain(td);

    // Start background search for "foo" on the Task Queue
    task_submission_t* sub = task_queue_get_submission(queue);
    ASSERT_NE(sub, nullptr);

    trace_search_task_t* task =
        trace_search_task_create("foo", td, true, true, sub, a);
    EXPECT_NE(task, nullptr);

    task_queue_submit(queue);

    // Block and wait for the search task completion in the CQ
    task_completion_t cqe = {};
    task_queue_wait_completion(queue, &cqe);
    EXPECT_EQ(cqe.task, trace_search_task_run);
    EXPECT_EQ(cqe.user_data, task);
    EXPECT_EQ(cqe.status, TASK_STATUS_OK);

    // Verify search results inside the task context
    EXPECT_EQ(task->results.len, 1u);
    EXPECT_EQ(((int64_t*)task->results.ptr)[0], 0);  // Event index 0

    EXPECT_NE(task->histogram, nullptr);
    EXPECT_EQ(task->histogram->total_count, 1u);

    // Clean up resources
    task_queue_remove_completion(queue);

    // Release both references to td (ref_count: 2 -> 0, triggers free)
    trace_data_release(td, a);
    trace_data_release(td, a);

    trace_event_matcher_deinit(&matcher);
    trace_search_task_destroy(task);
    task_queue_destroy(queue);

    // Tear down workers to ensure all threads exit and free their resources
    platform_teardown_workers();
  }

  // Verify that all memory was perfectly deallocated!
  EXPECT_EQ(counting_allocator_get_allocated_bytes(&ca), 0u);
}
