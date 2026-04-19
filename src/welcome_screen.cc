#include "src/welcome_screen.h"

#include "third_party/imgui/imgui.h"

void welcome_screen_draw(const Theme* theme) {
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImVec2 size = ImGui::GetContentRegionAvail();
  draw_list->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                           theme->viewport_bg);

  const char* msg = "Drop a Chrome Trace file here to begin";
  ImVec2 text_size = ImGui::CalcTextSize(msg);
  ImGui::SetCursorScreenPos(
      ImVec2(pos.x + (size.x - text_size.x) * 0.5f,
             pos.y + (size.y - text_size.y) * 0.5f));
  ImGui::Text("%s", msg);
}
