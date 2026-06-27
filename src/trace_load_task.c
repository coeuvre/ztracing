#include "src/trace_load_task.h"

#include <stdatomic.h>

#include "core/logging.h"
#include "core/assert.h"
#include "src/platform.h"
#include "src/track.h"

// The concrete, opaque implementation of trace_load_task_t
struct trace_load_task {
  task_queue_t* queue;      // The shared task queue
  task_stream_t stream_id;  // Serialized stream ID
  allocator_t* allocator;   // The allocator used to create this task context

  // Streaming parser state
  trace_parser_t parser;
  trace_data_t* td;
  trace_event_matcher_t matcher;

  // Progress tracking
  size_t total_discarded_bytes;

  // Shared ownership & flow control metrics
  _Atomic size_t
      active_tasks;  // Reference counter (shared between UI and worker threads)
  _Atomic size_t
      buffered_bytes;  // Total unparsed raw bytes currently in-flight

  // Performance Benchmarking Stats (thread-safe metrics)
  double
      start_time;  // Wall-clock start time of the loading session (UI thread)
  _Atomic uint64_t active_parse_time_ns;  // Total accumulated active parsing
                                          // duration in nanoseconds
};

// Background worker task (forward declared in header)
void trace_load_task_run(task_context_t* ctx) {
  trace_load_task_chunk_t* payload = (trace_load_task_chunk_t*)ctx->user_data;
  CHECK(payload != nullptr);

  trace_load_task_t* task = payload->task;
  CHECK(task != nullptr);

  size_t chunk_size = payload->size;

  double chunk_start_time = platform_get_now();

  // 1. Cooperative cancellation check
  if (task_should_abort(ctx)) {
    // Decrement buffered bytes
    atomic_fetch_sub(&task->buffered_bytes, chunk_size);
    return;
  }

  // 2. Feed the raw chunk to the streaming parser
  task->total_discarded_bytes +=
      trace_parser_feed(&task->parser, payload->data, payload->size,
                        payload->is_eof, task->allocator);

  // 3. Parse all available events in this chunk
  trace_event_t event;
  while (trace_parser_next(&task->parser, &event, task->allocator)) {
    trace_data_add_event(task->td, &event, &task->matcher, task->allocator);
  }

  // Decrement buffered bytes since this chunk is parsed and memory is freed
  atomic_fetch_sub(&task->buffered_bytes, chunk_size);

  // 4. Record progress in the payload for the CQE
  payload->parsed_event_count = task->td->events.len;
  payload->processed_bytes = task->total_discarded_bytes + task->parser.pos;

  // 5. EOF completion handling
  if (payload->is_eof) {
    double organize_start_time = platform_get_now();

    array_list_t tracks = {};
    int64_t min_ts = 0;
    int64_t max_ts = 0;
    // Run track organization pass
    allocator_t* scratch_allocator = arena_get_allocator(ctx->arena);
    track_organize(task->td, &tracks, &min_ts, &max_ts, task->allocator,
                   scratch_allocator);
    double organize_duration_ms = platform_get_now() - organize_start_time;

    double size_mb = (double)(task->total_discarded_bytes + task->parser.pos) /
                     (1024.0 * 1024.0);

    // Ingestion duration is the time from the start until parsing completed
    // (before organization began)
    double ingestion_duration_ms = organize_start_time - task->start_time;
    double ingestion_duration_s = ingestion_duration_ms / 1000.0;
    double speed_mb_s =
        ingestion_duration_s > 0.0 ? size_mb / ingestion_duration_s : 0.0;

    double total_duration_ms = platform_get_now() - task->start_time;

    // Accumulate this final chunk's parsing time (excluding track organization)
    double final_chunk_parse_ms = organize_start_time - chunk_start_time;
    uint64_t total_active_ns = atomic_load(&task->active_parse_time_ns) +
                               (uint64_t)(final_chunk_parse_ms * 1000000.0);
    double active_parse_ms = (double)total_active_ns / 1000000.0;

    // Real starvation is the idle time where the parser was waiting for chunks.
    // It excludes both active parsing time AND track organization time!
    double starvation_ms =
        total_duration_ms - (active_parse_ms + organize_duration_ms);
    if (starvation_ms < 0.0) {
      starvation_ms = 0.0;  // Clamp against clock precision variances
    }
    double starvation_pct =
        ingestion_duration_ms > 0.0
            ? (starvation_ms / ingestion_duration_ms) * 100.0
            : 0.0;

    // Populate performance stats structure in the completion payload
    payload->stats.size_mb = size_mb;
    payload->stats.ingestion_duration_ms = ingestion_duration_ms;
    payload->stats.speed_mb_s = speed_mb_s;
    payload->stats.starvation_ms = starvation_ms;
    payload->stats.starvation_pct = starvation_pct;
    payload->stats.organize_duration_ms = organize_duration_ms;
    payload->stats.total_duration_ms = total_duration_ms;
    payload->stats.ready = true;

    // Transfer ownership of parsed trace data and tracks to payload for
    // adoption
    payload->completed_td = task->td;
    payload->completed_tracks = tracks;
    payload->completed_min_ts = min_ts;
    payload->completed_max_ts = max_ts;

    // Clear task pointer to prevent double-free during task destruction
    task->td = nullptr;
  } else {
    // Accumulate active chunk parsing time
    double chunk_duration_ms = platform_get_now() - chunk_start_time;
    atomic_fetch_add(&task->active_parse_time_ns,
                     (uint64_t)(chunk_duration_ms * 1000000.0));
  }
}

