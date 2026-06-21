#include "src/trace_load_task.h"

#include <gtest/gtest.h>

#include "src/allocator.h"
#include "src/app_msg.h"
#include "src/trace_data.h"

// Verify that trace_load_send_chunk automatically cleans up raw heap data if
// the send fails.
TEST(trace_load_task_test, safe_send_helper_chunk_cleanup_on_failure) {
  counting_allocator_t ca = counting_allocator_init(allocator_get_default());
  allocator_t a = counting_allocator_get_allocator(&ca);

  {
    // Bounded channel of capacity 1
    channel_t* chan = channel_create(trace_load_msg_t, 1, a);

    // 1. Send first chunk (Should succeed)
    char* data1 = (char*)allocator_alloc(a, 10);
    EXPECT_TRUE(trace_load_send_chunk(chan, data1, 10, 0, false, a));

    // 2. Send second chunk (Should fail due to queue full, and AUTOMATICALLY
    // free data2)
    char* data2 = (char*)allocator_alloc(a, 20);
    EXPECT_FALSE(trace_load_send_chunk(chan, data2, 20, 0, false, a));

    // 3. Drain the first chunk to manually free its memory
    trace_load_msg_t popped_msg;
    EXPECT_TRUE(channel_recv(chan, &popped_msg));
    EXPECT_EQ(popped_msg.type, MSG_TRACE_LOAD_CHUNK);
    allocator_free(a, popped_msg.as.chunk.data, popped_msg.as.chunk.size);

    channel_destroy(chan, a);
  }

  // If data2 was not freed automatically, this check would fail!
  EXPECT_EQ(counting_allocator_get_allocated_bytes(&ca), 0u);
}

// Verify that app_send_load_complete automatically cleans up trace data and
// tracks on send failure.
TEST(trace_load_task_test, safe_send_helper_load_complete_cleanup_on_failure) {
  counting_allocator_t ca = counting_allocator_init(allocator_get_default());
  allocator_t a = counting_allocator_get_allocator(&ca);

  {
    // App channel
    channel_t* chan = channel_create(app_msg_t, 5, a);

    // Allocate trace data shell and tracks list on the heap
    trace_data_t* td = (trace_data_t*)allocator_alloc(a, sizeof(trace_data_t));
    // Zero-initialize to ensure trace_data_deinit is a safe no-op on internal
    // lists
    *td = {};

    array_list_t tracks = {};  // Empty list requires no heap allocation, but
                               // deinit is a safe no-op

    // Close channel to force send failure!
    channel_close_and_drain(chan, app_msg_t, app_msg_deinit, a);

    // Send should fail and AUTOMATICALLY deinit and free both td and tracks!
    EXPECT_FALSE(app_send_load_complete(chan, td, tracks, 0, 0, a));

    channel_destroy(chan, a);
  }

  // If td was not freed automatically, this check would fail!
  EXPECT_EQ(counting_allocator_get_allocated_bytes(&ca), 0u);
}
