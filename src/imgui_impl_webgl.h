#pragma once

#include "third_party/imgui/imgui.h"

#ifdef __cplusplus
extern "C" {
#endif

bool ImGui_ImplWebGL_Init();
void ImGui_ImplWebGL_Shutdown();
void ImGui_ImplWebGL_NewFrame();
void ImGui_ImplWebGL_RenderDrawData(ImDrawData* draw_data);
bool ImGui_ImplWebGL_CreateFontsTexture();
void ImGui_ImplWebGL_DestroyFontsTexture();

#ifdef __cplusplus
}
#endif
