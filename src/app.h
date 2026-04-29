#ifndef ZTRACING_SRC_APP_H_
#define ZTRACING_SRC_APP_H_

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

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

struct TraceChunk {
  char* data;
  size_t size;
  // Raw bytes consumed from the input stream to produce this chunk.
  size_t input_consumed_bytes;
  bool is_eof;
};

struct ChunkQueue {
  std::mutex mutex;
  std::condition_variable cv;
  std::queue<TraceChunk> queue;
  std::atomic<size_t> queue_size_bytes{0};
  bool closed = false;
};

struct TraceLoadingState {
  std::atomic<size_t> event_count;
  std::atomic<size_t> total_bytes;
  // Total expected bytes from the raw input stream (0 if unknown).
  std::atomic<size_t> input_total_bytes;
  // Total raw bytes consumed from the input stream so far.
  std::atomic<size_t> input_consumed_bytes;
  double start_time;
  std::atomic<bool> active;
  int session_id;
  ArrayList<char> filename;

  std::thread worker_thread;
  std::atomic<bool> worker_should_abort;
  std::atomic<bool> request_update;
  ChunkQueue chunk_queue;

  TraceParser parser;

  // Dependencies for background processing
  Allocator allocator;
  TraceData* trace_data;
  const Theme* theme;
  TraceViewer* trace_viewer;
};

struct App {
  CountingAllocator counting_allocator;
  Allocator allocator;

  // UI & Config
  ThemeMode theme_mode;
  const Theme* theme;
  bool power_save_mode;
  bool first_frame;
  bool show_metrics_window;
  bool show_about_window;
  bool show_shortcuts_window;


  // Background Loading
  TraceLoadingState loading;

  // Data & Viewer
  TraceData trace_data;
  TraceViewer trace_viewer;
};

// Returns an initialized application state.
App app_init(Allocator allocator);

// Deinitializes the application state and releases resources.
void app_deinit(App* app);

// Updates the application state and UI for a single frame.
void app_update(App* app);

// Notifies the application that the system theme has changed.
void app_on_theme_changed(App* app);

// Begins a new loading session.
void app_begin_session(App* app, int session_id, const char* filename,
                       size_t input_total_bytes);

// Processes a chunk of trace data. Returns the current total size of chunks in the queue.
size_t app_handle_file_chunk(App* app, int session_id, char* data,
                             size_t size, size_t input_consumed_bytes,
                             bool is_eof);

// Returns the current total size of chunks in the queue.
size_t app_get_queue_size(App* app);

#endif  // ZTRACING_SRC_APP_H_
