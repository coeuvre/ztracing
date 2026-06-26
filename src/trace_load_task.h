#ifndef SRC_TRACE_LOAD_TASK_H
#define SRC_TRACE_LOAD_TASK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/allocator.h"
#include "core/task.h"
#include "src/trace_data.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration of the opaque loading task context
typedef struct trace_load_task trace_load_task_t;

// === 1. The Per-Chunk Payload Structure (exposed to UI via CQE user_data) ===
typedef struct {
  // Opaque parent task context pointer
  trace_load_task_t* task;
  // Raw chunk data buffer, owned by this payload.
  // Will be freed by the worker on success, or by the UI thread on
  // cancellation/failure.
  char* data;
  // Size of the raw chunk data buffer
  size_t size;
  // Cumulative bytes consumed up to this chunk (from the stream reader)
  size_t input_consumed_bytes;
  // True if this is the final EOF chunk
  bool is_eof;

  // --- Progress Metrics (written by worker, read by UI thread on CQE reap) ---
  size_t parsed_event_count;
  size_t processed_bytes;

  // --- Final Results (written by EOF worker, adopted by UI thread on CQE reap)
  // ---
  trace_data_t* completed_td;
  array_list_t completed_tracks;
  int64_t completed_min_ts;
  int64_t completed_max_ts;

  // --- Performance Telemetry (written by EOF worker on success, read by UI
  // thread) ---
  struct {
    double size_mb;
    double ingestion_duration_ms;
    double speed_mb_s;
    double starvation_ms;
    double starvation_pct;
    double organize_duration_ms;
    double total_duration_ms;
    bool ready;
  } stats;
} trace_load_task_chunk_t;

// === 2. Public Loading Task Lifecycle API ===

// Creates a new asynchronous trace loading task context.
// Returns a pointer to the opaque task context handle.
trace_load_task_t* trace_load_task_create(task_queue_t* queue,
                                          task_stream_t stream_id,
                                          allocator_t allocator);

// Prepares a chunk submission slot (SQE) for the task queue.
// Internally copies the transient input 'data' buffer into the task-local
// arena. sub: The vacant slot obtained from the queue by the caller. task: The
// active loading task context. data: The raw chunk buffer (internally copied,
// safe to free immediately after return). size: The size of the chunk buffer.
// input_consumed_bytes: Cumulative input bytes consumed.
// is_eof: True if this is the last chunk.
void trace_load_task_prep_chunk(trace_load_task_t* task, task_submission_t* sub,
                                const char* data, size_t size,
                                size_t input_consumed_bytes, bool is_eof);

// Aborts the loading task, cancelling all remaining tasks in the stream.
void trace_load_task_abort(trace_load_task_t* task);

// Releases a reference to the loading task (Shared Ownership).
// The task structure will be automatically destroyed when all background
// tasks and references finish.
void trace_load_task_release(trace_load_task_t* task);

// === 3. Flow Control & Backpressure API ===

// Returns the number of unparsed raw bytes currently buffered in-flight in the
// task queue. Thread-safe: safe to call from the UI thread to apply
// backpressure.
size_t trace_load_task_get_buffered_bytes(const trace_load_task_t* task);

// The background worker function (used by UI thread to identify CQEs)
void trace_load_task_run(task_context_t* ctx);

#ifdef __cplusplus
}
#endif

#endif  // SRC_TRACE_LOAD_TASK_H
