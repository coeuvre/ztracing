#include <GLES3/gl3.h>
#include <cassert>
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
static App* g_app = nullptr;
static ArrayList<unsigned char> g_font_data;

static void* imgui_alloc(size_t sz, void* user_data) {
  Allocator* a = (Allocator*)user_data;
  size_t header_size = 16;  // Ensure 16-byte alignment
  size_t total_size = sz + header_size;
  void* ptr = allocator_alloc(*a, total_size);
  if (!ptr) return nullptr;
  *(size_t*)ptr = total_size;
  return (char*)ptr + header_size;
}

static void imgui_free(void* ptr, void* user_data) {
  if (!ptr) return;
  Allocator* a = (Allocator*)user_data;
  size_t header_size = 16;
  void* real_ptr = (char*)ptr - header_size;
  allocator_free(*a, real_ptr, *(size_t*)real_ptr);
}

static void main_loop() {
  if (g_app->loading.request_update.exchange(false) ||
      g_app->trace_viewer.search.request_update.exchange(false)) {
    imgui_impl_wasm_request_update();
  }

  if (g_app->power_save_mode && !imgui_impl_wasm_need_update()) {
    return;
  }

  imgui_impl_webgl_new_frame();
  imgui_impl_wasm_new_frame();
  ImGui::NewFrame();

  ImGuiIO& io = ImGui::GetIO();
  app_update(g_app);

  ImGui::Render();

  glViewport(0, 0, (int)(io.DisplaySize.x * io.DisplayFramebufferScale.x),
             (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y));
  glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
  glClear(GL_COLOR_BUFFER_BIT);

  imgui_impl_webgl_render_draw_data(ImGui::GetDrawData());
}

extern "C" {
EMSCRIPTEN_KEEPALIVE int ztracing_init(const char* canvas_selector) {
  Allocator default_allocator = allocator_get_default();
  g_app = (App*)allocator_alloc(default_allocator, sizeof(App));
  new (g_app) App(app_init(default_allocator));

  static Allocator imgui_allocator;
  imgui_allocator = counting_allocator_get_allocator(&g_app->counting_allocator);
  ImGui::SetAllocatorFunctions(imgui_alloc, imgui_free, &imgui_allocator);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

  Allocator allocator = imgui_allocator;
  array_list_clear(&g_canvas_selector);
  array_list_append(&g_canvas_selector, allocator, canvas_selector,
                    strlen(canvas_selector) + 1);

  EmscriptenWebGLContextAttributes attrs;
  emscripten_webgl_init_context_attributes(&attrs);
  attrs.majorVersion = 2;
  attrs.minorVersion = 0;
  attrs.alpha = EM_FALSE;
  attrs.antialias = EM_FALSE;
  attrs.premultipliedAlpha = EM_FALSE;
  attrs.depth = EM_FALSE;
  attrs.stencil = EM_FALSE;
  attrs.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;

  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx =
      emscripten_webgl_create_context(g_canvas_selector.data, &attrs);
  if (ctx <= 0) {
    LOG_ERROR("failed to create webgl context for selector '%s' (error: %d)",
              g_canvas_selector.data, (int)ctx);
    return 1;
  }
  emscripten_webgl_make_context_current(ctx);

  imgui_impl_wasm_init(g_canvas_selector.data, allocator);
  if (!imgui_impl_webgl_init(allocator)) {
    return 2;
  }

  app_on_theme_changed(g_app);
  imgui_impl_wasm_request_update();
  LOG_DEBUG("ztracing initialized successfully.");
  return 0;
}

EMSCRIPTEN_KEEPALIVE void ztracing_start() {
  emscripten_set_main_loop(main_loop, 0, 0);
}

EMSCRIPTEN_KEEPALIVE void ztracing_set_font_data(unsigned char* font_data,
                                                 int font_size) {
  array_list_clear(&g_font_data);
  Allocator allocator = counting_allocator_get_allocator(&g_app->counting_allocator);
  array_list_append(&g_font_data, allocator, font_data,
                    (size_t)font_size);

  ImGuiIO& io = ImGui::GetIO();
  float dpi_scale = imgui_impl_wasm_get_dpi_scale();
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
  assert(g_app != nullptr);
  // Use default allocator if app is not initialized yet.
  Allocator a = counting_allocator_get_allocator(&g_app->counting_allocator);
  return allocator_alloc(a, (size_t)size);
}

EMSCRIPTEN_KEEPALIVE void ztracing_free(void* ptr, int size) {
  assert(g_app != nullptr);
  Allocator a = counting_allocator_get_allocator(&g_app->counting_allocator);
  allocator_free(a, ptr, (size_t)size);
}

EMSCRIPTEN_KEEPALIVE void ztracing_begin_session(int session_id,
                                                 const char* filename,
                                                 double input_total_bytes) {
  app_begin_session(g_app, session_id, filename, (size_t)input_total_bytes);
  imgui_impl_wasm_request_update();
}

EMSCRIPTEN_KEEPALIVE int ztracing_handle_file_chunk(int session_id,
                                                     char* data, int size,
                                                     double input_consumed_bytes,
                                                     bool is_eof) {
  return (int)app_handle_file_chunk(g_app, session_id, data, (size_t)size,
                                    (size_t)input_consumed_bytes, is_eof);
}

EMSCRIPTEN_KEEPALIVE int ztracing_get_queue_size() {
  return (int)app_get_queue_size(g_app);
}

EMSCRIPTEN_KEEPALIVE void ztracing_on_theme_changed() {
  app_on_theme_changed(g_app);
  imgui_impl_wasm_request_update();
}
}
