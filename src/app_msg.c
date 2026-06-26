#include "src/app_msg.h"

#include <stdbool.h>
#include <stddef.h>

#include "src/assert.h"
#include "src/trace_data.h"
#include "src/trace_viewer.h"

void app_msg_deinit(app_msg_t* msg) {
  CHECK(msg != nullptr);
  allocator_t allocator = msg->allocator;

  switch (msg->type) {
    case APP_MSG_TRACE_SEARCH_COMPLETE: {
      app_msg_trace_search_complete_t* res = &msg->as.trace_search_complete;
      trace_data_release(res->trace_data, allocator);
      array_list_deinit(&res->results, allocator);
      if (res->histogram != nullptr) {
        allocator_free(allocator, res->histogram, sizeof(trace_histogram_t));
      }
      if (res->task_channel != nullptr) {
        channel_destroy(res->task_channel);
      }
      break;
    }

    case APP_MSG_TRACE_SEARCH_ABORTED: {
      app_msg_trace_search_aborted_t* aborted = &msg->as.trace_search_aborted;
      trace_data_release(aborted->trace_data, allocator);
      if (aborted->task_channel != nullptr) {
        channel_destroy(aborted->task_channel);
      }
      break;
    }

    default:
      break;
  }
}

bool app_send_trace_search_complete(channel_t* app_channel,
                                    trace_data_t* trace_data,
                                    array_list_t results,
                                    trace_histogram_t* histogram,
                                    channel_t* task_channel,
                                    allocator_t allocator) {
  CHECK(trace_data != nullptr);
  CHECK(histogram != nullptr);

  app_msg_t msg = {
      .type = APP_MSG_TRACE_SEARCH_COMPLETE,
      .allocator = allocator,
      .as = {.trace_search_complete = {.trace_data = trace_data,
                                       .results = results,
                                       .histogram = histogram,
                                       .task_channel = task_channel}}};

  return channel_send(app_channel, &msg);
}

bool app_send_trace_search_aborted(channel_t* app_channel,
                                   trace_data_t* trace_data,
                                   channel_t* task_channel,
                                   allocator_t allocator) {
  app_msg_t msg = {
      .type = APP_MSG_TRACE_SEARCH_ABORTED,
      .allocator = allocator,
      .as = {.trace_search_aborted = {.trace_data = trace_data,
                                      .task_channel = task_channel}}};

  return channel_send(app_channel, &msg);
}
