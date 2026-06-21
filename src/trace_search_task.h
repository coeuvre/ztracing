#ifndef ZTRACING_SRC_TRACE_SEARCH_TASK_H_
#define ZTRACING_SRC_TRACE_SEARCH_TASK_H_

#include <stdbool.h>
#include <stddef.h>

#include "src/allocator.h"
#include "src/channel.h"

#ifdef __cplusplus
extern "C" {
#endif

// === 1. Input Message Types ===
typedef enum {
  MSG_TRACE_SEARCH_ABORT,  // Signal the active search task to abort execution
} trace_search_msg_type_t;

// === 2. Input Message Envelope ===
typedef struct {
  trace_search_msg_type_t type;
} trace_search_msg_t;

// === 3. Public Task API ===
// Minimalist functional entry point to spawn the background search task.
// app_channel: Output channel of app_msg_t (Task -> UI)
// trace_search_channel: Input mailbox channel of trace_search_msg_t (UI ->
// Task)
typedef struct trace_data trace_data_t;

void trace_search_start(const char* query, const trace_data_t* td,
                        bool include_threads, bool include_counters,
                        channel_t* app_channel, channel_t* trace_search_channel,
                        allocator_t allocator);

// === 4. Safe Input Sending APIs ===

// Sends an abort request to the search task.
bool trace_search_send_abort(channel_t* trace_search_channel);

#ifdef __cplusplus
}
#endif

#endif  // ZTRACING_SRC_TRACE_SEARCH_TASK_H_
