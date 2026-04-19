#ifndef ZTRACING_SRC_APP_H_
#define ZTRACING_SRC_APP_H_

#include "src/allocator.h"
#include "src/array_list.h"
#include "src/colors.h"
#include "src/trace_data.h"
#include "src/trace_parser.h"
#include "src/trace_viewer.h"

enum ThemeMode {
  THEME_MODE_AUTO = 0,
  THEME_MODE_DARK = 1,
  THEME_MODE_LIGHT = 2,
};

struct App {
  Allocator allocator;
  ThemeMode theme_mode;
  const Theme* theme;
  bool power_save_mode;
  bool first_frame;
  bool show_metrics_window;
  bool show_about_window;

  TraceParser trace_parser;
  TraceData trace_data;
  size_t trace_event_count;
  size_t trace_total_bytes;
  double trace_start_time;
  bool trace_parser_active;
  int current_session_id;
  ArrayList<char> trace_filename;

  TraceViewer trace_viewer;
};

// Initializes the application state.
void app_init(App* app, Allocator allocator);

// Deinitializes the application state and releases resources.
void app_deinit(App* app);

// Updates the application state and UI for a single frame.
void app_update(App* app);

// Notifies the application that the system theme has changed.
void app_on_theme_changed(App* app);

// Begins a new loading session.
void app_begin_session(App* app, int session_id, const char* filename);

// Processes a chunk of trace data.
void app_handle_file_chunk(App* app, int session_id, const char* data,
                           size_t size, bool is_eof);

#endif  // ZTRACING_SRC_APP_H_
