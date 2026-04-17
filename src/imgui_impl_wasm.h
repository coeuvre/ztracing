#pragma once

#include "third_party/imgui/imgui.h"

#ifdef __cplusplus
extern "C" {
#endif

bool ImGui_ImplWasm_Init(const char* canvas_selector);
void ImGui_ImplWasm_Shutdown();
void ImGui_ImplWasm_NewFrame();

void ImGui_ImplWasm_RequestUpdate();
bool ImGui_ImplWasm_NeedUpdate();

#ifdef __cplusplus
}
#endif
