#define _POSIX_C_SOURCE 200809L
#include "core/task.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <time.h>

#include "core/allocator.h"
#include "core/arena.h"
#include "core/assert.h"
#include "core/logging.h"

// ─── Internal Structures ─────────────────────────────────────────────────────

// Represents a node in the internal pending list.
// Decouples the Submission Queue (SQ) from the dispatching engine,
// allowing out-of-order dispatching to resolve Head-of-Line blocking.
typedef struct task_node {
  // The carried task submission data
  task_submission_t sub;
  // The arena carrying the task's inputs/scratch
  arena_t* arena;
  // Link to the next node in the list
  struct task_node* next;
  // True if this task was cancelled while pending in the queue
  bool cancelled;
} task_node_t;

// Represents an active, stateful execution context in the background engine.
typedef struct {
  // The task function pointer to run
  task_t task;
  // Opaque user pointer carried through
  void* user_data;
  // Multiplexing Stream ID
  task_stream_t stream;
  // The task-local scratch Arena, pre-allocated and cleared between runs
  arena_t* arena;
  // The final execution status
  task_status_t status;
  // True if the context is currently leased by an executing task
  bool active;
  // True if the task was requested to abort
  _Atomic bool cancelled;
  // True if the task has completed execution and its status is committed
  bool completed;
} task_execution_t;

typedef struct {
  task_context_t public_ctx;
  task_execution_t* exec;
  bool failed;  // Private failure flag
} task_context_internal_t;

// The concrete implementation of the opaque task_queue_t.
struct task_queue {
  // Staging array for submissions (the physical SQ)
  task_submission_t* sq_entries;
  // Arenas associated with each SQ slot
  arena_t** sq_arenas;
  // Arenas associated with each CQ slot
  arena_t** cq_arenas;
  // Parallel array to track cancellation status of SQ entries
  bool* sq_cancelled;
  // Array of completed entries ready for reaping (the physical CQ)
  task_completion_t* cq_entries;
  // The internal stateful execution contexts (the runtime engine)
  task_execution_t* executions;
  // Total capacity (slots) of the queues and execution arrays
  size_t cap;
  // The allocator used to provision the queue
  allocator_t* allocator;
  // The abstract executor callback used to dispatch background work
  task_executor_t executor;
  // Read pointer for the circular SQ
  size_t sq_head;
  // Committed write pointer for the circular SQ
  size_t sq_sub_tail;
  // Prepared write pointer for the circular SQ (user-leased tasks)
  size_t sq_tail;
  // Read pointer for the circular CQ
  size_t cq_head;
  // Write pointer for the circular CQ
  size_t cq_tail;
  // The global synchronization lock protecting all state transitions
  pthread_mutex_t mutex;
  // Condition variable to wake up the reader when completions are posted
  pthread_cond_t cond_reap;
  // Condition variable to wake up worker threads when CQ space is freed
  pthread_cond_t cond_space;

  // Pre-allocated pool of nodes to avoid dynamic allocation during execution
  task_node_t* node_pool;
  // Singly-linked list of free/vacant nodes in the pool
  task_node_t* free_nodes;
  // Head of the global pending list (FIFO)
  task_node_t* pending_head;
  // Tail of the global pending list (for O(1) appends)
  task_node_t* pending_tail;
  // The thread assumed to be reaping completions (for deadlock detection)
  pthread_t owner_thread;
};

// ─── Private Helper Declarations ─────────────────────────────────────────────

static void task_worker(void* arg);
static bool has_active_stream_locked(task_queue_t* queue, task_stream_t stream,
                                     size_t exclude_exec_idx);
static bool post_completion_locked(task_queue_t* queue, task_t task,
                                   void* user_data, task_status_t status,
                                   arena_t** arena);
static void dispatch_pending_locked(task_queue_t* queue);
static void cancel_stream_locked(task_queue_t* queue, task_stream_t stream);
static bool pending_list_push_locked(task_queue_t* queue,
                                     const task_submission_t* sub,
                                     arena_t** arena, bool cancelled);

// ─── Public API: Lifecycle ───────────────────────────────────────────────────

