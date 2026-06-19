#include "src/imgui_c.h"

#include <stdarg.h>
#include "third_party/imgui/imgui.h"

// Window Draw List & Cursor
ig_draw_list_t* ig_get_window_draw_list(void) {
  return reinterpret_cast<ig_draw_list_t*>(ImGui::GetWindowDrawList());
}

ig_vec2_t ig_get_cursor_screen_pos(void) {
  ImVec2 pos = ImGui::GetCursorScreenPos();
  return {pos.x, pos.y};
}

ig_vec2_t ig_get_content_region_avail(void) {
  ImVec2 size = ImGui::GetContentRegionAvail();
  return {size.x, size.y};
}

void ig_set_cursor_screen_pos(ig_vec2_t pos) {
  ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y));
}

// Draw List Commands
void ig_draw_list_add_rect_filled(ig_draw_list_t* draw_list,
                                  ig_vec2_t p_min, ig_vec2_t p_max,
                                  uint32_t col) {
  reinterpret_cast<ImDrawList*>(draw_list)->AddRectFilled(ImVec2(p_min.x, p_min.y),
                                                          ImVec2(p_max.x, p_max.y), col);
}

// Widgets & Text
ig_vec2_t ig_calc_text_size(const char* text) {
  ImVec2 size = ImGui::CalcTextSize(text);
  return {size.x, size.y};
}

void ig_text(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  ImGui::TextV(fmt, args);
  va_end(args);
}

void ig_text_colored(ig_vec4_t col, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  ImGui::TextColoredV(ImVec4(col.x, col.y, col.z, col.w), fmt, args);
  va_end(args);
}

bool ig_button(const char* label, ig_vec2_t size) {
  return ImGui::Button(label, ImVec2(size.x, size.y));
}

void ig_progress_bar(float fraction, ig_vec2_t size_arg, const char* overlay) {
  ImGui::ProgressBar(fraction, ImVec2(size_arg.x, size_arg.y), overlay);
}
