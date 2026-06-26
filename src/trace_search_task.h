#ifndef SRC_TRACE_SEARCH_TASK_H
#define SRC_TRACE_SEARCH_TASK_H

#include <stdbool.h>
#include <stddef.h>

#include "core/allocator.h"
#include "core/task.h"
#include "src/array_list.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct trace_data trace_data_t;
typedef struct trace_histogram trace_histogram_t;

// Transparent search task context, representing a submitted search session and
// its outputs
struct trace_search_task {
  char* query;  // Heap-allocated copy of query string
  const trace_data_t* td;
  bool include_threads;
  bool include_counters;
  allocator_t* allocator;

  // --- Outputs (written by worker on success, read by UI thread) ---
  array_list_t results;
  trace_histogram_t* histogram;
};

typedef struct trace_search_task trace_search_task_t;

// Spawns a background search task on the shared global task queue.
// Returns the task context pointer, which the UI thread stores to identify the
// active search session and trigger cancellations.
trace_search_task_t* trace_search_task_create(
    const char* query, const trace_data_t* td, bool include_threads,
    bool include_counters, task_submission_t* sub, allocator_t* allocator);

// Destroys the search task context and frees all associated memory.
// Safe to call from the UI thread on completed or cancelled search tasks.
void trace_search_task_destroy(trace_search_task_t* task);

// Opaque background worker function signature (passed to the task queue)
void trace_search_task_run(task_context_t* ctx);

#ifdef __cplusplus
}
#endif

#endif  // SRC_TRACE_SEARCH_TASK_H
