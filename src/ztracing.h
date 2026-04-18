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

// Handles a chunk of file data from the JavaScript side.
EMSCRIPTEN_KEEPALIVE void ztracing_handle_file_chunk(const char* data, int size,
                                                     bool is_eof);

}  // extern "C"

#endif  // ZTRACING_SRC_ZTRACING_H_
