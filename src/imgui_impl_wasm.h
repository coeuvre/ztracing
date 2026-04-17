#ifndef ZTRACING_SRC_IMGUI_IMPL_WASM_H_
#define ZTRACING_SRC_IMGUI_IMPL_WASM_H_

#include "src/allocator.h"
#include "third_party/imgui/imgui.h"

bool ImGui_ImplWasm_Init(const char* canvas_selector, Allocator allocator);
void ImGui_ImplWasm_Shutdown();
void ImGui_ImplWasm_NewFrame();

void ImGui_ImplWasm_RequestUpdate();
bool ImGui_ImplWasm_NeedUpdate();

#endif  // ZTRACING_SRC_IMGUI_IMPL_WASM_H_
