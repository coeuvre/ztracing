#ifndef ZTRACING_SRC_TRACE_LOAD_TASK_H_
#define ZTRACING_SRC_TRACE_LOAD_TASK_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "src/allocator.h"
#include "src/channel.h"

#ifdef __cplusplus
extern "C" {
#endif

// === 1. Loader Channel Messages ===
typedef enum {
  MSG_TRACE_LOAD_CHUNK,
  MSG_TRACE_LOAD_ABORT,
} trace_load_msg_type_t;

// Chunk payload (Value-semantic, transfers ownership of data buffer)
typedef struct {
  char* data;
  size_t size;
  size_t input_consumed_bytes;
  bool is_eof;
} trace_load_chunk_t;

// Loader message envelope
typedef struct {
  trace_load_msg_type_t type;
  allocator_t allocator;  // The allocator used to allocate the chunk data
  union {
    trace_load_chunk_t chunk;
  } as;
} trace_load_msg_t;

// === 2. Message Destructor (Single Source of Truth Cleanup) ===
void trace_load_msg_deinit(trace_load_msg_t* msg);

// === 3. Public Task API ===
// Minimalist functional entry point to spawn the background loading task.
// app_channel: Output channel of app_msg_t (Task -> UI)
// trace_load_channel: Input mailbox channel of trace_load_msg_t (UI -> Task)
void trace_load_start(channel_t* app_channel, channel_t* trace_load_channel,
                      allocator_t allocator);

// === 4. Safe Input Sending APIs ===

// Sends a file chunk to the loader task.
bool trace_load_send_chunk(channel_t* trace_load_channel, char* data,
                           size_t size, size_t input_consumed_bytes,
                           bool is_eof, allocator_t allocator);

// Sends an abort request to the loader task.
bool trace_load_send_abort(channel_t* trace_load_channel);

#ifdef __cplusplus
}
#endif

#endif  // ZTRACING_SRC_TRACE_LOAD_TASK_H_