task_queue_t* task_queue_create(size_t cap, task_executor_t executor,
                                allocator_t* allocator) {
  CHECK(cap > 0);
  CHECK(executor != nullptr);

  // Allocate the queue structure itself (never returns NULL, aborts on OOM)
  task_queue_t* queue = allocator_alloc(allocator, sizeof(task_queue_t));

  // Zero-initialize the structure (ZII)
  *queue = (task_queue_t){
      .cap = cap,
      .allocator = allocator,
      .executor = executor,
      .owner_thread = pthread_self(),
  };

  // Allocate the circular ring buffers and node pool (never return NULL, abort
  // on OOM)
  queue->sq_entries =
      allocator_alloc(queue->allocator, sizeof(task_submission_t) * cap);
  queue->sq_arenas = allocator_alloc(queue->allocator, sizeof(arena_t*) * cap);
  queue->cq_arenas = allocator_alloc(queue->allocator, sizeof(arena_t*) * cap);
  queue->sq_cancelled = allocator_alloc(queue->allocator, sizeof(bool) * cap);
  queue->cq_entries =
      allocator_alloc(queue->allocator, sizeof(task_completion_t) * cap);
  queue->executions =
      allocator_alloc(queue->allocator, sizeof(task_execution_t) * cap);
  queue->node_pool =
      allocator_alloc(queue->allocator, sizeof(task_node_t) * cap);

  // Initialize the node pool as a free list of vacant nodes
  queue->free_nodes = &queue->node_pool[0];
  for (size_t i = 0; i < cap - 1; ++i) {
    queue->node_pool[i].next = &queue->node_pool[i + 1];
  }
  queue->node_pool[cap - 1].next = nullptr;

  // Initialize the internal execution contexts and their task-local Arenas
  for (size_t i = 0; i < cap; ++i) {
    queue->sq_entries[i] = (task_submission_t){};
    queue->sq_arenas[i] = nullptr;
    queue->cq_arenas[i] = nullptr;
    queue->sq_cancelled[i] = false;
    queue->cq_entries[i] = (task_completion_t){};
    queue->executions[i] = (task_execution_t){};
  }

  // Initialize the synchronization mutex (fail-fast via native CHECK)
  CHECK(pthread_mutex_init(&queue->mutex, nullptr) == 0);

  // Initialize the condition variables (fail-fast via native CHECK)
  CHECK(pthread_cond_init(&queue->cond_reap, nullptr) == 0);
  CHECK(pthread_cond_init(&queue->cond_space, nullptr) == 0);

  return queue;
}

void task_queue_destroy(task_queue_t* queue) {
  CHECK(pthread_equal(pthread_self(), queue->owner_thread));

  // Mutex and conds are destroyed first, failing fast on any error via native
  // CHECK
  CHECK(pthread_mutex_destroy(&queue->mutex) == 0);
  CHECK(pthread_cond_destroy(&queue->cond_reap) == 0);
  CHECK(pthread_cond_destroy(&queue->cond_space) == 0);

  // 1. Destroy all arenas in SQ and CQ
  for (size_t i = 0; i < queue->cap; ++i) {
    if (queue->sq_arenas[i] != nullptr) {
      arena_destroy(queue->sq_arenas[i]);
    }
    if (queue->cq_arenas[i] != nullptr) {
      arena_destroy(queue->cq_arenas[i]);
    }
  }
  // 2. Destroy all arenas in pending list
  task_node_t* curr_node = queue->pending_head;
  while (curr_node) {
    if (curr_node->arena != nullptr) {
      arena_destroy(curr_node->arena);
    }
    curr_node = curr_node->next;
  }
  // 3. Destroy all arenas in executions
  for (size_t i = 0; i < queue->cap; ++i) {
    if (queue->executions[i].arena != nullptr) {
      arena_destroy(queue->executions[i].arena);
    }
  }

  // Free the node pool, ring buffers, and the queue structure itself
  allocator_free(queue->allocator, queue->node_pool,
                 sizeof(task_node_t) * queue->cap);
  allocator_free(queue->allocator, queue->executions,
                 sizeof(task_execution_t) * queue->cap);
  allocator_free(queue->allocator, queue->sq_entries,
                 sizeof(task_submission_t) * queue->cap);
  allocator_free(queue->allocator, queue->sq_arenas,
                 sizeof(arena_t*) * queue->cap);
  allocator_free(queue->allocator, queue->cq_arenas,
                 sizeof(arena_t*) * queue->cap);
  allocator_free(queue->allocator, queue->sq_cancelled,
                 sizeof(bool) * queue->cap);
  allocator_free(queue->allocator, queue->cq_entries,
                 sizeof(task_completion_t) * queue->cap);
  allocator_free(queue->allocator, queue, sizeof(task_queue_t));
}

