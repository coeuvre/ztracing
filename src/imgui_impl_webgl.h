#ifndef ZTRACING_SRC_IMGUI_IMPL_WEBGL_H_
#define ZTRACING_SRC_IMGUI_IMPL_WEBGL_H_

#include "src/allocator.h"
#include "third_party/imgui/imgui.h"

bool ImGui_ImplWebGL_Init(Allocator allocator);
void ImGui_ImplWebGL_Shutdown();
void ImGui_ImplWebGL_NewFrame();
void ImGui_ImplWebGL_RenderDrawData(ImDrawData* draw_data);
bool ImGui_ImplWebGL_CreateFontsTexture();
void ImGui_ImplWebGL_DestroyFontsTexture();

#endif  // ZTRACING_SRC_IMGUI_IMPL_WEBGL_H_
