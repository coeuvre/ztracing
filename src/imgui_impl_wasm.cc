#include "src/imgui_impl_wasm.h"

#include <emscripten/html5.h>
#include <stdlib.h>
#include <string.h>

#include "src/allocator.h"

struct BackendData {
  Allocator allocator;
  char* canvas_selector;
  double last_time;
  int frames_to_render;
};

static BackendData* get_backend_data() {
  return ImGui::GetCurrentContext()
             ? (BackendData*)ImGui::GetIO().BackendPlatformUserData
             : nullptr;
}

void imgui_impl_wasm_request_update() {
  if (BackendData* bd = get_backend_data()) {
    bd->frames_to_render = 5;
  }
}

static void update_canvas_size(BackendData* bd) {
  ImGuiIO& io = ImGui::GetIO();
  double width, height;
  if (emscripten_get_element_css_size(bd->canvas_selector, &width, &height) !=
      EMSCRIPTEN_RESULT_SUCCESS) {
    width = 1280;
    height = 720;
  }
  float dpi_scale = (float)emscripten_get_device_pixel_ratio();
  io.DisplaySize = ImVec2((float)width, (float)height);
  io.DisplayFramebufferScale = ImVec2(dpi_scale, dpi_scale);

  emscripten_set_canvas_element_size(
      bd->canvas_selector, (int)(width * dpi_scale), (int)(height * dpi_scale));
}

bool imgui_impl_wasm_need_update() {
  if (BackendData* bd = get_backend_data()) {
    return bd->frames_to_render > 0;
  }
  return true;
}

static EM_BOOL on_mouse_move(int event_type,
                             const EmscriptenMouseEvent* mouse_event,
                             void* user_data) {
  (void)event_type;
  (void)user_data;
  ImGuiIO& io = ImGui::GetIO();
  io.AddMousePosEvent((float)mouse_event->targetX, (float)mouse_event->targetY);
  imgui_impl_wasm_request_update();
  return EM_TRUE;
}

static EM_BOOL on_mouse_button(int event_type,
                               const EmscriptenMouseEvent* mouse_event,
                               void* user_data) {
  (void)user_data;
  ImGuiIO& io = ImGui::GetIO();
  io.AddMouseButtonEvent(mouse_event->button,
                         event_type == EMSCRIPTEN_EVENT_MOUSEDOWN);
  imgui_impl_wasm_request_update();
  return EM_TRUE;
}

static EM_BOOL on_wheel(int event_type, const EmscriptenWheelEvent* wheel_event,
                        void* user_data) {
  (void)event_type;
  (void)user_data;
  ImGuiIO& io = ImGui::GetIO();
  io.AddMouseWheelEvent((float)wheel_event->deltaX * -0.01f,
                        (float)wheel_event->deltaY * -0.01f);
  imgui_impl_wasm_request_update();
  return EM_TRUE;
}

static ImGuiKey string_to_imgui_key(const char* code) {
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

static EM_BOOL on_key(int event_type, const EmscriptenKeyboardEvent* key_event,
                      void* user_data) {
  (void)user_data;
  ImGuiIO& io = ImGui::GetIO();
  ImGuiKey key = string_to_imgui_key(key_event->code);
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

  imgui_impl_wasm_request_update();

  // Prevent browser from handling keys like Tab or Backspace when ImGui wants
  // them
  return io.WantCaptureKeyboard ? EM_TRUE : EM_FALSE;
}

static EM_BOOL on_resize(int event_type, const EmscriptenUiEvent* ui_event,
                         void* user_data) {
  (void)event_type;
  (void)ui_event;
  if (BackendData* bd = (BackendData*)user_data) {
    update_canvas_size(bd);
  }
  imgui_impl_wasm_request_update();
  return EM_TRUE;
}

bool imgui_impl_wasm_init(const char* canvas_selector, Allocator allocator) {
  ImGuiIO& io = ImGui::GetIO();

  BackendData* bd =
      (BackendData*)allocator_alloc(allocator, sizeof(BackendData));
  bd->allocator = allocator;
  bd->canvas_selector =
      (char*)allocator_alloc(bd->allocator, strlen(canvas_selector) + 1);
  strcpy(bd->canvas_selector, canvas_selector);
  bd->last_time = 0.0;
  bd->frames_to_render = 20;

  io.BackendPlatformUserData = (void*)bd;
  io.BackendPlatformName = "imgui_impl_wasm";
  io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;

  emscripten_set_mousemove_callback(canvas_selector, nullptr, EM_FALSE,
                                    on_mouse_move);
  emscripten_set_mousedown_callback(canvas_selector, nullptr, EM_FALSE,
                                    on_mouse_button);
  emscripten_set_mouseup_callback(canvas_selector, nullptr, EM_FALSE,
                                  on_mouse_button);
  emscripten_set_wheel_callback(canvas_selector, nullptr, EM_FALSE, on_wheel);

  emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr,
                                  EM_FALSE, on_key);
  emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr,
                                EM_FALSE, on_key);

  emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, (void*)bd,
                                 EM_FALSE, on_resize);

  update_canvas_size(bd);

  return true;
}

void imgui_impl_wasm_shutdown() {
  BackendData* bd = get_backend_data();
  Allocator allocator = bd->allocator;
  allocator_free(allocator, bd->canvas_selector,
                 strlen(bd->canvas_selector) + 1);
  allocator_free(allocator, bd, sizeof(BackendData));
  ImGui::GetIO().BackendPlatformUserData = nullptr;
}

void imgui_impl_wasm_new_frame() {
  BackendData* bd = get_backend_data();
  ImGuiIO& io = ImGui::GetIO();

  if (bd->frames_to_render > 0) {
    bd->frames_to_render--;
  }

  double current_time = emscripten_get_now() / 1000.0;
  io.DeltaTime = bd->last_time > 0.0 ? (float)(current_time - bd->last_time)
                                     : (float)(1.0f / 60.0f);
  bd->last_time = current_time;
}
