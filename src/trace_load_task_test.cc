#include "src/trace_load_task.h"

#include <gtest/gtest.h>

#include "src/allocator.h"
#include "src/app_msg.h"
#include "src/trace_data.h"

// Verify that app_send_trace_load_complete automatically cleans up trace data
// and tracks on send failure.
TEST(trace_load_task_test, safe_send_helper_load_complete_cleanup_on_failure) {
  counting_allocator_t ca = counting_allocator_init(allocator_get_default());
  allocator_t a = counting_allocator_get_allocator(&ca);

  {
    // App channel
    channel_t* chan = channel_create(app_msg_t, 5, app_msg_deinit, a);

    // Create trace data shell on the heap using the lifecycle API (ref_count =
    // 1)
    trace_data_t* td = trace_data_create(a);

    array_list_t tracks = {};  // Empty list requires no heap allocation, but
                               // deinit is a safe no-op

    // Close channel to force send failure!
    channel_close_tx(chan);
    channel_close_rx(chan);

    // Send should fail and AUTOMATICALLY deinit and free both td and tracks!
    EXPECT_FALSE(
        app_send_trace_load_complete(chan, td, tracks, 0, 0, nullptr, a));

    channel_destroy(chan);
  }

  // If td was not freed automatically, this check would fail!
  EXPECT_EQ(counting_allocator_get_allocated_bytes(&ca), 0u);
}
