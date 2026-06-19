#ifndef ZTRACING_SRC_APP_H_
#define ZTRACING_SRC_APP_H_

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "src/allocator.h"
#include "src/array_list.h"
#include "src/colors.h"
#include "src/trace_data.h"
#include "src/trace_parser.h"
#include "src/trace_viewer.h"

typedef enum ThemeMode {
  THEME_MODE_AUTO = 0,
  THEME_MODE_DARK = 1,
  THEME_MODE_LIGHT = 2,
} theme_mode_t;

typedef struct TraceChunk {
  char* data;
  size_t size;
  // Raw bytes consumed from the input stream to produce this chunk.
  size_t input_consumed_bytes;
  bool is_eof;
} trace_chunk_t;

typedef struct TraceChunkNode {
  trace_chunk_t chunk;
  struct TraceChunkNode* next;
} trace_chunk_node_t;

typedef struct ChunkQueue {
  pthread_mutex_t mutex;
  pthread_cond_t cv;
  trace_chunk_node_t* head;
  trace_chunk_node_t* tail;
  _Atomic(size_t) queue_size_bytes;
  bool closed;
} chunk_queue_t;

typedef struct TraceLoadingState {
  _Atomic(size_t) event_count;
  _Atomic(size_t) total_bytes;
  // Total expected bytes from the raw input stream (0 if unknown).
  _Atomic(size_t) input_total_bytes;
  // Total raw bytes consumed from the input stream so far.
  _Atomic(size_t) input_consumed_bytes;
  double start_time;
  _Atomic(bool) active;
  int session_id;
  array_list_t filename;

  _Atomic(bool) request_update;
  chunk_queue_t chunk_queue;

  // Background job coordination
  _Atomic(bool) jobs_should_abort;
  pthread_mutex_t quit_mutex;
  pthread_cond_t quit_cv;

  trace_parser_t parser;

  // Dependencies for background processing
  allocator_t allocator;
  trace_data_t* trace_data;
  const theme_t* theme;
  trace_viewer_t* trace_viewer;
} trace_loading_state_t;

typedef struct App {
  counting_allocator_t counting_allocator;

  // UI & Config
  theme_mode_t theme_mode;
  const theme_t* theme;
  bool power_save_mode;
  bool first_frame;
  bool show_metrics_window;
  bool show_about_window;
  bool show_shortcuts_window;

  // Background Loading
  trace_loading_state_t loading;

  // Data & Viewer
  trace_data_t trace_data;
  trace_viewer_t trace_viewer;
} app_t;

// Initializes the application state.
void app_init(app_t* app, allocator_t allocator);

// Deinitializes the application state and releases resources.
void app_deinit(app_t* app);

// Updates the application state and UI for a single frame.
void app_update(app_t* app);

// Notifies the application that the system theme has changed.
void app_on_theme_changed(app_t* app);

// Begins a new loading session.
void app_begin_session(app_t* app, int session_id, const char* filename,
                       size_t input_total_bytes);

// Processes a chunk of trace data. Returns the current total size of chunks in
// the queue.
size_t app_handle_file_chunk(app_t* app, int session_id, char* data,
                             size_t size, size_t input_consumed_bytes,
                             bool is_eof);

// Returns the current total size of chunks in the queue.
size_t app_get_queue_size(app_t* app);

#endif  // ZTRACING_SRC_APP_H_
