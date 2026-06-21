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

// === 1. Public Task API ===
// Creates the loader input mailbox and spawns the background loader task.
// Returns the opaque channel the caller uses to feed chunks / abort.
// app_channel: output channel of app_msg_t (Task -> UI).
channel_t* trace_load_start(channel_t* app_channel, allocator_t allocator);

// === 2. Safe Input Sending APIs ===

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
