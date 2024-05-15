#include "ztracing.h"

void ztracing_update(ZTracing *ztracing) {
    ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    if (ImGui::Begin(
            "MainWindow",
            0,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings
        )) {
    }
    ImGui::End();

    ImGui::ShowDemoWindow();
}
