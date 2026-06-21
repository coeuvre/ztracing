#include "src/app_msg.h"

#include <gtest/gtest.h>

#include "src/allocator.h"

// Verify that app_send_trace_load_progress successfully packages and transmits
// progress updates.
TEST(app_msg_test, app_send_trace_load_progress_packaging) {
  allocator_t a = allocator_get_default();

  {
    channel_t* chan = channel_create(app_msg_t, 2, app_msg_deinit, a);

    EXPECT_TRUE(app_send_trace_load_progress(chan, 1234, 5678));

    app_msg_t popped_msg = {};
    EXPECT_TRUE(channel_recv(chan, &popped_msg));
    EXPECT_EQ(popped_msg.type, APP_MSG_TRACE_LOAD_PROGRESS);
    EXPECT_EQ(popped_msg.as.trace_load_progress.event_count, 1234u);
    EXPECT_EQ(popped_msg.as.trace_load_progress.total_bytes, 5678u);

    channel_close_tx(chan);
    channel_close_rx(chan);
    channel_destroy(chan);
  }
}

// Verify that app_send_trace_load_aborted successfully packages and transmits
// the load aborted signal.
TEST(app_msg_test, app_send_trace_load_aborted_packaging) {
  allocator_t a = allocator_get_default();

  {
    channel_t* chan = channel_create(app_msg_t, 2, app_msg_deinit, a);

    EXPECT_TRUE(app_send_trace_load_aborted(chan, nullptr, a));

    app_msg_t popped_msg = {};
    EXPECT_TRUE(channel_recv(chan, &popped_msg));
    EXPECT_EQ(popped_msg.type, APP_MSG_TRACE_LOAD_ABORTED);
    EXPECT_EQ(popped_msg.as.trace_load_aborted.task_channel, nullptr);

    channel_close_tx(chan);
    channel_close_rx(chan);
    channel_destroy(chan);
  }
}

// Verify that app_send_trace_search_aborted successfully packages and transmits
// the search aborted signal.
TEST(app_msg_test, app_send_trace_search_aborted_packaging) {
  allocator_t a = allocator_get_default();

  {
    channel_t* chan = channel_create(app_msg_t, 2, app_msg_deinit, a);

    EXPECT_TRUE(app_send_trace_search_aborted(chan, nullptr, nullptr, a));

    app_msg_t popped_msg = {};
    EXPECT_TRUE(channel_recv(chan, &popped_msg));
    EXPECT_EQ(popped_msg.type, APP_MSG_TRACE_SEARCH_ABORTED);
    EXPECT_EQ(popped_msg.as.trace_search_aborted.trace_data, nullptr);
    EXPECT_EQ(popped_msg.as.trace_search_aborted.task_channel, nullptr);

    channel_close_tx(chan);
    channel_close_rx(chan);
    channel_destroy(chan);
  }
}