// ─── Public API: Cancellation ────────────────────────────────────────────────

void task_queue_cancel_stream(task_queue_t* queue, task_stream_t stream) {
  if (stream == 0) return;

  CHECK(pthread_mutex_lock(&queue->mutex) == 0);
  // Perform the cancellation while holding the lock to ensure safety
  cancel_stream_locked(queue, stream);
  CHECK(pthread_mutex_unlock(&queue->mutex) == 0);
}

void task_queue_cancel_submission(task_queue_t* queue, void* user_data) {
  if (!user_data) return;

  CHECK(pthread_mutex_lock(&queue->mutex) == 0);

  task_stream_t cascaded_stream = 0;

  // 1. Check the SQ for a matching submission (only up to sq_sub_tail!)
  // We never scan unsubmitted entries past sq_sub_tail to respect boundaries
  // and prevent data races with concurrent lock-free preparation.
  size_t curr_sq = queue->sq_head;
  while (curr_sq != queue->sq_sub_tail) {
    task_submission_t* sub = &queue->sq_entries[curr_sq % queue->cap];
    if (sub->user_data == user_data) {
      cascaded_stream = sub->stream;
      queue->sq_cancelled[curr_sq % queue->cap] = true;
      break;
    }
    curr_sq++;
  }

  // 1b. Check the global PENDING LIST for a matching submission
  if (cascaded_stream == 0) {
    task_node_t* curr_node = queue->pending_head;
    while (curr_node) {
      if (curr_node->sub.user_data == user_data) {
        cascaded_stream = curr_node->sub.stream;
        curr_node->cancelled = true;
        break;
      }
      curr_node = curr_node->next;
    }
  }

  // 2. Check the active execution contexts for a matching running task
  if (cascaded_stream == 0) {
    for (size_t i = 0; i < queue->cap; ++i) {
      task_execution_t* exec = &queue->executions[i];
      if (exec->active && !exec->completed && exec->user_data == user_data) {
        cascaded_stream = exec->stream;
        exec->cancelled = true;
        exec->status = TASK_STATUS_CANCELLED;
        break;
      }
    }
  }

  // 3. Cascading Cancellation: If it belongs to a serialized stream, abort the
  // rest!
  if (cascaded_stream > 0) {
    cancel_stream_locked(queue, cascaded_stream);
  }

  CHECK(pthread_mutex_unlock(&queue->mutex) == 0);
}

// ─── Submission Queue (SQ) ───────────────────────────────────────────────────

task_submission_t* task_queue_get_submission(task_queue_t* queue) {
  CHECK(pthread_equal(pthread_self(), queue->owner_thread));

  CHECK(pthread_mutex_lock(&queue->mutex) == 0);

  // Check if the staging SQ has space based on the prepared tail pointer!
  size_t prepared = queue->sq_tail - queue->sq_head;
  if (prepared >= queue->cap) {
    CHECK(pthread_mutex_unlock(&queue->mutex) == 0);
    return nullptr;
  }

  // Return the next vacant staging slot and instantly increment the prepared
  // tail!
  size_t idx = queue->sq_tail % queue->cap;
  task_submission_t* sub = &queue->sq_entries[idx];
  queue->sq_tail++;

  // Clean the slot before leasing and associate a fresh task-local arena
  *sub = (task_submission_t){};
  queue->sq_arenas[idx] = arena_create_with_allocator(queue->allocator);
  sub->arena = queue->sq_arenas[idx];

  CHECK(pthread_mutex_unlock(&queue->mutex) == 0);
  return sub;
}

void task_queue_submit(task_queue_t* queue) {
  CHECK(pthread_equal(pthread_self(), queue->owner_thread));

  CHECK(pthread_mutex_lock(&queue->mutex) == 0);

  // Flush-commit all prepared tasks by advancing the committed pointer!
  queue->sq_sub_tail = queue->sq_tail;

  // Dispatch will automatically flush the SQ to the pending list first
  dispatch_pending_locked(queue);

  CHECK(pthread_mutex_unlock(&queue->mutex) == 0);
}

