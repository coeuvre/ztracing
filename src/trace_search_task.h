#ifndef ZTRACING_SRC_TRACE_SEARCH_TASK_H_
#define ZTRACING_SRC_TRACE_SEARCH_TASK_H_

#include <stdbool.h>
#include <stddef.h>

#include "src/allocator.h"
#include "src/channel.h"

#ifdef __cplusplus
extern "C" {
#endif

// === 1. Public Task API ===
// Creates the search task input mailbox and spawns the background search task.
// Returns the opaque channel the caller uses to send abort requests.
// app_channel: output channel of app_msg_t (Task -> UI).
typedef struct trace_data trace_data_t;

channel_t* trace_search_start(const char* query, const trace_data_t* td,
                              bool include_threads, bool include_counters,
                              channel_t* app_channel, allocator_t allocator);

// === 2. Safe Input Sending APIs ===

// Sends an abort request to the search task.
bool trace_search_send_abort(channel_t* trace_search_channel);

#ifdef __cplusplus
}
#endif

#endif  // ZTRACING_SRC_TRACE_SEARCH_TASK_H_
