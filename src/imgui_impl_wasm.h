#ifndef ZTRACING_SRC_IMGUI_IMPL_WASM_H_
#define ZTRACING_SRC_IMGUI_IMPL_WASM_H_

#include "src/allocator.h"
#include "third_party/imgui/imgui.h"

bool imgui_impl_wasm_init(const char* canvas_selector, Allocator allocator);
void imgui_impl_wasm_shutdown();
void imgui_impl_wasm_new_frame();

void imgui_impl_wasm_request_update();
bool imgui_impl_wasm_need_update();

#endif  // ZTRACING_SRC_IMGUI_IMPL_WASM_H_