// ─── Completion Queue (CQ) ───────────────────────────────────────────────────

bool task_queue_peek_completion(task_queue_t* queue,
                                task_completion_t* out_completed) {
  if (!out_completed) return false;

  CHECK(pthread_equal(pthread_self(), queue->owner_thread));

  CHECK(pthread_mutex_lock(&queue->mutex) == 0);

  if (queue->cq_head == queue->cq_tail) {
    CHECK(pthread_mutex_unlock(&queue->mutex) == 0);
    return false;
  }

  // Peek the oldest completed entry
  *out_completed = queue->cq_entries[queue->cq_head % queue->cap];

  CHECK(pthread_mutex_unlock(&queue->mutex) == 0);
  return true;
}

void task_queue_wait_completion(task_queue_t* queue,
                                task_completion_t* out_completed) {
  CHECK(pthread_equal(pthread_self(), queue->owner_thread));

  CHECK(pthread_mutex_lock(&queue->mutex) == 0);

  // Block the calling thread (CPU-free sleep) while the CQ is completely empty
  while (queue->cq_head == queue->cq_tail) {
    CHECK(pthread_cond_wait(&queue->cond_reap, &queue->mutex) == 0);
  }

  // Peek the oldest completed entry
  if (out_completed) {
    *out_completed = queue->cq_entries[queue->cq_head % queue->cap];
  }

  CHECK(pthread_mutex_unlock(&queue->mutex) == 0);
}

bool task_queue_wait_completion_timeout(task_queue_t* queue,
                                        task_completion_t* out_completed,
                                        uint64_t timeout_ns) {
  CHECK(pthread_equal(pthread_self(), queue->owner_thread));

  CHECK(pthread_mutex_lock(&queue->mutex) == 0);

  // 1. Calculate absolute target timespec with native nanosecond precision
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);

  ts.tv_sec += (time_t)(timeout_ns / 1000000000ULL);
  ts.tv_nsec += (long)(timeout_ns % 1000000000ULL);
  if (ts.tv_nsec >= 1000000000LL) {
    ts.tv_sec += 1;
    ts.tv_nsec -= 1000000000LL;
  }

  // 2. Sleep on the condition variable until signaled or timeout expires
  int rc = 0;
  while (queue->cq_head == queue->cq_tail && rc == 0) {
    rc = pthread_cond_timedwait(&queue->cond_reap, &queue->mutex, &ts);
    // Crash immediately on any real error, but allow ETIMEDOUT as a valid
    // result
    CHECK(rc == 0 || rc == ETIMEDOUT);
  }

  // 3. Check if we actually have a completion
  bool success = (queue->cq_head != queue->cq_tail);
  if (success && out_completed) {
    *out_completed = queue->cq_entries[queue->cq_head % queue->cap];
  }

  CHECK(pthread_mutex_unlock(&queue->mutex) == 0);
  return success;
}

void task_queue_remove_completion(task_queue_t* queue) {
  CHECK(pthread_equal(pthread_self(), queue->owner_thread));

  CHECK(pthread_mutex_lock(&queue->mutex) == 0);

  if (queue->cq_head < queue->cq_tail) {
    size_t idx = queue->cq_head % queue->cap;
    if (queue->cq_arenas[idx] != nullptr) {
      arena_destroy(queue->cq_arenas[idx]);
      queue->cq_arenas[idx] = nullptr;
    }
    queue->cq_head++;
    // Signal any worker threads blocked waiting for space in the CQ!
    CHECK(pthread_cond_signal(&queue->cond_space) == 0);
  }

  CHECK(pthread_mutex_unlock(&queue->mutex) == 0);
}

// ─── Private Helper Implementations ──────────────────────────────────────────

