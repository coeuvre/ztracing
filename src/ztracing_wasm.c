#include <GLES3/gl3.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "src/app.h"
#include "src/assert.h"
#include "src/imgui_c.h"
#include "src/imgui_impl_wasm.h"
#include "src/imgui_impl_webgl.h"
#include "src/logging.h"
#include "src/platform.h"
#include "src/ztracing.h"

static array_list_t g_canvas_selector;
static app_t* g_app = nullptr;
static array_list_t g_font_data;

static void* imgui_alloc(size_t sz, void* user_data) {
  allocator_t* a = (allocator_t*)user_data;
  size_t header_size = 16;  // Ensure 16-byte alignment
  size_t total_size = sz + header_size;
  void* ptr = allocator_alloc(*a, total_size);
  if (!ptr) return nullptr;
  *(size_t*)ptr = total_size;
  return (char*)ptr + header_size;
}

static void imgui_free(void* ptr, void* user_data) {
  if (!ptr) return;
  allocator_t* a = (allocator_t*)user_data;
  size_t header_size = 16;
  void* real_ptr = (char*)ptr - header_size;
  allocator_free(*a, real_ptr, *(size_t*)real_ptr);
}

static void main_loop() {
  // 1. Poll and process all pending background task completions first
  app_poll_completions(g_app);

  // 2. Check if a redraw has been requested by the app state
  bool request_update = false;
  if (g_app->loading.request_update) {
    g_app->loading.request_update = false;
    request_update = true;
  }

  if (request_update) {
    imgui_impl_wasm_request_update();
  }

  if (g_app->power_save_mode && !imgui_impl_wasm_need_update()) {
    return;
  }

  imgui_impl_webgl_new_frame();
  imgui_impl_wasm_new_frame();
  ig_new_frame();

  app_update(g_app);

  ig_render();

  ig_vec2_t display_size = ig_get_io_display_size();
  ig_vec2_t fb_scale = ig_get_io_display_framebuffer_scale();
  glViewport(0, 0, (int)(display_size.x * fb_scale.x),
             (int)(display_size.y * fb_scale.y));
  glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
  glClear(GL_COLOR_BUFFER_BIT);

  imgui_impl_webgl_render_draw_data(ig_get_draw_data());
}

EMSCRIPTEN_KEEPALIVE int ztracing_init(const char* canvas_selector) {
  allocator_t default_allocator = allocator_get_default();
  g_app = (app_t*)allocator_alloc(default_allocator, sizeof(app_t));
  app_init(g_app, default_allocator);

  static allocator_t imgui_allocator;
  imgui_allocator =
      counting_allocator_get_allocator(&g_app->counting_allocator);
  ig_set_allocator_functions(imgui_alloc, imgui_free, &imgui_allocator);

  ig_create_context();
  ig_io_add_config_flags(IG_CONFIG_FLAGS_NAV_ENABLE_KEYBOARD |
                         IG_CONFIG_FLAGS_DOCKING_ENABLE);

  allocator_t allocator = imgui_allocator;
  array_list_clear(&g_canvas_selector);
  size_t len = strlen(canvas_selector) + 1;
  char* dest = (char*)array_list_append_(&g_canvas_selector, len, sizeof(char),
                                         allocator);
  memcpy(dest, canvas_selector, len);

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

  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_create_context(
      (const char*)g_canvas_selector.ptr, &attrs);
  if (ctx <= 0) {
    LOG_ERROR("failed to create webgl context for selector '%s' (error: %d)",
              (const char*)g_canvas_selector.ptr, (int)ctx);
    return 1;
  }
  emscripten_webgl_make_context_current(ctx);

  imgui_impl_wasm_init((const char*)g_canvas_selector.ptr, allocator);
  if (!imgui_impl_webgl_init(allocator)) {
    return 2;
  }

  app_on_theme_changed(g_app, platform_is_dark_mode());
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
  allocator_t allocator =
      counting_allocator_get_allocator(&g_app->counting_allocator);
  size_t len = (size_t)font_size;
  unsigned char* dest = (unsigned char*)array_list_append_(
      &g_font_data, len, sizeof(unsigned char), allocator);
  memcpy(dest, font_data, len);

  float dpi_scale = imgui_impl_wasm_get_dpi_scale();
  ig_set_font_data(g_font_data.ptr, (int)g_font_data.len, dpi_scale);
  imgui_impl_webgl_destroy_fonts_texture();
  imgui_impl_webgl_create_fonts_texture();

  imgui_impl_wasm_request_update();
}

EMSCRIPTEN_KEEPALIVE void* ztracing_malloc(int size) {
  CHECK(g_app != nullptr);
  allocator_t a = counting_allocator_get_allocator(&g_app->counting_allocator);
  return allocator_alloc(a, (size_t)size);
}

EMSCRIPTEN_KEEPALIVE void ztracing_free(void* ptr, int size) {
  CHECK(g_app != nullptr);
  allocator_t a = counting_allocator_get_allocator(&g_app->counting_allocator);
  allocator_free(a, ptr, (size_t)size);
}

EMSCRIPTEN_KEEPALIVE void ztracing_begin_session(int session_id,
                                                 const char* filename,
                                                 double input_total_bytes) {
  app_begin_session(g_app, session_id, filename, (size_t)input_total_bytes);
  imgui_impl_wasm_request_update();
}

EMSCRIPTEN_KEEPALIVE int ztracing_handle_file_chunk(int session_id, char* data,
                                                    int size,
                                                    double input_consumed_bytes,
                                                    bool is_eof) {
  return (int)app_handle_file_chunk(g_app, session_id, data, (size_t)size,
                                    (size_t)input_consumed_bytes, is_eof);
}

EMSCRIPTEN_KEEPALIVE int ztracing_get_buffered_bytes() {
  return (int)app_get_buffered_bytes(g_app);
}

EMSCRIPTEN_KEEPALIVE void ztracing_on_theme_changed(bool is_dark) {
  app_on_theme_changed(g_app, is_dark);
  imgui_impl_wasm_request_update();
}

EMSCRIPTEN_KEEPALIVE void ztracing_update(void) { main_loop(); }

EMSCRIPTEN_KEEPALIVE void ztracing_deinit(void) {
  // Stub on WASM
}

EMSCRIPTEN_KEEPALIVE bool ztracing_is_loading_active(void) {
  return g_app ? g_app->loading.active : false;
}

EMSCRIPTEN_KEEPALIVE size_t ztracing_get_allocated_bytes(void) {
  return g_app ? counting_allocator_get_allocated_bytes(
                     &g_app->counting_allocator)
               : 0;
}
