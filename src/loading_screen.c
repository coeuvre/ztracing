#include "src/loading_screen.h"

#include <stdio.h>

#include "src/imgui_c.h"

void loading_screen_draw(const char* filename, size_t event_count,
                         size_t total_bytes, size_t input_consumed_bytes,
                         size_t input_total_bytes, const theme_t* theme) {
  ig_draw_list_t* draw_list = ig_get_window_draw_list();
  ig_vec2_t pos = ig_get_cursor_screen_pos();
  ig_vec2_t size = ig_get_content_region_avail();
  ig_draw_list_add_rect_filled(draw_list, pos,
                               (ig_vec2_t){pos.x + size.x, pos.y + size.y},
                               theme->viewport_bg);

  ig_vec2_t center = {pos.x + size.x * 0.5f, pos.y + size.y * 0.5f};

  const char* title = "Loading Trace...";
  ig_vec2_t title_size = ig_calc_text_size(title);

  ig_set_cursor_screen_pos(
      (ig_vec2_t){center.x - title_size.x * 0.5f, center.y - 70.0f});
  ig_text("%s", title);

  if (filename && filename[0] != '\0') {
    ig_vec2_t file_size = ig_calc_text_size(filename);
    ig_set_cursor_screen_pos(
        (ig_vec2_t){center.x - file_size.x * 0.5f, center.y - 40.0f});
    ig_text_colored((ig_vec4_t){0.7f, 0.7f, 0.7f, 1.0f}, "%s", filename);
  }

  if (input_total_bytes > 0) {
    float fraction = (float)input_consumed_bytes / (float)input_total_bytes;
    if (fraction > 1.0f) fraction = 1.0f;

    ig_set_cursor_screen_pos((ig_vec2_t){center.x - 150.0f, center.y - 10.0f});
    ig_progress_bar(fraction, (ig_vec2_t){300.0f, 0.0f}, nullptr); // Use nullptr!
  }

  char progress[128];
  snprintf(progress, sizeof(progress), "Parsed %zu events (%.2f MB)",
           event_count, (double)total_bytes / (1024.0 * 1024.0));
  ig_vec2_t progress_size = ig_calc_text_size(progress);
  ig_set_cursor_screen_pos(
      (ig_vec2_t){center.x - progress_size.x * 0.5f, center.y + 25.0f});
  
  ig_text_colored(theme->status_loading, "%s", progress);
}
