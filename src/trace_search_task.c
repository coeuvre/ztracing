#include "src/trace_search_task.h"

#include <stdbool.h>
#include <string.h>

#include "src/app_msg.h"
#include "src/assert.h"
#include "src/logging.h"
#include "src/platform.h"
#include "src/trace_data.h"
#include "src/trace_viewer.h"

// === Input Message Types (private to this translation unit) ===
typedef enum {
  TRACE_SEARCH_MSG_ABORT,  // Signal the active search task to abort execution
} trace_search_msg_type_t;

// === Input Message Envelope ===
typedef struct {
  trace_search_msg_type_t type;
} trace_search_msg_t;

// Context structure for the background search thread
typedef struct {
  char* query;  // Heap-allocated copy of query string
  const trace_data_t* td;
  bool include_threads;
  bool include_counters;
  channel_t* app_channel;
  channel_t* trace_search_channel;
  allocator_t allocator;
} trace_search_task_t;

// Background worker thread function
static void trace_search_run(void* arg) {
  trace_search_task_t* task = (trace_search_task_t*)arg;
  allocator_t allocator = task->allocator;
  const trace_data_t* td = task->td;

  LOG_DEBUG("trace_search_run background task started (query: '%s')",
            task->query ? task->query : "");

  array_list_t results = {};  // ZII
  bool aborted = false;

  if (task->query && task->query[0] != '\0') {
    const char* query_ptr = task->query;
    size_t query_len = strlen(query_ptr);
    size_t n_events = td->events.len;

    for (size_t i = 0; i < n_events; i++) {
      // Periodically check for abort signals from the UI thread (every 2048
      // events)
      if ((i & 2047) == 0) {
        if (channel_is_tx_closed(task->trace_search_channel)) {
          aborted = true;
          break;
        }
        trace_search_msg_t abort_msg;
        if (channel_try_recv(task->trace_search_channel, &abort_msg)) {
          if (abort_msg.type == TRACE_SEARCH_MSG_ABORT) {
            aborted = true;
            break;
          }
        }
      }

      const trace_event_persisted_t* e =
          array_list_get(&td->events, const trace_event_persisted_t, i);
      string_t ph = trace_data_get_string(td, e->ph_ref);
      bool is_counter = (ph.len == 1 && ph.ptr[0] == 'C');
      bool is_metadata = (ph.len == 1 && ph.ptr[0] == 'M');

      if (is_counter && !task->include_counters) continue;
      if (!is_counter && !is_metadata && !task->include_threads) continue;

      string_t name = trace_data_get_string(td, e->name_ref);
      string_t cat = trace_data_get_string(td, e->cat_ref);

      bool match =
          trace_viewer_str_contains_case_insensitive(name, query_ptr,
                                                     query_len) ||
          trace_viewer_str_contains_case_insensitive(cat, query_ptr, query_len);

      if (!match) {
        for (uint32_t k = 0; k < e->args_count; k++) {
          const trace_arg_persisted_t* event_arg =
              &((const trace_arg_persisted_t*)td->args.ptr)[e->args_offset + k];

          if (event_arg->val_ref != 0) {
            string_t arg_val = trace_data_get_string(td, event_arg->val_ref);
            if (trace_viewer_str_contains_case_insensitive(arg_val, query_ptr,
                                                           query_len)) {
              match = true;
              break;
            }
          }
        }
      }

      if (match) {
        *array_list_push(&results, int64_t, allocator) = (int64_t)i;
      }
    }
  }

  if (aborted) {
    LOG_DEBUG("trace_search_run background task aborted");
    array_list_deinit(&results, allocator);
    channel_close_rx(task->trace_search_channel);
    app_send_trace_search_aborted(task->app_channel, (trace_data_t*)td,
                                  task->trace_search_channel, allocator);
  } else {
    LOG_DEBUG(
        "trace_search_run background task completed, generating histogram");

    // Calculate the duration histogram of the search results
    duration_histogram_t* histogram = (duration_histogram_t*)allocator_alloc(
        allocator, sizeof(duration_histogram_t));
    *histogram = (duration_histogram_t){};  // ZII
    trace_viewer_calculate_histogram(&results, td, histogram);

    // Transmit the results back to the App UI thread mailbox!
    // If sending fails, app_send_trace_search_complete automatically cleans up
    // results and histogram!
    channel_close_rx(task->trace_search_channel);
    app_send_trace_search_complete(task->app_channel, (trace_data_t*)td,
                                   results, histogram,
                                   task->trace_search_channel, allocator);
  }

  // Clean up task resources
  if (task->query) {
    allocator_free(allocator, task->query, strlen(task->query) + 1);
  }
  allocator_free(allocator, task, sizeof(trace_search_task_t));

  LOG_DEBUG("trace_search_run background task exiting");
}

channel_t* trace_search_start(const char* query, const trace_data_t* td,
                              bool include_threads, bool include_counters,
                              channel_t* app_channel, allocator_t allocator) {
  CHECK(td != nullptr);
  CHECK(app_channel != nullptr);

  channel_t* trace_search_channel =
      channel_create(trace_search_msg_t, 8, nullptr, allocator);

  trace_search_task_t* task = (trace_search_task_t*)allocator_alloc(
      allocator, sizeof(trace_search_task_t));
  task->query = nullptr;

  // Copy query string
  if (query) {
    size_t len = strlen(query) + 1;
    task->query = (char*)allocator_alloc(allocator, len);
    memcpy(task->query, query, len);
  }

  task->td = td;
  task->include_threads = include_threads;
  task->include_counters = include_counters;
  task->app_channel = app_channel;
  task->trace_search_channel = trace_search_channel;
  task->allocator = allocator;

  platform_submit_job(trace_search_run, task);

  return trace_search_channel;
}

bool trace_search_send_abort(channel_t* trace_search_channel) {
  CHECK(trace_search_channel != nullptr);
  trace_search_msg_t msg = {.type = TRACE_SEARCH_MSG_ABORT};
  return channel_try_send(trace_search_channel, &msg);
}
