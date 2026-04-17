#include <GLES3/gl3.h>
#include <emscripten.h>
#include <emscripten/html5.h>

#include "src/allocator.h"
#include "src/array_list.h"
#include "src/imgui_impl_wasm.h"
#include "src/imgui_impl_webgl.h"
#include "src/logging.h"
#include "third_party/imgui/imgui.h"

static const char* CANVAS_SELECTOR = "#canvas";
static ArrayList<unsigned char> g_font_data;
static bool g_power_save_mode = true;

extern "C" {
EMSCRIPTEN_KEEPALIVE void ztracing_set_font_data(unsigned char* font_data,
                                                 int font_size) {
  array_list_clear(&g_font_data);
  array_list_append(&g_font_data, allocator_get_default(), font_data,
                    (size_t)font_size);
  imgui_impl_wasm_request_update();
}
}

void main_loop() {
  if (g_power_save_mode && !imgui_impl_wasm_need_update()) {
    return;
  }

  imgui_impl_webgl_new_frame();
  imgui_impl_wasm_new_frame();
  ImGui::NewFrame();

  ImGuiIO& io = ImGui::GetIO();
  emscripten_set_canvas_element_size(
      CANVAS_SELECTOR, (int)(io.DisplaySize.x * io.DisplayFramebufferScale.x),
      (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y));

  // UI code
  {
    static float f = 0.0f;
    static int counter = 0;
    static bool show_demo_window = false;

    if (show_demo_window) ImGui::ShowDemoWindow(&show_demo_window);

    ImGui::Begin("ztracing");
    ImGui::Checkbox("Power-save Mode", &g_power_save_mode);
    ImGui::Checkbox("Show Demo Window", &show_demo_window);
    ImGui::Separator();
    ImGui::Text("Welcome to the Chrome Tracing Replacement.");
    ImGui::SliderFloat("float", &f, 0.0f, 1.0f);
    if (ImGui::Button("Button")) counter++;
    ImGui::SameLine();
    ImGui::Text("counter = %d", counter);
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
                1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
    ImGui::End();
  }

  ImGui::Render();

  glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
  glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
  glClear(GL_COLOR_BUFFER_BIT);

  imgui_impl_webgl_render_draw_data(ImGui::GetDrawData());
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  EmscriptenWebGLContextAttributes attrs;
  emscripten_webgl_init_context_attributes(&attrs);
  attrs.majorVersion = 2;
  attrs.minorVersion = 0;
  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx =
      emscripten_webgl_create_context(CANVAS_SELECTOR, &attrs);
  if (ctx <= 0) {
    LOG_ERROR("failed to create webgl context for selector '%s' (error: %d)",
              CANVAS_SELECTOR, (int)ctx);
    return 1;
  }
  emscripten_webgl_make_context_current(ctx);

  double width, height;
  emscripten_get_element_css_size(CANVAS_SELECTOR, &width, &height);

  ImGui::StyleColorsDark();
  Allocator allocator = allocator_get_default();
  imgui_impl_wasm_init(CANVAS_SELECTOR, allocator);
  imgui_impl_webgl_init(allocator);

  LOG_DEBUG("ztracing initialized successfully.");

  if (g_font_data.size > 0) {
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
  }

  emscripten_set_main_loop(main_loop, 0, 1);

  return 0;
}
