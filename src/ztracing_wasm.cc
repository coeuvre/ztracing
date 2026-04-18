#include <GLES3/gl3.h>
#include <emscripten.h>
#include <emscripten/html5.h>

#include <cstdio>
#include <cstring>

#include "src/app.h"
#include "src/imgui_impl_wasm.h"
#include "src/imgui_impl_webgl.h"
#include "src/logging.h"
#include "src/ztracing.h"
#include "third_party/imgui/imgui.h"

static ArrayList<char> g_canvas_selector;
static App g_app;
static ArrayList<unsigned char> g_font_data;

static void main_loop() {
  if (g_app.power_save_mode && !imgui_impl_wasm_need_update()) {
    return;
  }

  imgui_impl_webgl_new_frame();
  imgui_impl_wasm_new_frame();
  ImGui::NewFrame();

  ImGuiIO& io = ImGui::GetIO();
  app_update(&g_app);

  ImGui::Render();

  glViewport(0, 0, (int)(io.DisplaySize.x * io.DisplayFramebufferScale.x),
             (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y));
  glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
  glClear(GL_COLOR_BUFFER_BIT);

  imgui_impl_webgl_render_draw_data(ImGui::GetDrawData());
}

extern "C" {
EMSCRIPTEN_KEEPALIVE int ztracing_init(const char* canvas_selector) {
  Allocator allocator = allocator_get_default();
  array_list_clear(&g_canvas_selector);
  array_list_append(&g_canvas_selector, allocator, canvas_selector,
                    strlen(canvas_selector) + 1);
  app_init(&g_app, allocator);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  EmscriptenWebGLContextAttributes attrs;
  emscripten_webgl_init_context_attributes(&attrs);
  attrs.majorVersion = 2;
  attrs.minorVersion = 0;
  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx =
      emscripten_webgl_create_context(g_canvas_selector.data, &attrs);
  if (ctx <= 0) {
    LOG_ERROR("failed to create webgl context for selector '%s' (error: %d)",
              g_canvas_selector.data, (int)ctx);
    return 1;
  }
  emscripten_webgl_make_context_current(ctx);

  ImGui::StyleColorsDark();
  imgui_impl_wasm_init(g_canvas_selector.data, allocator);
  imgui_impl_webgl_init(allocator);

  LOG_DEBUG("ztracing initialized successfully.");
  return 0;
}

EMSCRIPTEN_KEEPALIVE void ztracing_start() {
  emscripten_set_main_loop(main_loop, 0, 0);
}

EMSCRIPTEN_KEEPALIVE void ztracing_set_font_data(unsigned char* font_data,
                                                 int font_size) {
  array_list_clear(&g_font_data);
  array_list_append(&g_font_data, g_app.allocator, font_data,
                    (size_t)font_size);

  ImGuiIO& io = ImGui::GetIO();
  float dpi_scale = (float)emscripten_get_device_pixel_ratio();
  io.Fonts->Clear();
  ImFontConfig font_cfg;
  font_cfg.FontDataOwnedByAtlas = false;
  io.Fonts->AddFontFromMemoryTTF(g_font_data.data, (int)g_font_data.size,
                                 16.0f * dpi_scale, &font_cfg);
  io.Fonts->Build();
  io.FontGlobalScale = 1.0f / dpi_scale;
  imgui_impl_webgl_destroy_fonts_texture();
  imgui_impl_webgl_create_fonts_texture();

  imgui_impl_wasm_request_update();
}

EMSCRIPTEN_KEEPALIVE void* ztracing_malloc(int size) {
  // Use default allocator if app is not initialized yet.
  Allocator a =
      g_app.allocator.alloc ? g_app.allocator : allocator_get_default();
  return allocator_alloc(a, (size_t)size);
}

EMSCRIPTEN_KEEPALIVE void ztracing_free(void* ptr, int size) {
  Allocator a =
      g_app.allocator.alloc ? g_app.allocator : allocator_get_default();
  allocator_free(a, ptr, (size_t)size);
}

EMSCRIPTEN_KEEPALIVE void ztracing_begin_session(int session_id,
                                                 const char* filename) {
  app_begin_session(&g_app, session_id, filename);
  imgui_impl_wasm_request_update();
}

EMSCRIPTEN_KEEPALIVE void ztracing_handle_file_chunk(int session_id,
                                                     const char* data, int size,
                                                     bool is_eof) {
  app_handle_file_chunk(&g_app, session_id, data, (size_t)size, is_eof);
  imgui_impl_wasm_request_update();
}
}
