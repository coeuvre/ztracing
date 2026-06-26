#ifndef SRC_IMGUI_IMPL_WEBGL_H
#define SRC_IMGUI_IMPL_WEBGL_H

#include "core/allocator.h"

struct ig_draw_data;

#ifdef __cplusplus
extern "C" {
#endif

bool imgui_impl_webgl_init(allocator_t allocator);
void imgui_impl_webgl_shutdown(void);
void imgui_impl_webgl_new_frame(void);
void imgui_impl_webgl_render_draw_data(struct ig_draw_data* draw_data);
bool imgui_impl_webgl_create_fonts_texture(void);
void imgui_impl_webgl_destroy_fonts_texture(void);

#ifdef __cplusplus
}
#endif

#endif  // SRC_IMGUI_IMPL_WEBGL_H