static void task_worker(void* arg) {
  struct {
    task_queue_t* queue;
    size_t exec_idx;
  }* payload = arg;

  task_queue_t* queue = payload->queue;
  size_t exec_idx = payload->exec_idx;

  CHECK(pthread_mutex_lock(&queue->mutex) == 0);
  task_execution_t* exec = &queue->executions[exec_idx];
  CHECK(pthread_mutex_unlock(&queue->mutex) == 0);

  while (true) {
    // 1. Build the task context (Arena is guaranteed to be empty/reset)
    task_context_internal_t internal_ctx = {
        .public_ctx =
            {
                .user_data = exec->user_data,
                .arena = exec->arena,
            },
        .exec = exec,
        .failed = false,
    };

    // 2. Execute the task (outside the global mutex lock!)
    if (!atomic_load(&exec->cancelled)) {
      exec->task(&internal_ctx.public_ctx);
    }

    // 3. Lock the mutex to commit the result and check for next streams
    CHECK(pthread_mutex_lock(&queue->mutex) == 0);

    // Update status based on task context feedback.
    // Cancellation takes precedence over failure.
    if (exec->cancelled) {
      exec->status = TASK_STATUS_CANCELLED;
    } else if (internal_ctx.failed) {
      exec->status = TASK_STATUS_FAILED;
    } else {
      exec->status = TASK_STATUS_OK;
    }
    exec->completed = true;

    // Write the completion (CQE) into the circular CQ.
    // If the CQ is completely full, the worker thread MUST block and wait for
    // space!
    while (queue->cq_tail - queue->cq_head >= queue->cap) {
      if (pthread_equal(pthread_self(), queue->owner_thread)) {
        LOG_ERROR(
            "Deadlock detected: synchronous task execution blocked on full CQ "
            "(cap %zu). You must reap completions "
            "(task_queue_remove_completion) to free space.",
            queue->cap);
        CHECK(false && "Proactor deadlock: sync task blocked on full CQ");
      }
      CHECK(pthread_cond_wait(&queue->cond_space, &queue->mutex) == 0);
    }

    // Post the completion to the CQ (guaranteed to succeed since we blocked for
    // space!)
    // Note: This transfers ownership of exec->arena to the CQ slot, and clears
    // exec->arena
    bool posted = post_completion_locked(queue, exec->task, exec->user_data,
                                         exec->status, &exec->arena);
    CHECK(posted);

    // Wake up any threads blocked in task_queue_wait_completion
    CHECK(pthread_cond_signal(&queue->cond_reap) == 0);

    task_stream_t stream = exec->stream;
    task_status_t final_status = exec->status;

    // Mark the current execution context as vacant
    exec->active = false;

    // 5. Cascading Failures / Aborts
    // We call the locked helper directly to avoid dropping the lock and opening
    // a race window.
    // Only cancel the stream on actual task FAILURE. If the task was CANCELLED,
    // the stream cancellation has already been processed at the time of the
    // cancellation request, and calling it again here would incorrectly cancel
    // new tasks submitted after the cancellation request.
    if (stream > 0 && final_status == TASK_STATUS_FAILED) {
      cancel_stream_locked(queue, stream);
    }

    // 6. Persistent Worker Loop: Drain the pending list in-place on the current
    // thread!
    task_node_t* next_node = nullptr;
    task_node_t* prev_node = nullptr;

    // Pass 1: Stream Affinity (prioritize tasks with the same stream ID for
    // L1/L2 cache warmth)
    if (stream > 0) {
      task_node_t* curr = queue->pending_head;
      task_node_t* prev = nullptr;
      while (curr) {
        if (curr->sub.task && curr->sub.stream == stream) {
          next_node = curr;
          prev_node = prev;
          break;
        }
        prev = curr;
        curr = curr->next;
      }
    }

    // Pass 2: FIFO Fallback (if no same-stream task is found, pick the oldest
    // eligible task)
    if (!next_node) {
      task_node_t* curr = queue->pending_head;
      task_node_t* prev = nullptr;
      while (curr) {
        if (curr->sub.task) {
          if (curr->sub.stream == 0 ||
              !has_active_stream_locked(queue, curr->sub.stream, exec_idx)) {
            next_node = curr;
            prev_node = prev;
            break;
          }
        }
        prev = curr;
        curr = curr->next;
      }
    }

    if (next_node) {
      // Found an eligible task! Lease this context again for it.
      *exec = (task_execution_t){
          .task = next_node->sub.task,
          .user_data = next_node->sub.user_data,
          .stream = next_node->sub.stream,
          .arena = next_node->arena,  // Transfer the pointer!
          .status = TASK_STATUS_OK,
          .active = true,
          .cancelled = next_node->cancelled,
      };
      next_node->arena = nullptr;  // Clear pointer!

      // Remove the node from the pending list
      task_node_t* next_next = next_node->next;
      if (prev_node) {
        prev_node->next = next_next;
      } else {
        queue->pending_head = next_next;
      }
      if (next_node == queue->pending_tail) {
        queue->pending_tail = prev_node;
      }

      // Return the node to the free pool
      next_node->next = queue->free_nodes;
      queue->free_nodes = next_node;

      // Trigger flushing/dispatching since we just freed a node!
      dispatch_pending_locked(queue);

      // Loop and execute immediately on the same thread!
      CHECK(pthread_mutex_unlock(&queue->mutex) == 0);
      continue;
    }

    // No more eligible tasks. We are about to exit the thread and free our
    // execution slot. Automatically trigger dispatching of any pending tasks
    // waiting in the SQ/pending list!
    dispatch_pending_locked(queue);

    CHECK(pthread_mutex_unlock(&queue->mutex) == 0);
    break;
  }
}

