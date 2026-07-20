#ifndef SRC_IMGUI_IMPL_WEBGL_H
#define SRC_IMGUI_IMPL_WEBGL_H

struct ig_draw_data;

#ifdef __cplusplus
extern "C" {
#endif

int imgui_impl_webgl_init(void);
void imgui_impl_webgl_shutdown(void);
void imgui_impl_webgl_new_frame(void);
void imgui_impl_webgl_render_draw_data(struct ig_draw_data* draw_data);
int imgui_impl_webgl_create_fonts_texture(void);
void imgui_impl_webgl_destroy_fonts_texture(void);

#ifdef __cplusplus
}
#endif

#endif  // SRC_IMGUI_IMPL_WEBGL_H
