#include "src/trace_search_task.h"

#include <stdbool.h>
#include <string.h>

#include "core/assert.h"
#include "core/logging.h"
#include "core/task.h"
#include "src/trace_data.h"
#include "src/trace_histogram.h"
#include "src/trace_viewer.h"

// Background worker thread function (conforms to task_t signature)
void trace_search_task_run(task_context_t* ctx) {
  trace_search_task_t* task = (trace_search_task_t*)ctx->user_data;
  CHECK(task != nullptr);

  allocator_t allocator = task->allocator;
  const trace_data_t* td = task->td;

  LOG_DEBUG("trace_search_task_run background task started (query: '%s')",
            task->query ? task->query : "");

  array_list_t results = {};  // ZII
  bool aborted = false;

  if (task->query && task->query[0] != '\0') {
    const char* query_ptr = task->query;
    size_t query_len = strlen(query_ptr);
    size_t n_events = td->events.len;

    for (size_t i = 0; i < n_events; i++) {
      // Periodically check for abort signals from the Task Queue (every 2048
      // events)
      if ((i & 2047) == 0) {
        if (task_should_abort(ctx)) {
          aborted = true;
          break;
        }
      }

      const trace_event_persisted_t* e =
          array_list_get(&td->events, const trace_event_persisted_t, i);
      string_view_t ph = trace_data_get_string(td, e->ph_ref);
      bool is_counter = (ph.len == 1 && ph.ptr[0] == 'C');
      bool is_metadata = (ph.len == 1 && ph.ptr[0] == 'M');

      if (is_counter && !task->include_counters) continue;
      if (!is_counter && !is_metadata && !task->include_threads) continue;

      string_view_t name = trace_data_get_string(td, e->name_ref);
      string_view_t cat = trace_data_get_string(td, e->cat_ref);

      bool match =
          trace_viewer_str_contains_case_insensitive(name, query_ptr,
                                                     query_len) ||
          trace_viewer_str_contains_case_insensitive(cat, query_ptr, query_len);

      if (!match) {
        for (uint32_t k = 0; k < e->args_count; k++) {
          const trace_arg_persisted_t* event_arg =
              &((const trace_arg_persisted_t*)td->args.ptr)[e->args_offset + k];

          if (event_arg->val_ref != 0) {
            string_view_t arg_val =
                trace_data_get_string(td, event_arg->val_ref);
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
    LOG_DEBUG("trace_search_task_run background task aborted");
    array_list_deinit(&results, allocator);
  } else {
    LOG_DEBUG(
        "trace_search_task_run background task completed, generating "
        "histogram");

    // Calculate the duration histogram of the search results
    trace_histogram_t* histogram = (trace_histogram_t*)allocator_alloc(
        allocator, sizeof(trace_histogram_t));
    *histogram = (trace_histogram_t){};  // ZII
    trace_histogram_compute(&results, td, histogram);

    // Save outputs to the task context to be adopted by the UI thread
    task->results = results;
    task->histogram = histogram;
  }

  LOG_DEBUG("trace_search_task_run background task exiting");
}

trace_search_task_t* trace_search_task_create(
    const char* query, const trace_data_t* td, bool include_threads,
    bool include_counters, task_submission_t* sub, allocator_t allocator) {
  CHECK(td != nullptr);
  CHECK(sub != nullptr);

  // Derive the allocator from the submission's arena
  allocator_t sub_allocator = arena_get_allocator(sub->arena);

  // 1. Allocate the task context from the task-local submission allocator
  trace_search_task_t* task = (trace_search_task_t*)allocator_alloc(
      sub_allocator, sizeof(trace_search_task_t));
  *task = (trace_search_task_t){
      .td = td,
      .include_threads = include_threads,
      .include_counters = include_counters,
      .allocator = allocator,
  };

  // 2. Copy query string into the same task-local arena
  if (query) {
    size_t len = strlen(query) + 1;
    task->query = (char*)allocator_alloc(sub_allocator, len);
    memcpy(task->query, query, len);
  }

  // 3. Prepare the SQE
  sub->task = trace_search_task_run;
  sub->user_data = task;
  sub->stream = 2;  // Stream 2 for serialized search execution

  return task;
}

void trace_search_task_destroy(trace_search_task_t* task) {
  if (!task) return;
  allocator_t a = task->allocator;
  array_list_deinit(&task->results, a);
  if (task->histogram) {
    allocator_free(a, task->histogram, sizeof(trace_histogram_t));
  }
  // Note: task itself and task->query are allocated from the task-local arena
  // and will be automatically reclaimed when task_queue_remove_completion is
  // called.
}
