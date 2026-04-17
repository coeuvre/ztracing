#include "src/imgui_impl_wasm.h"

#include <emscripten/html5.h>
#include <stdlib.h>
#include <string.h>

struct BackendData {
  char* canvas_selector;
  double last_time;
  int frames_to_render;
};

static BackendData* GetBackendData() {
  return ImGui::GetCurrentContext()
             ? (BackendData*)ImGui::GetIO().BackendPlatformUserData
             : nullptr;
}

void ImGui_ImplWasm_RequestUpdate() {
  if (BackendData* bd = GetBackendData()) {
    bd->frames_to_render = 5;
  }
}

bool ImGui_ImplWasm_NeedUpdate() {
  if (BackendData* bd = GetBackendData()) {
    return bd->frames_to_render > 0;
  }
  return true;
}

static EM_BOOL OnMouseMove(int event_type,
                           const EmscriptenMouseEvent* mouse_event,
                           void* user_data) {
  ImGuiIO& io = ImGui::GetIO();
  io.AddMousePosEvent((float)mouse_event->targetX, (float)mouse_event->targetY);
  ImGui_ImplWasm_RequestUpdate();
  return EM_TRUE;
}

static EM_BOOL OnMouseButton(int event_type,
                             const EmscriptenMouseEvent* mouse_event,
                             void* user_data) {
  ImGuiIO& io = ImGui::GetIO();
  io.AddMouseButtonEvent(mouse_event->button,
                         event_type == EMSCRIPTEN_EVENT_MOUSEDOWN);
  ImGui_ImplWasm_RequestUpdate();
  return EM_TRUE;
}

static EM_BOOL OnWheel(int event_type, const EmscriptenWheelEvent* wheel_event,
                       void* user_data) {
  ImGuiIO& io = ImGui::GetIO();
  io.AddMouseWheelEvent((float)wheel_event->deltaX * -0.01f,
                        (float)wheel_event->deltaY * -0.01f);
  ImGui_ImplWasm_RequestUpdate();
  return EM_TRUE;
}

static ImGuiKey StringToImGuiKey(const char* code) {
  if (strcmp(code, "ArrowLeft") == 0) return ImGuiKey_LeftArrow;
  if (strcmp(code, "ArrowRight") == 0) return ImGuiKey_RightArrow;
  if (strcmp(code, "ArrowUp") == 0) return ImGuiKey_UpArrow;
  if (strcmp(code, "ArrowDown") == 0) return ImGuiKey_DownArrow;
  if (strcmp(code, "PageUp") == 0) return ImGuiKey_PageUp;
  if (strcmp(code, "PageDown") == 0) return ImGuiKey_PageDown;
  if (strcmp(code, "Home") == 0) return ImGuiKey_Home;
  if (strcmp(code, "End") == 0) return ImGuiKey_End;
  if (strcmp(code, "Insert") == 0) return ImGuiKey_Insert;
  if (strcmp(code, "Delete") == 0) return ImGuiKey_Delete;
  if (strcmp(code, "Backspace") == 0) return ImGuiKey_Backspace;
  if (strcmp(code, "Space") == 0) return ImGuiKey_Space;
  if (strcmp(code, "Enter") == 0) return ImGuiKey_Enter;
  if (strcmp(code, "Escape") == 0) return ImGuiKey_Escape;
  if (strcmp(code, "Tab") == 0) return ImGuiKey_Tab;
  if (strncmp(code, "Key", 3) == 0 && code[3] >= 'A' && code[3] <= 'Z')
    return (ImGuiKey)(ImGuiKey_A + (code[3] - 'A'));
  if (strncmp(code, "Digit", 5) == 0 && code[5] >= '0' && code[5] <= '9')
    return (ImGuiKey)(ImGuiKey_0 + (code[5] - '0'));
  if (strncmp(code, "F", 1) == 0 && strlen(code) >= 2) {
    int f = atoi(code + 1);
    if (f >= 1 && f <= 12) return (ImGuiKey)(ImGuiKey_F1 + (f - 1));
  }
  if (strcmp(code, "ShiftLeft") == 0) return ImGuiKey_LeftShift;
  if (strcmp(code, "ShiftRight") == 0) return ImGuiKey_RightShift;
  if (strcmp(code, "ControlLeft") == 0) return ImGuiKey_LeftCtrl;
  if (strcmp(code, "ControlRight") == 0) return ImGuiKey_RightCtrl;
  if (strcmp(code, "AltLeft") == 0) return ImGuiKey_LeftAlt;
  if (strcmp(code, "AltRight") == 0) return ImGuiKey_RightAlt;
  return ImGuiKey_None;
}

