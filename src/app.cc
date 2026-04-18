#include "src/app.h"

#include "src/platform.h"

#include "src/logging.h"
#include "third_party/imgui/imgui.h"

void app_init(App* app, Allocator allocator) {
  *app = {};
  app->allocator = allocator;
  app->power_save_mode = true;
}

void app_deinit(App* app) {
  if (app->trace_parser_active) {
    trace_parser_deinit(&app->trace_parser);
  }
}

void app_update(App* app) {
  // UI code
  {
    static float f = 0.0f;
    static int counter = 0;
    static bool show_demo_window = false;

    if (show_demo_window) ImGui::ShowDemoWindow(&show_demo_window);

    ImGui::Begin("ztracing");
    ImGui::Checkbox("Power-save Mode", &app->power_save_mode);
    ImGui::Checkbox("Show Demo Window", &show_demo_window);
    ImGui::Separator();
    ImGui::Text("Welcome to the Chrome Tracing Replacement.");
    ImGui::SliderFloat("float", &f, 0.0f, 1.0f);
    if (ImGui::Button("Button")) counter++;
    ImGui::SameLine();
    ImGui::Text("counter = %d", counter);
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
                1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

    if (app->trace_parser_active) {
      ImGui::Separator();
      ImGui::Text("Loading trace...");
      ImGui::Text("Parsed %zu events", app->trace_event_count);
      ImGui::Text("%.2f MB loaded",
                  (double)app->trace_total_bytes / (1024.0 * 1024.0));
    }

    ImGui::End();
  }
}

void app_handle_file_chunk(App* app, const char* data, size_t size,
                           bool is_eof) {
  if (!app->trace_parser_active) {
    trace_parser_init(&app->trace_parser, app->allocator);
    app->trace_event_count = 0;
    app->trace_total_bytes = 0;
    app->trace_start_time = platform_get_now();
    app->trace_parser_active = true;
  }

  if (size > 0) {
    app->trace_total_bytes += size;
    trace_parser_feed(&app->trace_parser, data, size, is_eof);
  } else if (is_eof) {
    trace_parser_feed(&app->trace_parser, nullptr, 0, true);
  }

  TraceEvent event;
  while (trace_parser_next(&app->trace_parser, &event)) {
    app->trace_event_count++;
  }

  if (is_eof) {
    double duration_ms = platform_get_now() - app->trace_start_time;
    double duration_s = duration_ms / 1000.0;
    double speed_mb_s =
        duration_s > 0.0
            ? ((double)app->trace_total_bytes / (1024.0 * 1024.0)) / duration_s
            : 0.0;
    LOG_INFO("parsed %zu events in %.3f ms (%.2f mb/s)", app->trace_event_count,
             duration_ms, speed_mb_s);
    trace_parser_deinit(&app->trace_parser);
    app->trace_parser_active = false;
  }
}
