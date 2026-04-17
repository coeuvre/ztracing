#include <GLES3/gl3.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <stdio.h>

#include "third_party/imgui/imgui.h"
#include "src/imgui_impl_wasm.h"
#include "src/imgui_impl_webgl.h"

static const char* kCanvasSelector = "#canvas";

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

  ImGui::StyleColorsDark();

  EmscriptenWebGLContextAttributes attrs;
  emscripten_webgl_init_context_attributes(&attrs);
  attrs.majorVersion = 2;
  attrs.minorVersion = 0;
  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx =
      emscripten_webgl_create_context(kCanvasSelector, &attrs);
  if (ctx <= 0) {
    printf("Failed to create WebGL context for selector '%s' (error: %d)\n",
           kCanvasSelector, (int)ctx);
    return 1;
  }
  emscripten_webgl_make_context_current(ctx);

  double width, height;
  emscripten_get_element_css_size(kCanvasSelector, &width, &height);
  printf("Canvas size: %.1f x %.1f\n", width, height);

  ImGui_ImplWasm_Init(kCanvasSelector);
  ImGui_ImplWebGL_Init();

  emscripten_set_main_loop(MainLoop, 0, 1);

  return 0;
}
