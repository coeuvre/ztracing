#pragma once

#include "third_party/imgui/imgui.h"

#ifdef __cplusplus
extern "C" {
#endif

bool ImGui_ImplWasm_Init(const char* canvas_selector);
void ImGui_ImplWasm_Shutdown();
void ImGui_ImplWasm_NewFrame();

#ifdef __cplusplus
}
#endif
