#ifndef ZTRACING_SRC_TASK_H_
#define ZTRACING_SRC_TASK_H_

#include <stdbool.h>
#include <stdint.h>

#include "src/allocator.h"
#include "src/arena.h"

#ifdef __cplusplus
extern "C" {
#endif

// ─── Core Multiplexing ───────────────────────────────────────────────────────

// task_stream_t represents an independent, sequential stream of execution.
// Stream 0 is the default parallel stream (tasks run in parallel, no
// serialization). Streams 1-4294967295 are serialized streams (tasks within the
// same stream run strictly sequentially and are implicitly linked on the same
// worker thread to preserve cache locality).
typedef uint32_t task_stream_t;

// ─── Opaque Types ────────────────────────────────────────────────────────────

typedef struct task_queue task_queue_t;

// ─── Task Execution Context ──────────────────────────────────────────────────

// The execution context passed to a running task, containing inputs and
// feedback.
typedef struct {
  // Opaque user pointer passed from the submission
  void* user_data;
  // The task-local Arena, managed and automatically cleared by the queue
  arena_t* arena;
} task_context_t;

// Returns true if the task has been requested to abort.
// Thread-safe: safe to call from the background task execution thread.
bool task_should_abort(const task_context_t* ctx);

// Marks the task as failed. This will trigger automatic cascading cancellation
// of the stream if the task belongs to a serialized stream.
// Thread-safe: safe to call from the background task execution thread.
void task_set_failed(task_context_t* ctx);

// ─── Function Pointers ───────────────────────────────────────────────────────

// The background task execution signature.
typedef void (*task_t)(task_context_t* ctx);

// The executor callback used to dispatch work to an abstract thread pool.
typedef void (*task_executor_t)(void (*work_fn)(void*), void* arg);

// ─── The Submission Entry (SQE) ──────────────────────────────────────────────

// Represents the input request. Allocated in the SQ, filled by the caller.
typedef struct {
  // The background function to execute
  task_t task;
  // Opaque user pointer passed to task(), and returned in completion
  void* user_data;
  // Multiplexing Stream ID.
  // - stream = 0: Default parallel stream. Tasks run in parallel, are fully
  //   independent, and failures/cancellations are isolated to that task.
  // - stream > 0: Serialized sequential stream. Tasks in the same stream run
  //   strictly sequentially, are implicitly linked on the same worker thread
  //   to preserve cache locality, and share a strict dependency chain:
  //     1. Cancellation: Cancelling any submitted task in a serialized stream
  //        automatically cascades, cancelling the entire stream (all other
  //        submitted tasks in that stream, active or pending).
  //     2. Error Handling: If a task signals failure (sets ctx->failed = true),
  //        the queue engine automatically aborts all subsequent pending tasks
  //        in that stream to preserve state safety.
  task_stream_t stream;
} task_submission_t;

// ─── Status Types ────────────────────────────────────────────────────────────

// Represents the final execution status of a completed task
typedef enum {
  // Task completed successfully
  TASK_STATUS_OK = 0,
  // Task failed (set ctx->failed = true), which automatically aborted the
  // stream
  TASK_STATUS_FAILED,
  // Task was cancelled (UI request, stream cancel, or cascaded abort)
  TASK_STATUS_CANCELLED,
} task_status_t;

// ─── The Completion Entry (CQE) ──────────────────────────────────────────────

// Represents the output result. Allocated in the CQ, read by the caller.
typedef struct {
  // Opaque user pointer copied from the submission
  void* user_data;
  // The final execution status of the task
  task_status_t status;
} task_completion_t;

// ─── Public API: Lifecycle ───────────────────────────────────────────────────

// Creates a new task queue with the specified capacity (number of pre-allocated
// slots). Dispatches background work using the provided abstract executor.
task_queue_t* task_queue_create(size_t cap, task_executor_t executor,
                                allocator_t allocator);
void task_queue_destroy(task_queue_t* queue);

// Cancels all pending submissions for a specific stream.
// Any pending tasks in the queue for this stream will be aborted and completed
// immediately with status set to TASK_STATUS_CANCELLED, without executing
// their task functions.
void task_queue_cancel_stream(task_queue_t* queue, task_stream_t stream);

// Cancels a specific submission identified by its unique user_data pointer.
// Works for both stream and non-stream (stream = 0) submissions.
// Any matching pending task will be aborted and completed immediately with
// status set to TASK_STATUS_CANCELLED. If the task is already executing, its
// 'should_abort' flag will be set to true.
// Note: If the cancelled submission belongs to a serialized stream (stream >
// 0), the cancellation will cascade, automatically aborting all other
// submitted tasks in that stream (both active and pending) to preserve
// sequential state safety.
// Note: Only submissions that have been submitted via task_queue_submit()
// can be cancelled. Unsubmitted ("pending") submissions in the SQ are not
// managed by the system and cannot be cancelled.
void task_queue_cancel_submission(task_queue_t* queue, void* user_data);

// ─── Thread Safety Warning ───────────────────────────────────────────────────

// Accessing the Submission Queue (SQ) and Completion Queue (CQ) APIs is NOT
// thread-safe. These functions must only be called from the owning thread
// (typically the main/caller thread). If multiple threads need to access the
// same queue, SQ/CQ operations must be synchronized externally (e.g., using a
// mutex).

// ─── Submission Queue (SQ) ───────────────────────────────────────────────────

// Gets a pointer to the next vacant task_submission_t slot in the SQ to prepare
// a task. Callers can call this function multiple times in succession to
// prepare a batch of tasks before calling task_queue_submit() to flush them all
// at once. The prepared tasks will not start executing until
// task_queue_submit() is called. Returns NULL if the SQ is full.
task_submission_t* task_queue_get_submission(task_queue_t* queue);

// Submits all prepared task_submission_t slots in the SQ to the background
// worker pool. This flushes all prepared tasks in a single batch, triggering
// their execution.
void task_queue_submit(task_queue_t* queue);

// ─── Completion Queue (CQ) ───────────────────────────────────────────────────

// Peeks the oldest completed task_completion_t from the CQ.
// Does NOT remove the completion from the queue.
// Returns true if a completion is available, false otherwise.
bool task_queue_peek_completion(task_queue_t* queue,
                                task_completion_t* out_completed);

// Blocks the calling thread until at least one completion is available in the
// CQ. Once available, peeks the oldest completion into out_completed. Does NOT
// remove the completion from the queue.
void task_queue_wait_completion(task_queue_t* queue,
                                task_completion_t* out_completed);

// Blocks the calling thread until at least one completion is available in the
// CQ, or until the specified timeout (in nanoseconds) expires. Returns true if
// a completion is available (written to out_completed), or false if the timeout
// expired. Does NOT remove the completion from the queue.
bool task_queue_wait_completion_timeout(task_queue_t* queue,
                                        task_completion_t* out_completed,
                                        uint64_t timeout_ns);

// Removes the oldest completed task from the CQ, freeing its slot.
// MUST be called after processing a peeked completion to release the resources.
void task_queue_remove_completion(task_queue_t* queue);

#ifdef __cplusplus
}
#endif

#endif  // ZTRACING_SRC_TASK_H_
