#ifndef ZTRACING_SRC_LOADING_SCREEN_H_
#define ZTRACING_SRC_LOADING_SCREEN_H_

#include <stddef.h>

#include "src/colors.h"

// Draws the loading screen with progress information.
// input_consumed_bytes: Absolute raw bytes processed so far.
// input_total_bytes: Total raw bytes expected (0 to hide progress bar).
#ifdef __cplusplus
extern "C" {
#endif

void loading_screen_draw(const char* filename, size_t event_count,
                         size_t total_bytes, size_t input_consumed_bytes,
                         size_t input_total_bytes, const theme_t* theme);

#ifdef __cplusplus
}
#endif

#endif  // ZTRACING_SRC_LOADING_SCREEN_H_
