#ifndef ZTRACING_SRC_LOADING_SCREEN_H_
#define ZTRACING_SRC_LOADING_SCREEN_H_

#include <stddef.h>

#include "src/colors.h"

void loading_screen_draw(const char* filename, size_t event_count,
                         size_t total_bytes, const Theme* theme);

#endif  // ZTRACING_SRC_LOADING_SCREEN_H_
