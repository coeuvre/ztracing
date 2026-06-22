#include "src/trace_search_task.h"

#include <gtest/gtest.h>

#include "src/allocator.h"
#include "src/app_msg.h"
#include "src/colors.h"
#include "src/platform.h"
#include "src/trace_data.h"
#include "src/trace_histogram.h"

// Verify that app_send_trace_search_complete automatically cleans up results
// and histogram on failure.
TEST(trace_search_task_test,
     safe_send_helper_search_complete_cleanup_on_failure) {
  counting_allocator_t ca = counting_allocator_init(allocator_get_default());
  allocator_t a = counting_allocator_get_allocator(&ca);

  {
    // App channel
    channel_t* chan = channel_create(app_msg_t, 5, app_msg_deinit, a);

    // Allocate search results list and histogram on the heap
    trace_histogram_t* hist =
        (trace_histogram_t*)allocator_alloc(a, sizeof(trace_histogram_t));
    *hist = {};  // Zero-initialize

    array_list_t results = {};

    // Create a mock trace data shell on the heap (ref_count = 1)
    trace_data_t* td = trace_data_create(a);

    // Close channel to force send failure!
    channel_close_tx(chan);
    channel_close_rx(chan);

    // Send should fail and AUTOMATICALLY free results, hist, and release td!
    EXPECT_FALSE(
        app_send_trace_search_complete(chan, td, results, hist, nullptr, a));

    channel_destroy(chan);
  }

  // If hist or td was not freed automatically, this check would fail!
  EXPECT_EQ(counting_allocator_get_allocated_bytes(&ca), 0u);
}

// E2E test for the background trace search task.
// Verifies event scanning, case-insensitive matching, and results delivery.
TEST(trace_search_task_test, e2e_search_task) {
  counting_allocator_t ca = counting_allocator_init(allocator_get_default());
  allocator_t a = counting_allocator_get_allocator(&ca);

  {
    // Construct a mock trace data with 3 events on the heap (ref_count = 1)
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

    // Create coordination channels
    channel_t* app_channel = channel_create(app_msg_t, 5, app_msg_deinit, a);

    // Retain td for the background task (ref_count: 1 -> 2)
    trace_data_retain(td);

    // Start background search for "foo" (creates its own search channel,
    // which is later destroyed by app_msg_deinit via task_channel)
    trace_search_start("foo", td, true, true, app_channel, a);

    // Block-receive the result from app_channel
    app_msg_t msg = {};
    EXPECT_TRUE(channel_recv(app_channel, &msg));
    EXPECT_EQ(msg.type, APP_MSG_TRACE_SEARCH_COMPLETE);

    app_msg_trace_search_complete_t result = msg.as.trace_search_complete;
    EXPECT_EQ(result.results.len, 1u);
    EXPECT_EQ(((int64_t*)result.results.ptr)[0], 0);  // Event index 0

    EXPECT_NE(result.histogram, nullptr);
    EXPECT_EQ(result.histogram->total_count, 1u);

    // Centralized cleanup via app_msg_deinit!
    // This will release the background task's reference to td (ref_count: 2 ->
    // 1), deinit the results list, free the histogram shell, and destroy the
    // search_channel.
    // First close our (tx) side of the search channel, mirroring what
    // app_poll_messages does in the SEARCH_COMPLETE handler before deinit.
    channel_close_tx(result.task_channel);
    app_msg_deinit(&msg);

    // Tear down channels and release our own reference to td (ref_count: 1
    // -> 0, triggers free)
    channel_close_tx(app_channel);
    channel_close_rx(app_channel);
    channel_destroy(app_channel);
    trace_data_release(td, a);
    trace_event_matcher_deinit(&matcher);

    // Join the worker pool before checking allocations: the background search
    // task frees its own task struct + query string asynchronously after
    // channel_send returns, so without joining there's a race where those
    // (~60 bytes) haven't been freed yet when we check below.
    platform_teardown_workers();
  }

  // Verify that all memory was perfectly deallocated!
  EXPECT_EQ(counting_allocator_get_allocated_bytes(&ca), 0u);
}
