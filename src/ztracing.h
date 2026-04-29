#ifndef ZTRACING_SRC_ZTRACING_H_
#define ZTRACING_SRC_ZTRACING_H_

#include <emscripten.h>

extern "C" {
// IMPORTANT: ztracing_init must be called before any other ztracing_*
// functions.

// Initializes the ztracing system with the specified canvas selector.
EMSCRIPTEN_KEEPALIVE int ztracing_init(const char* canvas_selector);

// Allocates memory using the application's allocator.
EMSCRIPTEN_KEEPALIVE void* ztracing_malloc(int size);

// Frees memory using the application's allocator.
EMSCRIPTEN_KEEPALIVE void ztracing_free(void* ptr, int size);

// Sets the font data to be used by the application.
EMSCRIPTEN_KEEPALIVE void ztracing_set_font_data(unsigned char* font_data,
                                                 int font_size);

// Starts the main application loop.
EMSCRIPTEN_KEEPALIVE void ztracing_start();

// Begins a new loading session.
// input_total_bytes: Total expected raw bytes from source (0 if unknown).
EMSCRIPTEN_KEEPALIVE void ztracing_begin_session(int session_id,
                                                 const char* filename,
                                                 double input_total_bytes);

// Handles a chunk of file data from the JavaScript side. Returns current queue size.
// input_consumed_bytes: Absolute count of raw bytes read from source up to this chunk.
EMSCRIPTEN_KEEPALIVE int ztracing_handle_file_chunk(int session_id,
                                                     char* data, int size,
                                                     double input_consumed_bytes,
                                                     bool is_eof);

// Returns the current total size of chunks in the queue.
EMSCRIPTEN_KEEPALIVE int ztracing_get_queue_size();

// Notifies the application that the system theme has changed.
EMSCRIPTEN_KEEPALIVE void ztracing_on_theme_changed();

}  // extern "C"

#endif  // ZTRACING_SRC_ZTRACING_H_
