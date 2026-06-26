#include "src/trace_load_task.h"

#include <gtest/gtest.h>

#include "core/allocator.h"
#include "core/counting_allocator.h"
#include "core/task.h"
#include "src/trace_data.h"
#include "src/track.h"

// Simple inline executor that runs tasks synchronously for deterministic
// testing
static void inline_executor(void (*work_fn)(void*), void* arg) { work_fn(arg); }

TEST(trace_load_task_test, success_path_reaps_correctly) {
  counting_allocator_t ca;
  counting_allocator_init(&ca, c_allocator());
  allocator_t* a = counting_allocator_get_allocator(&ca);

  {
    task_queue_t* queue = task_queue_create(32, inline_executor, a);
    task_stream_t stream_id = 1;

    // 1. Create the task context
    trace_load_task_t* task = trace_load_task_create(queue, stream_id, a);
    ASSERT_NE(task, nullptr);

    // 2. Submit a chunk
    task_submission_t* sub = task_queue_get_submission(queue);
    ASSERT_NE(sub, nullptr);

    // Create some raw dummy JSON trace data
    const char* dummy_json = R"([
      {"name": "event1", "cat": "test", "ph": "B", "ts": 1000, "pid": 1, "tid": 1},
      {"name": "event1", "cat": "test", "ph": "E", "ts": 2000, "pid": 1, "tid": 1}
    ])";
    size_t data_len = strlen(dummy_json) + 1;
    // Prepare and submit (runs inline/synchronously! Internally copies
    // dummy_json to arena)
    trace_load_task_prep_chunk(task, sub, dummy_json, data_len, data_len,
                               true /* is_eof */);
    task_queue_submit(queue);

    // 3. Verify Completion Queue (CQ) has the completion
    task_completion_t cqe;
    EXPECT_TRUE(task_queue_peek_completion(queue, &cqe));

    // Identify task type directly from cqe.task!
    EXPECT_EQ(cqe.task, trace_load_task_run);
    EXPECT_EQ(cqe.status, TASK_STATUS_OK);

    // 4. Reap results from CQE payload
    trace_load_task_chunk_t* payload = (trace_load_task_chunk_t*)cqe.user_data;
    ASSERT_NE(payload, nullptr);
    EXPECT_TRUE(payload->is_eof);
    EXPECT_EQ(payload->parsed_event_count,
              1u);  // Matched B/E events -> 1 duration event!

    // Adopt results
    trace_data_t* td = payload->completed_td;
    array_list_t tracks = payload->completed_tracks;
    EXPECT_NE(td, nullptr);
    EXPECT_GT(tracks.len, 0u);

    // Clean up results (loop and deinit each track!)
    for (size_t i = 0; i < tracks.len; ++i) {
      track_t* track = array_list_get(&tracks, track_t, i);
      track_deinit(track, a);
    }
    trace_data_release(td, a);
    array_list_deinit(&tracks, a);

    // Note: payload->data and payload itself are allocated from the task-local
    // arena and will be automatically reclaimed when
    // task_queue_remove_completion is called.
    trace_load_task_release(task);  // Release CQE reference

    task_queue_remove_completion(queue);

    // 5. Release UI thread's reference
    trace_load_task_release(task);

    task_queue_destroy(queue);
  }

  // Verify all memory was cleanly freed
  EXPECT_EQ(counting_allocator_get_allocated_bytes(&ca), 0u);
}

TEST(trace_load_task_test, cancellation_path_cleans_up_without_leaks) {
  counting_allocator_t ca;
  counting_allocator_init(&ca, c_allocator());
  allocator_t* a = counting_allocator_get_allocator(&ca);

  {
    task_queue_t* queue = task_queue_create(32, inline_executor, a);
    task_stream_t stream_id = 1;

    // 1. Create the task context
    trace_load_task_t* task = trace_load_task_create(queue, stream_id, a);
    ASSERT_NE(task, nullptr);

    // 2. Submit a custom aborting task on the same serialized stream (stream 1)
    task_submission_t* sub1 = task_queue_get_submission(queue);
    ASSERT_NE(sub1, nullptr);
    sub1->task = [](task_context_t* ctx) {
      trace_load_task_t* t = (trace_load_task_t*)ctx->user_data;
      trace_load_task_abort(t);  // Aborts stream 1!
    };
    sub1->user_data = task;
    sub1->stream = stream_id;

    // 3. Submit a loader chunk task on the same serialized stream (stream 1)
    task_submission_t* sub2 = task_queue_get_submission(queue);
    ASSERT_NE(sub2, nullptr);

    const char* dummy_json = "[]";
    size_t data_len = strlen(dummy_json) + 1;
    // Internally copies dummy_json to arena
    trace_load_task_prep_chunk(task, sub2, dummy_json, data_len, data_len,
                               false /* is_eof */);

    // 4. Submit the queue.
    // sub1 executes first, aborts the stream, which cancels the pending sub2 in
    // the SQ. sub2 is then bypassed and completed as CANCELLED.
    task_queue_submit(queue);

    // 5. Verify and reap completions
    task_completion_t cqe;

    // First completion is the custom aborting task.
    // Since it calls abort on its own stream, it cancels itself!
    EXPECT_TRUE(task_queue_peek_completion(queue, &cqe));
    EXPECT_EQ(cqe.status, TASK_STATUS_CANCELLED);
    task_queue_remove_completion(queue);

    // Second completion is the loader chunk task (should be CANCELLED!)
    EXPECT_TRUE(task_queue_peek_completion(queue, &cqe));
    EXPECT_EQ(cqe.status, TASK_STATUS_CANCELLED);

    // Verify task pointer identity is preserved perfectly even on cancelled
    // tasks!
    EXPECT_EQ(cqe.task, trace_load_task_run);

    // Reap cancelled CQE and clean up payload raw buffer to prevent leaks
    trace_load_task_chunk_t* payload = (trace_load_task_chunk_t*)cqe.user_data;
    ASSERT_NE(payload, nullptr);
    EXPECT_NE(payload->data, nullptr);  // Raw buffer was never consumed!

    // Note: payload->data and payload itself are allocated from the task-local
    // arena and will be automatically reclaimed when
    // task_queue_remove_completion is called.

    trace_load_task_release(task);  // Release CQE reference
    task_queue_remove_completion(queue);

    // 6. Release UI thread's reference
    trace_load_task_release(task);

    task_queue_destroy(queue);
  }

  // If the cancelled payload or raw buffer leaked, this check will fail!
  EXPECT_EQ(counting_allocator_get_allocated_bytes(&ca), 0u);
}
