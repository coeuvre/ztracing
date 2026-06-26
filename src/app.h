#ifndef SRC_APP_H
#define SRC_APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/allocator.h"
#include "core/counting_allocator.h"
#include "core/task.h"
#include "src/array_list.h"
#include "src/colors.h"
#include "src/trace_data.h"
#include "src/trace_viewer.h"

struct trace_load_task;

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ThemeMode {
  THEME_MODE_AUTO = 0,
  THEME_MODE_DARK = 1,
  THEME_MODE_LIGHT = 2,
} theme_mode_t;

// Simplified, value-semantic loading state owned by the UI thread.
// High-frequency atomic updates and parser configurations are completely
// decoupled and isolated inside the background task thread.
typedef struct trace_loading_state {
  size_t event_count;        // Number of parsed events (UI display)
  size_t total_bytes;        // Total parsed bytes (UI display)
  size_t input_total_bytes;  // Total expected raw stream bytes
  size_t
      input_consumed_bytes;  // Raw stream bytes consumed so far (progress bar)
  double start_time;         // Timestamp when loading session started
  bool active;               // True if an active loading session is running
  int session_id;            // Current active session ID
  task_stream_t stream_id;   // Scheduler stream ID of the active session
  array_list_t filename;     // Name of the trace file being loaded
  bool request_update;       // Set to true when new frames should be drawn
} trace_loading_state_t;

typedef struct app {
  counting_allocator_t counting_allocator;

  // UI & Config
  theme_mode_t theme_mode;
  const theme_t* theme;
  bool power_save_mode;
  bool first_frame;
  bool show_metrics_window;
  bool show_about_window;
  bool show_shortcuts_window;

  // Background Task Schedulers
  task_queue_t* task_queue;  // Global background task queue scheduler
  struct trace_load_task*
      trace_load_task;  // Active loading task handle (opaque)
  struct trace_search_task*
      active_search_task;  // Active search task handle (opaque)

  // Background Loading State
  trace_loading_state_t loading;

  // Data & Viewer
  trace_data_t* trace_data;
  trace_viewer_t trace_viewer;
} app_t;

// Initializes the application state.
void app_init(app_t* app, allocator_t* allocator);

// Deinitializes the application state and releases resources.
void app_deinit(app_t* app);

// Polls and processes all pending background task completions.
void app_poll_completions(app_t* app);

// Updates the application state and UI for a single frame.
void app_update(app_t* app);

// Notifies the application that the system theme has changed.
void app_on_theme_changed(app_t* app, bool is_dark);

// Begins a new loading session.
void app_begin_session(app_t* app, int session_id, const char* filename,
                       size_t input_total_bytes);

// Processes a chunk of trace data. Returns the current total size of chunks in
// the queue.
size_t app_handle_file_chunk(app_t* app, int session_id, char* data,
                             size_t size, size_t input_consumed_bytes,
                             bool is_eof);

// Stops and aborts all active background jobs (loading, search, etc.) on the
// app.
void app_stop_jobs(app_t* app);

// Returns the current total size of chunks in the queue in bytes.
size_t app_get_buffered_bytes(app_t* app);

#ifdef __cplusplus
}
#endif

#endif  // SRC_APP_H
