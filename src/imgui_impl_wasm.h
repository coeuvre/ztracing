#ifndef ZTRACING_SRC_IMGUI_IMPL_WASM_H_
#define ZTRACING_SRC_IMGUI_IMPL_WASM_H_

#include "src/allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

bool imgui_impl_wasm_init(const char* canvas_selector, allocator_t allocator);
void imgui_impl_wasm_shutdown(void);
void imgui_impl_wasm_new_frame(void);

void imgui_impl_wasm_request_update(void);
bool imgui_impl_wasm_need_update(void);

float imgui_impl_wasm_get_dpi_scale(void);

#ifdef __cplusplus
}
#endif

#endif  // IMGUI_IMPL_WASM_H_  // ZTRACING_SRC_IMGUI_IMPL_WASM_H_