// Destroys the task context (called when reference count drops to 0)
static void trace_load_task_destroy(trace_load_task_t* task) {
  // Free streaming parser
  trace_parser_deinit(&task->parser, task->allocator);

  // Free trace data if it wasn't reaped (e.g. on abort/failure)
  if (task->td != nullptr) {
    trace_data_release(task->td, task->allocator);
  }

  // Free matcher
  trace_event_matcher_deinit(&task->matcher);

  // Free the context structure itself
  allocator_free(task->allocator, task, sizeof(trace_load_task_t));
}

// Creates a new loading task context
trace_load_task_t* trace_load_task_create(task_queue_t* queue,
                                          task_stream_t stream_id,
                                          allocator_t* allocator) {
  CHECK(queue != nullptr);

  trace_load_task_t* task =
      (trace_load_task_t*)allocator_alloc(allocator, sizeof(trace_load_task_t));

  *task = (trace_load_task_t){
      .queue = queue,
      .stream_id = stream_id,
      .allocator = allocator,
  };

  // Initialize atomic reference count to 1 (owned by UI thread)
  atomic_store(&task->active_tasks, 1);
  atomic_store(&task->buffered_bytes, 0);
  atomic_store(&task->active_parse_time_ns, 0);

  task->start_time = platform_get_now();

  // Initialize streaming trace data storage (parser and matcher are
  // ZII-initialized)
  task->td = trace_data_create(allocator);

  return task;
}

// Prepares a chunk submission slot (SQE)
void trace_load_task_prep_chunk(trace_load_task_t* task, task_submission_t* sub,
                                const char* data, size_t size,
                                size_t input_consumed_bytes, bool is_eof) {
  CHECK(sub != nullptr);
  CHECK(task != nullptr);

  // Derive the allocator from the submission's arena
  allocator_t* sub_allocator = arena_get_allocator(sub->arena);

  // Allocate the chunk payload using the task-local submission allocator
  trace_load_task_chunk_t* payload = (trace_load_task_chunk_t*)allocator_alloc(
      sub_allocator, sizeof(trace_load_task_chunk_t));

  // Copy the transient input chunk data into the task-local arena
  char* arena_data = nullptr;
  if (data && size > 0) {
    arena_data = (char*)allocator_alloc(sub_allocator, size);
    memcpy(arena_data, data, size);
  }

  *payload = (trace_load_task_chunk_t){
      .task = task,
      .data = arena_data,
      .size = size,
      .input_consumed_bytes = input_consumed_bytes,
      .is_eof = is_eof,
  };

  // Prepare the SQE
  sub->task = trace_load_task_run;  // The background function to execute
  sub->user_data = payload;         // Per-chunk payload
  sub->stream = task->stream_id;    // Serialized stream ID

  // Increment active tasks reference counter for the CQE
  atomic_fetch_add(&task->active_tasks, 1);

  // Increment buffered bytes for backpressure flow control
  atomic_fetch_add(&task->buffered_bytes, size);
}

// Aborts the loading task
void trace_load_task_abort(trace_load_task_t* task) {
  if (task == nullptr) return;

  // Cancel all pending tasks for this stream in the task queue
  task_queue_cancel_stream(task->queue, task->stream_id);
}

// Releases the reference (Shared Ownership)
void trace_load_task_release(trace_load_task_t* task) {
  if (task == nullptr) return;

  // Decrement reference count atomically
  size_t prev_refs = atomic_fetch_sub(&task->active_tasks, 1);
  if (prev_refs == 1) {
    // Reference dropped to 0! Clean up the task context
    trace_load_task_destroy(task);
  }
}

// Returns buffered bytes
size_t trace_load_task_get_buffered_bytes(const trace_load_task_t* task) {
  if (task == nullptr) return 0;
  return atomic_load(&task->buffered_bytes);
}
