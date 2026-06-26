#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/logging.h"
#include "src/app.h"
#include "src/headless_gl.h"
#include "src/imgui_c.h"
#include "src/imgui_impl_webgl.h"
#include "src/platform.h"
#include "src/ztracing.h"

static app_t* g_app = nullptr;
static headless_gl_context_t g_gl_ctx = {};
static array_list_t g_font_data = {};

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

int ztracing_init(const char* canvas_selector) {
  (void)canvas_selector;

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

  // Initialize headless GL context (800x600 default)
  if (!headless_gl_init(&g_gl_ctx, 800, 600)) {
    LOG_ERROR("Failed to initialize headless GL context");
    return 1;
  }

  // Initialize WebGL renderer natively
  if (!imgui_impl_webgl_init(imgui_allocator)) {
    LOG_ERROR("Failed to initialize imgui_impl_webgl");
    headless_gl_shutdown(&g_gl_ctx);
    return 2;
  }

  app_on_theme_changed(g_app, platform_is_dark_mode());
  LOG_INFO("Headless ztracing initialized successfully.");
  return 0;
}

void ztracing_update(void) {
  assert(g_app != nullptr);

  ig_io_set_display_size(
      (ig_vec2_t){(float)g_gl_ctx.width, (float)g_gl_ctx.height});
  ig_io_set_delta_time(1.0f / 60.0f);

  // Poll and process all pending background task completions first
  app_poll_completions(g_app);

  ig_new_frame();
  app_update(g_app);
  ig_render();

  // Render to FBO
  imgui_impl_webgl_render_draw_data(ig_get_draw_data());
}

void ztracing_deinit(void) {
  if (!g_app) return;

  // Abort all active background jobs and wake them up from any wait conditions
  // so they can be successfully joined by platform_teardown_workers without
  // hanging.
  app_stop_jobs(g_app);

  platform_teardown_workers();

  allocator_t app_allocator =
      counting_allocator_get_allocator(&g_app->counting_allocator);
  if (g_font_data.ptr) {
    array_list_deinit(&g_font_data, app_allocator);
  }

  app_deinit(g_app);
  imgui_impl_webgl_shutdown();
  ig_destroy_context();
  headless_gl_shutdown(&g_gl_ctx);

  allocator_t default_allocator = allocator_get_default();
  allocator_free(default_allocator, g_app, sizeof(app_t));
  g_app = nullptr;
  LOG_INFO("Headless ztracing deinitialized.");
}

void ztracing_start(void) {
  // Headless start: no-op
}

void ztracing_set_font_data(unsigned char* font_data, int font_size) {
  assert(g_app != nullptr);

  array_list_clear(&g_font_data);
  allocator_t allocator =
      counting_allocator_get_allocator(&g_app->counting_allocator);
  size_t len = (size_t)font_size;
  unsigned char* dest = (unsigned char*)array_list_append_(
      &g_font_data, len, sizeof(unsigned char), allocator);
  memcpy(dest, font_data, len);

  float dpi_scale = 1.0f;
  ig_set_font_data(g_font_data.ptr, (int)g_font_data.len, dpi_scale);
  imgui_impl_webgl_destroy_fonts_texture();
  imgui_impl_webgl_create_fonts_texture();
}

void* ztracing_malloc(int size) {
  assert(g_app != nullptr);
  allocator_t a = counting_allocator_get_allocator(&g_app->counting_allocator);
  return allocator_alloc(a, (size_t)size);
}

void ztracing_free(void* ptr, int size) {
  assert(g_app != nullptr);
  allocator_t a = counting_allocator_get_allocator(&g_app->counting_allocator);
  allocator_free(a, ptr, (size_t)size);
}

void ztracing_begin_session(int session_id, const char* filename,
                            double input_total_bytes) {
  assert(g_app != nullptr);
  app_begin_session(g_app, session_id, filename, (size_t)input_total_bytes);
}

int ztracing_handle_file_chunk(int session_id, char* data, int size,
                               double input_consumed_bytes, bool is_eof) {
  assert(g_app != nullptr);
  return (int)app_handle_file_chunk(g_app, session_id, data, (size_t)size,
                                    (size_t)input_consumed_bytes, is_eof);
}

int ztracing_get_buffered_bytes(void) {
  assert(g_app != nullptr);
  return (int)app_get_buffered_bytes(g_app);
}

void ztracing_on_theme_changed(bool is_dark) {
  assert(g_app != nullptr);
  app_on_theme_changed(g_app, is_dark);
}

bool ztracing_is_loading_active(void) {
  return g_app ? g_app->loading.active : false;
}

size_t ztracing_get_allocated_bytes(void) {
  return g_app ? counting_allocator_get_allocated_bytes(
                     &g_app->counting_allocator)
               : 0;
}

app_t* ztracing_headless_get_app(void) { return g_app; }
