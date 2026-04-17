#ifndef ZTRACING_SRC_IMGUI_IMPL_WEBGL_H_
#define ZTRACING_SRC_IMGUI_IMPL_WEBGL_H_

#include "src/allocator.h"
#include "third_party/imgui/imgui.h"

bool imgui_impl_webgl_init(Allocator allocator);
void imgui_impl_webgl_shutdown();
void imgui_impl_webgl_new_frame();
void imgui_impl_webgl_render_draw_data(ImDrawData* draw_data);
bool imgui_impl_webgl_create_fonts_texture();
void imgui_impl_webgl_destroy_fonts_texture();

#endif  // ZTRACING_SRC_IMGUI_IMPL_WEBGL_H_