static bool has_active_stream_locked(task_queue_t* queue, task_stream_t stream,
                                     size_t exclude_exec_idx) {
  // Assumes queue->mutex is LOCKED on entry!
  for (size_t i = 0; i < queue->cap; ++i) {
    if (i == exclude_exec_idx) continue;
    if (queue->executions[i].active && queue->executions[i].stream == stream) {
      return true;
    }
  }
  return false;
}

static bool post_completion_locked(task_queue_t* queue, task_t task,
                                   void* user_data, task_status_t status,
                                   arena_t** arena) {
  // Assumes queue->mutex is LOCKED on entry!
  size_t cq_next = queue->cq_tail;
  if (cq_next - queue->cq_head < queue->cap) {
    size_t idx = cq_next % queue->cap;
    queue->cq_entries[idx] = (task_completion_t){
        .task = task,
        .user_data = user_data,
        .status = status,
    };
    queue->cq_arenas[idx] = *arena;  // Transfer pointer!
    *arena = nullptr;                // Clear pointer!
    queue->cq_tail++;
    return true;
  }
  return false;
}

static void dispatch_pending_locked(task_queue_t* queue) {
  // Assumes queue->mutex is LOCKED on entry!

  // We scan the global pending list. Because calling the executor requires
  // unlocking the mutex, any active iterator would be invalidated. To prevent
  // use-after-free and double-dispatch bugs (especially under synchronous
  // executors), we restart the scan fresh from the pending_head after every
  // dispatch.
  while (true) {
    // 1. Flush SQ to pending list as much as possible using any available free
    // nodes. This ensures that deferred tasks are immediately recovered as soon
    // as nodes are freed, preventing starvation of independent streams.
    while (queue->sq_head < queue->sq_sub_tail) {
      size_t sq_idx = queue->sq_head % queue->cap;
      task_submission_t* sub = &queue->sq_entries[sq_idx];
      if (sub->task) {
        bool cancelled = queue->sq_cancelled[sq_idx];
        if (!pending_list_push_locked(queue, sub, &queue->sq_arenas[sq_idx],
                                      cancelled)) {
          break;
        }
        queue->sq_cancelled[sq_idx] = false;  // Reset!
        *sub = (task_submission_t){};
      }
      queue->sq_head++;
    }

    task_node_t* curr = queue->pending_head;
    task_node_t* prev = nullptr;
    task_node_t* target_node = nullptr;
    task_node_t* target_prev = nullptr;

    // Find a vacant internal execution context in the engine
    task_execution_t* target_exec = nullptr;
    size_t target_idx = 0;
    for (size_t i = 0; i < queue->cap; ++i) {
      if (!queue->executions[i].active) {
        target_exec = &queue->executions[i];
        target_idx = i;
        break;
      }
    }

    if (!target_exec) {
      // The internal execution ring is completely full. Stop dispatching.
      break;
    }

    // Find the oldest eligible task in the pending list.
    // If a task belongs to an active serialized stream, we skip it and continue
    // scanning to resolve Head-of-Line blocking!
    while (curr) {
      if (curr->sub.stream == 0 ||
          !has_active_stream_locked(queue, curr->sub.stream, target_idx)) {
        target_node = curr;
        target_prev = prev;
        break;
      }
      prev = curr;
      curr = curr->next;
    }

    if (!target_node) {
      // No eligible tasks found in the pending list.
      break;
    }

    // Lease the execution context and copy the submission data (kernel-copy
    // style)
    *target_exec = (task_execution_t){
        .task = target_node->sub.task,
        .user_data = target_node->sub.user_data,
        .stream = target_node->sub.stream,
        .arena = target_node->arena,  // Transfer pointer!
        .status = TASK_STATUS_OK,
        .active = true,
        .cancelled = target_node->cancelled,
    };
    target_node->arena = nullptr;  // Clear pointer!

    // Pack the execution payload using the task's own scratch Arena
    typedef struct {
      task_queue_t* queue;
      size_t exec_idx;
    } job_payload_t;

    job_payload_t* payload = allocator_alloc(
        arena_get_allocator(target_exec->arena), sizeof(job_payload_t));
    *payload = (job_payload_t){
        .queue = queue,
        .exec_idx = target_idx,
    };

    // Remove the node from the pending list
    task_node_t* next_node = target_node->next;
    if (target_prev) {
      target_prev->next = next_node;
    } else {
      queue->pending_head = next_node;
    }
    if (target_node == queue->pending_tail) {
      queue->pending_tail = target_prev;
    }

    // Return the node to the free pool
    target_node->next = queue->free_nodes;
    queue->free_nodes = target_node;

    // Release lock BEFORE calling the external executor callback!
    CHECK(pthread_mutex_unlock(&queue->mutex) == 0);

    // Dispatch execution using the abstract injected executor!
    queue->executor(task_worker, payload);

    // Re-acquire lock to continue the dispatch loop safely!
    CHECK(pthread_mutex_lock(&queue->mutex) == 0);
  }
}

