#include <GLES3/gl3.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <stdio.h>

#include <vector>

#include "src/imgui_impl_wasm.h"
#include "src/imgui_impl_webgl.h"
#include "src/logging.h"
#include "third_party/imgui/imgui.h"

static const char* kCanvasSelector = "#canvas";
static std::vector<unsigned char> g_font_data;

extern "C" {
EMSCRIPTEN_KEEPALIVE void SetFontData(unsigned char* font_data, int font_size) {
  g_font_data.assign(font_data, font_data + font_size);
}
}

void MainLoop() {
  ImGui_ImplWebGL_NewFrame();
  ImGui_ImplWasm_NewFrame();
  ImGui::NewFrame();

  ImGuiIO& io = ImGui::GetIO();
  emscripten_set_canvas_element_size(
      kCanvasSelector, (int)(io.DisplaySize.x * io.DisplayFramebufferScale.x),
      (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y));

  // UI code
  {
    static float f = 0.0f;
    static int counter = 0;
    static bool show_demo_window = true;

    if (show_demo_window) ImGui::ShowDemoWindow(&show_demo_window);

    ImGui::Begin("ztracing");
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

  ImGui_ImplWebGL_RenderDrawData(ImGui::GetDrawData());
}

int main(int argc, char** argv) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  EmscriptenWebGLContextAttributes attrs;
  emscripten_webgl_init_context_attributes(&attrs);
  attrs.majorVersion = 2;
  attrs.minorVersion = 0;
  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx =
      emscripten_webgl_create_context(kCanvasSelector, &attrs);
  if (ctx <= 0) {
    LOG_ERROR("failed to create webgl context for selector '%s' (error: %d)",
              kCanvasSelector, (int)ctx);
    return 1;
  }
  emscripten_webgl_make_context_current(ctx);

  double width, height;
  emscripten_get_element_css_size(kCanvasSelector, &width, &height);

  ImGui::StyleColorsDark();
  ImGui_ImplWasm_Init(kCanvasSelector);
  ImGui_ImplWebGL_Init();

  LOG_DEBUG("ztracing initialized successfully.");

  if (!g_font_data.empty()) {
    float dpi_scale = (float)emscripten_get_device_pixel_ratio();
    io.Fonts->Clear();
    ImFontConfig font_cfg;
    font_cfg.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF(g_font_data.data(), (int)g_font_data.size(),
                                   16.0f * dpi_scale, &font_cfg);
    io.Fonts->Build();
    io.FontGlobalScale = 1.0f / dpi_scale;
    ImGui_ImplWebGL_DestroyFontsTexture();
    ImGui_ImplWebGL_CreateFontsTexture();
  }

  emscripten_set_main_loop(MainLoop, 0, 1);

  return 0;
}
