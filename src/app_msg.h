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

// Forward declarations to avoid deep header dependencies and circular
// compilation.
typedef struct trace_data trace_data_t;
typedef struct duration_histogram duration_histogram_t;

// === 1. Ingestion (Loader) Task Payloads ===

// Progress update payload (Value-semantic)
typedef struct {
  size_t event_count;
  size_t total_bytes;
} app_msg_load_progress_t;

// Ingestion completion payload (Value-semantic, transfers ownership of heap
// pointers)
typedef struct {
  trace_data_t* trace_data;  // Heap-allocated trace data shell
  array_list_t tracks;  // Organized tracks (value instance, heap array list)
  int64_t min_ts;
  int64_t max_ts;
  channel_t* task_channel;  // Mailbox channel of the loader task
} app_msg_load_result_t;

// Ingestion aborted payload (Value-semantic)
typedef struct {
  channel_t* task_channel;  // Mailbox channel of the loader task
} app_msg_load_aborted_t;

// === 2. Search Task Payloads ===

// Search completion payload (Value-semantic, transfers ownership of heap
// pointers)
typedef struct {
  trace_data_t* trace_data;         // The trace data used by the search
  array_list_t results;             // Value instance of results array list
  duration_histogram_t* histogram;  // Pointer to heap-allocated histogram
  channel_t* task_channel;          // Mailbox channel of the search task
} app_msg_search_result_t;

// Search aborted payload (Value-semantic)
typedef struct {
  trace_data_t* trace_data;  // The trace data used by the search
  channel_t* task_channel;   // Mailbox channel of the aborted task
} app_msg_search_aborted_t;

// === 3. Message Types (Received by the App UI Thread) ===
typedef enum {
  // Loading pipeline events
  MSG_TRACE_LOAD_PROGRESS,
  MSG_TRACE_LOAD_COMPLETE,
  MSG_TRACE_LOAD_ABORTED,

  // Search pipeline events
  MSG_TRACE_SEARCH_COMPLETE,
  MSG_TRACE_SEARCH_ABORTED,
} app_msg_type_t;

// === 4. App Message Envelope ===
typedef struct {
  app_msg_type_t type;
  union {
    app_msg_load_progress_t load_progress;
    app_msg_load_result_t load_result;
    app_msg_load_aborted_t load_aborted;
    app_msg_search_result_t search_result;
    app_msg_search_aborted_t search_aborted;
  } as;
} app_msg_t;

// === 5. Message Destructor (Single Source of Truth Cleanup) ===
void app_msg_deinit(app_msg_t* msg, allocator_t allocator);

// === 6. Safe Message Sending APIs (With automatic heap cleanup on failure) ===

// Sends a progress update from the loader task. (Value-only, no allocation)
bool app_send_load_progress(channel_t* app_channel, size_t event_count,
                            size_t total_bytes);

// Sends load completion, transferring ownership. AUTOMATICALLY cleans up all
// heap memory if the send fails.
bool app_send_load_complete(channel_t* app_channel, trace_data_t* trace_data,
                            array_list_t tracks, int64_t min_ts, int64_t max_ts,
                            channel_t* task_channel, allocator_t allocator);

// Sends load aborted signal.
bool app_send_load_aborted(channel_t* app_channel, channel_t* task_channel,
                           allocator_t allocator);

// Sends search completion, transferring ownership. AUTOMATICALLY cleans up
// results and histogram if the send fails.
bool app_send_search_complete(channel_t* app_channel, trace_data_t* trace_data,
                              array_list_t results,
                              duration_histogram_t* histogram,
                              channel_t* task_channel, allocator_t allocator);

// Sends search aborted signal.
bool app_send_search_aborted(channel_t* app_channel, trace_data_t* trace_data,
                             channel_t* task_channel, allocator_t allocator);

#ifdef __cplusplus
}
#endif

#endif  // ZTRACING_SRC_APP_MSG_H_
