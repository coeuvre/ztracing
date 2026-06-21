#include "src/app_msg.h"

#include <stdbool.h>
#include <stddef.h>

#include "src/assert.h"
#include "src/trace_data.h"
#include "src/trace_viewer.h"

// Forward declaration to break circular dependency in BUILD target dependencies
void trace_load_msg_deinit(void* msg, allocator_t allocator);

void app_msg_deinit(app_msg_t* msg, allocator_t allocator) {
  CHECK(msg != nullptr);

  switch (msg->type) {
    case MSG_TRACE_LOAD_COMPLETE: {
      app_msg_load_result_t* res = &msg->as.load_result;
      if (res->trace_data != nullptr) {
        trace_data_release(res->trace_data, allocator);
      }
      array_list_deinit(&res->tracks, allocator);
      if (res->task_channel != nullptr) {
        channel_close_and_drain_(
            res->task_channel,
            (void (*)(void*, allocator_t))trace_load_msg_deinit, allocator);
        channel_destroy(res->task_channel, allocator);
      }
      break;
    }

    case MSG_TRACE_LOAD_ABORTED: {
      app_msg_load_aborted_t* aborted = &msg->as.load_aborted;
      if (aborted->task_channel != nullptr) {
        channel_close_and_drain_(
            aborted->task_channel,
            (void (*)(void*, allocator_t))trace_load_msg_deinit, allocator);
        channel_destroy(aborted->task_channel, allocator);
      }
      break;
    }

    case MSG_TRACE_SEARCH_COMPLETE: {
      app_msg_search_result_t* res = &msg->as.search_result;
      trace_data_release(res->trace_data, allocator);
      array_list_deinit(&res->results, allocator);
      if (res->histogram != nullptr) {
        allocator_free(allocator, res->histogram, sizeof(duration_histogram_t));
      }
      if (res->task_channel != nullptr) {
        channel_close_and_drain(res->task_channel, trace_search_msg_t, nullptr,
                                allocator);
        channel_destroy(res->task_channel, allocator);
      }
      break;
    }

    case MSG_TRACE_SEARCH_ABORTED: {
      app_msg_search_aborted_t* aborted = &msg->as.search_aborted;
      trace_data_release(aborted->trace_data, allocator);
      if (aborted->task_channel != nullptr) {
        channel_close_and_drain(aborted->task_channel, trace_search_msg_t,
                                nullptr, allocator);
        channel_destroy(aborted->task_channel, allocator);
      }
      break;
    }

    default:
      break;
  }
}

bool app_send_load_progress(channel_t* app_channel, size_t event_count,
                            size_t total_bytes) {
  CHECK(app_channel != nullptr);

  app_msg_t msg = {.type = MSG_TRACE_LOAD_PROGRESS,
                   .as = {.load_progress = {.event_count = event_count,
                                            .total_bytes = total_bytes}}};
  return channel_send(app_channel, &msg);
}

bool app_send_load_complete(channel_t* app_channel, trace_data_t* trace_data,
                            array_list_t tracks, int64_t min_ts, int64_t max_ts,
                            channel_t* task_channel, allocator_t allocator) {
  CHECK(app_channel != nullptr);
  CHECK(trace_data != nullptr);

  app_msg_t msg = {.type = MSG_TRACE_LOAD_COMPLETE,
                   .as = {.load_result = {.trace_data = trace_data,
                                          .tracks = tracks,
                                          .min_ts = min_ts,
                                          .max_ts = max_ts,
                                          .task_channel = task_channel}}};

  bool ok = channel_send(app_channel, &msg);
  if (!ok) {
    app_msg_deinit(&msg, allocator);
  }
  return ok;
}

bool app_send_load_aborted(channel_t* app_channel, channel_t* task_channel,
                           allocator_t allocator) {
  CHECK(app_channel != nullptr);
  app_msg_t msg = {.type = MSG_TRACE_LOAD_ABORTED,
                   .as = {.load_aborted = {.task_channel = task_channel}}};
  bool ok = channel_send(app_channel, &msg);
  if (!ok) {
    app_msg_deinit(&msg, allocator);
  }
  return ok;
}

bool app_send_search_complete(channel_t* app_channel, trace_data_t* trace_data,
                              array_list_t results,
                              duration_histogram_t* histogram,
                              channel_t* task_channel, allocator_t allocator) {
  CHECK(app_channel != nullptr);
  CHECK(trace_data != nullptr);
  CHECK(histogram != nullptr);

  app_msg_t msg = {.type = MSG_TRACE_SEARCH_COMPLETE,
                   .as = {.search_result = {.trace_data = trace_data,
                                            .results = results,
                                            .histogram = histogram,
                                            .task_channel = task_channel}}};

  bool ok = channel_send(app_channel, &msg);
  if (!ok) {
    app_msg_deinit(&msg, allocator);
  }
  return ok;
}

bool app_send_search_aborted(channel_t* app_channel, trace_data_t* trace_data,
                             channel_t* task_channel, allocator_t allocator) {
  CHECK(app_channel != nullptr);
  app_msg_t msg = {.type = MSG_TRACE_SEARCH_ABORTED,
                   .as = {.search_aborted = {.trace_data = trace_data,
                                             .task_channel = task_channel}}};

  bool ok = channel_send(app_channel, &msg);
  if (!ok) {
    app_msg_deinit(&msg, allocator);
  }
  return ok;
}
