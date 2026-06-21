#ifndef ZTRACING_SRC_TRACE_LOAD_TASK_H_
#define ZTRACING_SRC_TRACE_LOAD_TASK_H_

#include <stdbool.h>
#include <stddef.h>

#include "src/allocator.h"
#include "src/channel.h"
#include "src/colors.h"

#ifdef __cplusplus
extern "C" {
#endif

// === 1. Input Payloads ===

// Chunk data payload (Value-semantic, transfers raw buffer ownership)
typedef struct {
  char* data;  // Heap-allocated raw chunk buffer
  size_t size;
  size_t input_consumed_bytes;
  bool is_eof;
} trace_load_chunk_t;

// === 2. Input Message Types ===
typedef enum {
  MSG_TRACE_LOAD_CHUNK,  // Stream a raw chunk to the loader
  MSG_TRACE_LOAD_ABORT,  // Signal the loader task to abort execution
} trace_load_msg_type_t;

// === 3. Input Message Envelope ===
typedef struct {
  trace_load_msg_type_t type;
  union {
    trace_load_chunk_t chunk;
  } as;
} trace_load_msg_t;

// === 4. Public Task API ===
// Minimalist functional entry point to spawn the background loading task.
// app_channel: Output channel of app_msg_t (Task -> UI)
// trace_load_channel: Input mailbox channel of trace_load_msg_t (UI -> Task)
void trace_load_start(channel_t* app_channel, channel_t* trace_load_channel,
                      allocator_t allocator);

// === 5. Message Destructor (Single Source of Truth Cleanup) ===
void trace_load_msg_deinit(trace_load_msg_t* msg, allocator_t allocator);

// === 6. Safe Input Sending APIs (With automatic heap cleanup on failure) ===

// Sends a file chunk to the loader task. AUTOMATICALLY cleans up raw data heap
// buffer if the send fails.
bool trace_load_send_chunk(channel_t* trace_load_channel, char* data,
                           size_t size, size_t input_consumed_bytes,
                           bool is_eof, allocator_t allocator);

// Sends an abort request to the loader task.
bool trace_load_send_abort(channel_t* trace_load_channel);

#ifdef __cplusplus
}
#endif

#endif  // ZTRACING_SRC_TRACE_LOAD_TASK_H_
