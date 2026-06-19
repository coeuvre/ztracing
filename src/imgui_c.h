#ifndef ZTRACING_SRC_IMGUI_C_H_
#define ZTRACING_SRC_IMGUI_C_H_

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque types for C
typedef struct ig_draw_list ig_draw_list_t;

// Basic structures
typedef struct ig_vec2 {
  float x, y;
} ig_vec2_t;

typedef struct ig_vec4 {
  float x, y, z, w;
} ig_vec4_t;

// Window Draw List & Cursor
ig_draw_list_t* ig_get_window_draw_list(void);
ig_vec2_t ig_get_cursor_screen_pos(void);
ig_vec2_t ig_get_content_region_avail(void);
void ig_set_cursor_screen_pos(ig_vec2_t pos);

// Draw List Commands (Simplified C helpers)
void ig_draw_list_add_rect_filled(ig_draw_list_t* draw_list,
                                  ig_vec2_t p_min, ig_vec2_t p_max,
                                  uint32_t col);

// Widgets & Text
ig_vec2_t ig_calc_text_size(const char* text);
void ig_text(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void ig_text_colored(ig_vec4_t col, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
bool ig_button(const char* label, ig_vec2_t size);
void ig_progress_bar(float fraction, ig_vec2_t size_arg, const char* overlay);

#ifdef __cplusplus
}
#endif

#endif  // ZTRACING_SRC_IMGUI_C_H_
