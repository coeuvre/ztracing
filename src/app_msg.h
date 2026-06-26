#ifndef ZTRACING_SRC_APP_MSG_H_
#define ZTRACING_SRC_APP_MSG_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "src/allocator.h"
#include "src/array_list.h"
#include "src/channel.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations to avoid header coupling
typedef struct trace_data trace_data_t;
typedef struct trace_histogram trace_histogram_t;

// === 1. Search Task Payloads ===

// Search completion payload (Value-semantic, transfers ownership of heap
// pointers)
typedef struct {
  trace_data_t* trace_data;      // The trace data used by the search
  array_list_t results;          // Value instance of results array list
  trace_histogram_t* histogram;  // Pointer to heap-allocated histogram
  channel_t* task_channel;       // Mailbox channel of the search task
} app_msg_trace_search_complete_t;

// Search aborted payload (Value-semantic)
typedef struct {
  trace_data_t* trace_data;  // The trace data used by the search
  channel_t* task_channel;   // Mailbox channel of the aborted task
} app_msg_trace_search_aborted_t;

// === 2. Message Types (Received by the App UI Thread) ===
typedef enum {
  // Search pipeline events
  APP_MSG_TRACE_SEARCH_COMPLETE,
  APP_MSG_TRACE_SEARCH_ABORTED,
} app_msg_type_t;

// === 3. App Message Envelope ===
typedef struct {
  app_msg_type_t type;
  allocator_t allocator;  // The allocator used to allocate the payload
  union {
    app_msg_trace_search_complete_t trace_search_complete;
    app_msg_trace_search_aborted_t trace_search_aborted;
  } as;
} app_msg_t;

// === 4. Message Destructor (Single Source of Truth Cleanup) ===
void app_msg_deinit(app_msg_t* msg);

// === 5. Safe Message Sending APIs ===

// Sends search completion, transferring ownership.
bool app_send_trace_search_complete(channel_t* app_channel,
                                    trace_data_t* trace_data,
                                    array_list_t results,
                                    trace_histogram_t* histogram,
                                    channel_t* task_channel,
                                    allocator_t allocator);

// Sends search aborted signal.
bool app_send_trace_search_aborted(channel_t* app_channel,
                                   trace_data_t* trace_data,
                                   channel_t* task_channel,
                                   allocator_t allocator);

#ifdef __cplusplus
}
#endif

#endif  // ZTRACING_SRC_APP_MSG_H_
