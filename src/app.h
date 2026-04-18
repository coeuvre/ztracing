#ifndef ZTRACING_SRC_APP_H_
#define ZTRACING_SRC_APP_H_

#include "src/allocator.h"
#include "src/array_list.h"
#include "src/trace_parser.h"

struct App {
  Allocator allocator;
  bool power_save_mode;

  TraceParser trace_parser;
  size_t trace_event_count;
  size_t trace_total_bytes;
  double trace_start_time;
  bool trace_parser_active;
};

// Initializes the application state.
void app_init(App* app, Allocator allocator);

// Deinitializes the application state and releases resources.
void app_deinit(App* app);

// Updates the application state and UI for a single frame.
void app_update(App* app);

// Processes a chunk of trace data.
void app_handle_file_chunk(App* app, const char* data, size_t size,
                           bool is_eof);

#endif  // ZTRACING_SRC_APP_H_