static void cancel_stream_locked(task_queue_t* queue, task_stream_t stream) {
  // Assumes queue->mutex is LOCKED on entry!
  if (stream == 0) return;

  // 1. Abort all pending submissions in the SQ matching the stream.
  // We ONLY scan up to sq_sub_tail (committed submissions) to strictly respect
  // the boundary of unsubmitted entries and avoid data races.
  size_t curr_sq = queue->sq_head;
  while (curr_sq != queue->sq_sub_tail) {
    task_submission_t* sub = &queue->sq_entries[curr_sq % queue->cap];
    if (sub->stream == stream) {
      queue->sq_cancelled[curr_sq % queue->cap] = true;
    }
    curr_sq++;
  }

  // 1b. Abort all pending tasks in the global pending list matching the stream
  task_node_t* curr_node = queue->pending_head;
  while (curr_node) {
    if (curr_node->sub.stream == stream) {
      curr_node->cancelled = true;
    }
    curr_node = curr_node->next;
  }

  // 2. Abort all active execution contexts matching the stream
  for (size_t i = 0; i < queue->cap; ++i) {
    task_execution_t* exec = &queue->executions[i];
    if (exec->active && !exec->completed && exec->stream == stream) {
      exec->cancelled = true;
      exec->status = TASK_STATUS_CANCELLED;
    }
  }
}

static bool pending_list_push_locked(task_queue_t* queue,
                                     const task_submission_t* sub,
                                     arena_t** arena, bool cancelled) {
  // Assumes queue->mutex is LOCKED on entry!
  if (!queue->free_nodes) {
    return false;
  }
  // Lease a node from the pool
  task_node_t* node = queue->free_nodes;
  queue->free_nodes = node->next;

  node->sub = *sub;
  node->arena = *arena;  // Copy pointer!
  *arena = nullptr;      // Clear pointer!
  node->cancelled = cancelled;
  node->next = nullptr;

  // Append to the pending list (FIFO)
  if (!queue->pending_head) {
    queue->pending_head = node;
    queue->pending_tail = node;
  } else {
    queue->pending_tail->next = node;
    queue->pending_tail = node;
  }
  return true;
}

bool task_should_abort(const task_context_t* ctx) {
  const task_context_internal_t* internal_ctx =
      (const task_context_internal_t*)ctx;
  return atomic_load(&internal_ctx->exec->cancelled);
}

void task_set_failed(task_context_t* ctx) {
  task_context_internal_t* internal_ctx = (task_context_internal_t*)ctx;
  internal_ctx->failed = true;
}
