#include "ztracing.h"

void ztracing_update(ZTracing *ztracing) {
    ImGuiViewport *viewport = ImGui::GetMainViewport();

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open")) {
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("About")) {
            ImGui::MenuItem("Dear ImGui", "", &ztracing->show_demo_window);
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    if (ImGui::Begin(
            "MainWindow",
            0,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoDocking
        )) {
    }
    ImGui::End();

    if (ztracing->show_demo_window) {
        ImGui::ShowDemoWindow(&ztracing->show_demo_window);
    }
}
