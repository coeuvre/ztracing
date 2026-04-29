#include "src/loading_screen.h"

#include <stdio.h>

#include "third_party/imgui/imgui.h"

void loading_screen_draw(const char* filename, size_t event_count,
                         size_t total_bytes, size_t input_consumed_bytes,
                         size_t input_total_bytes, const Theme* theme) {
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImVec2 size = ImGui::GetContentRegionAvail();
  draw_list->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                           theme->viewport_bg);

  ImVec2 center = ImVec2(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);

  const char* title = "Loading Trace...";
  ImVec2 title_size = ImGui::CalcTextSize(title);

  ImGui::SetCursorScreenPos(
      ImVec2(center.x - title_size.x * 0.5f, center.y - 70));
  ImGui::Text("%s", title);

  if (filename && filename[0] != '\0') {
    ImVec2 file_size = ImGui::CalcTextSize(filename);
    ImGui::SetCursorScreenPos(
        ImVec2(center.x - file_size.x * 0.5f, center.y - 40));
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", filename);
  }

  if (input_total_bytes > 0) {
    float fraction = (float)input_consumed_bytes / (float)input_total_bytes;
    if (fraction > 1.0f) fraction = 1.0f;

    ImGui::SetCursorScreenPos(ImVec2(center.x - 150, center.y - 10));
    ImGui::ProgressBar(fraction, ImVec2(300, 0));
  }

  char progress[128];
  snprintf(progress, sizeof(progress), "Parsed %zu events (%.2f MB)",
           event_count, (double)total_bytes / (1024.0 * 1024.0));
  ImVec2 progress_size = ImGui::CalcTextSize(progress);
  ImGui::SetCursorScreenPos(
      ImVec2(center.x - progress_size.x * 0.5f, center.y + 25));
  ImGui::TextColored(theme->status_loading, "%s", progress);
}