static EM_BOOL OnKey(int event_type, const EmscriptenKeyboardEvent* key_event,
                     void* user_data) {
  ImGuiIO& io = ImGui::GetIO();
  ImGuiKey key = StringToImGuiKey(key_event->code);
  if (key != ImGuiKey_None) {
    io.AddKeyEvent(key, event_type == EMSCRIPTEN_EVENT_KEYDOWN);
    io.AddKeyEvent(ImGuiMod_Ctrl, key_event->ctrlKey);
    io.AddKeyEvent(ImGuiMod_Shift, key_event->shiftKey);
    io.AddKeyEvent(ImGuiMod_Alt, key_event->altKey);
    io.AddKeyEvent(ImGuiMod_Super, key_event->metaKey);
  }

  if (event_type == EMSCRIPTEN_EVENT_KEYDOWN && strlen(key_event->key) == 1) {
    io.AddInputCharacter(key_event->key[0]);
  }

  ImGui_ImplWasm_RequestUpdate();

  // Prevent browser from handling keys like Tab or Backspace when ImGui wants
  // them
  return io.WantCaptureKeyboard ? EM_TRUE : EM_FALSE;
}

static EM_BOOL OnResize(int event_type, const EmscriptenUiEvent* ui_event,
                        void* user_data) {
  ImGui_ImplWasm_RequestUpdate();
  return EM_TRUE;
}

bool ImGui_ImplWasm_Init(const char* canvas_selector) {
  ImGuiIO& io = ImGui::GetIO();

  BackendData* bd = (BackendData*)malloc(sizeof(BackendData));
  bd->canvas_selector = strdup(canvas_selector);
  bd->last_time = 0.0;
  bd->frames_to_render = 20;

  io.BackendPlatformUserData = (void*)bd;
  io.BackendPlatformName = "imgui_impl_wasm";
  io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;

  emscripten_set_mousemove_callback(canvas_selector, nullptr, EM_FALSE,
                                    OnMouseMove);
  emscripten_set_mousedown_callback(canvas_selector, nullptr, EM_FALSE,
                                    OnMouseButton);
  emscripten_set_mouseup_callback(canvas_selector, nullptr, EM_FALSE,
                                  OnMouseButton);
  emscripten_set_wheel_callback(canvas_selector, nullptr, EM_FALSE, OnWheel);

  emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr,
                                  EM_FALSE, OnKey);
  emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr,
                                EM_FALSE, OnKey);

  emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr,
                                 EM_FALSE, OnResize);

  return true;
}

void ImGui_ImplWasm_Shutdown() {
  BackendData* bd = GetBackendData();
  free(bd->canvas_selector);
  free(bd);
  ImGui::GetIO().BackendPlatformUserData = nullptr;
}

void ImGui_ImplWasm_NewFrame() {
  BackendData* bd = GetBackendData();
  ImGuiIO& io = ImGui::GetIO();

  if (bd->frames_to_render > 0) {
    bd->frames_to_render--;
  }

  double current_time = emscripten_get_now() / 1000.0;
  io.DeltaTime = bd->last_time > 0.0 ? (float)(current_time - bd->last_time)
                                    : (float)(1.0f / 60.0f);
  bd->last_time = current_time;

  double width, height;
  if (emscripten_get_element_css_size(bd->canvas_selector, &width, &height) !=
      EMSCRIPTEN_RESULT_SUCCESS) {
    width = 1280;
    height = 720;  // Fallback
  }

  float dpi_scale = (float)emscripten_get_device_pixel_ratio();
  io.DisplaySize = ImVec2((float)width, (float)height);
  io.DisplayFramebufferScale = ImVec2(dpi_scale, dpi_scale);
}
