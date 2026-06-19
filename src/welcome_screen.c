#include "src/welcome_screen.h"
#include "src/platform.h"

#include "src/imgui_c.h"

void welcome_screen_draw(const theme_t* theme) {
  ig_draw_list_t* draw_list = ig_get_window_draw_list();
  ig_vec2_t pos = ig_get_cursor_screen_pos();
  ig_vec2_t size = ig_get_content_region_avail();
  ig_draw_list_add_rect_filled(draw_list, pos,
                               (ig_vec2_t){pos.x + size.x, pos.y + size.y},
                               theme->viewport_bg);

  const char* msg = "Drop a Chrome Trace file here to begin, or";
  ig_vec2_t text_size = ig_calc_text_size(msg);
  ig_set_cursor_screen_pos(
      (ig_vec2_t){pos.x + (size.x - text_size.x) * 0.5f,
                  pos.y + (size.y - text_size.y) * 0.5f - 20.0f});
  ig_text("%s", msg);

  const char* btn_label = "Select a file";
  ig_vec2_t btn_size = {120.0f, 30.0f};
  ig_set_cursor_screen_pos(
      (ig_vec2_t){pos.x + (size.x - btn_size.x) * 0.5f,
                  pos.y + (size.y - btn_size.y) * 0.5f + 20.0f});
  if (ig_button(btn_label, btn_size)) {
    platform_open_file_dialog();
  }
}
