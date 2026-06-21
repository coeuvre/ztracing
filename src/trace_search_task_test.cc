#include "src/trace_search_task.h"

#include <gtest/gtest.h>

#include "src/allocator.h"
#include "src/app_msg.h"
#include "src/colors.h"
#include "src/trace_data.h"
#include "src/trace_viewer.h"  // For duration_histogram_t

// Verify that app_send_search_complete automatically cleans up results and
// histogram on failure.
TEST(trace_search_task_test,
     safe_send_helper_search_complete_cleanup_on_failure) {
  counting_allocator_t ca = counting_allocator_init(allocator_get_default());
  allocator_t a = counting_allocator_get_allocator(&ca);

  {
    // App channel
    channel_t* chan = channel_create(app_msg_t, 5, a);

    // Allocate search results list and histogram on the heap
    duration_histogram_t* hist =
        (duration_histogram_t*)allocator_alloc(a, sizeof(duration_histogram_t));
    *hist = {};  // Zero-initialize

    array_list_t results = {};

    // Create a mock trace data shell on the heap (ref_count = 1)
    trace_data_t* td = trace_data_create(a);

    // Close channel to force send failure!
    channel_close_and_drain(chan, app_msg_t, app_msg_deinit, a);

    // Send should fail and AUTOMATICALLY free results, hist, and release td!
    EXPECT_FALSE(app_send_search_complete(chan, td, results, hist, nullptr, a));

    channel_destroy(chan, a);
  }

  // If hist or td was not freed automatically, this check would fail!
  EXPECT_EQ(counting_allocator_get_allocated_bytes(&ca), 0u);
}

// Verify that trace_search_send_abort successfully packages and transmits the
// abort message.
TEST(trace_search_task_test, safe_send_helper_search_abort) {
  allocator_t a = allocator_get_default();

  {
    channel_t* chan = channel_create(trace_search_msg_t, 2, a);

    EXPECT_TRUE(trace_search_send_abort(chan));

    trace_search_msg_t popped_msg = {};
    EXPECT_TRUE(channel_recv(chan, &popped_msg));
    EXPECT_EQ(popped_msg.type, MSG_TRACE_SEARCH_ABORT);

    channel_destroy(chan, a);
  }
}

// E2E test for the background trace search task.
// Verifies event scanning, case-insensitive matching, and results delivery.
TEST(trace_search_task_test, e2e_search_task) {
  counting_allocator_t ca = counting_allocator_init(allocator_get_default());
  allocator_t a = counting_allocator_get_allocator(&ca);

  {
    // 1. Construct a mock trace data with 3 events on the heap (ref_count = 1)
    trace_data_t* td = trace_data_create(a);
    trace_event_matcher_t matcher = {};

    // Event 0: name="foo", cat="bar"
    trace_event_t ev0 = {};
    ev0.name = string_lit("foo");
    ev0.cat = string_lit("bar");
    ev0.ph = string_lit("X");
    ev0.ts = 100;
    ev0.dur = 50;
    trace_data_add_event(td, &ev0, &matcher, a);

    // Event 1: name="hello", cat="world"
    trace_event_t ev1 = {};
    ev1.name = string_lit("hello");
    ev1.cat = string_lit("world");
    ev1.ph = string_lit("X");
    ev1.ts = 200;
    ev1.dur = 10;
    trace_data_add_event(td, &ev1, &matcher, a);

    // Event 2: name="baz", cat="bar"
    trace_event_t ev2 = {};
    ev2.name = string_lit("baz");
    ev2.cat = string_lit("bar");
    ev2.ph = string_lit("X");
    ev2.ts = 300;
    ev2.dur = 20;
    trace_data_add_event(td, &ev2, &matcher, a);

    // 2. Create coordination channels
    channel_t* app_channel = channel_create(app_msg_t, 5, a);
    channel_t* search_channel = channel_create(trace_search_msg_t, 5, a);

    // Retain td for the background task (ref_count: 1 -> 2)
    trace_data_retain(td);

    // 3. Start background search for "foo"
    trace_search_start("foo", td, true, true, app_channel, search_channel, a);

    // 4. Block-receive the result from app_channel
    app_msg_t msg = {};
    EXPECT_TRUE(channel_recv(app_channel, &msg));
    EXPECT_EQ(msg.type, MSG_TRACE_SEARCH_COMPLETE);

    app_msg_search_result_t result = msg.as.search_result;
    EXPECT_EQ(result.results.len, 1u);
    EXPECT_EQ(((int64_t*)result.results.ptr)[0], 0);  // Event index 0

    EXPECT_NE(result.histogram, nullptr);
    EXPECT_EQ(result.histogram->total_count, 1u);

    // 5. Centralized cleanup via app_msg_deinit!
    // This will release the background task's reference to td (ref_count: 2 ->
    // 1), deinit the results list, free the histogram shell, and destroy the
    // search_channel.
    app_msg_deinit(&msg, a);

    // 6. Tear down channels and release our own reference to td (ref_count: 1
    // -> 0, triggers free)
    channel_destroy(app_channel, a);
    trace_data_release(td, a);
    trace_event_matcher_deinit(&matcher);
  }

  // Verify that all memory was perfectly deallocated!
  EXPECT_EQ(counting_allocator_get_allocated_bytes(&ca), 0u);
}
