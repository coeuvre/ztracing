#include "src/app.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include "src/colors.h"
#include "src/format.h"
#include "src/logging.h"
#include "src/platform.h"
#include "src/trace_viewer.h"
#include "src/loading_screen.h"
#include "src/welcome_screen.h"
#include "third_party/imgui/imgui.h"
#include "third_party/imgui/imgui_internal.h"

static void trace_loading_organize_tracks(TraceLoadingState* loading) {
  track_organize(loading->trace_data, loading->allocator, loading->theme,
                 &loading->trace_viewer->tracks,
                 &loading->trace_viewer->viewport.min_ts,
                 &loading->trace_viewer->viewport.max_ts);

  trace_viewer_reset_view(loading->trace_viewer);
  LOG_INFO("organized %zu tracks, time range: [%lld, %lld]",
           loading->trace_viewer->tracks.size,
           (long long)loading->trace_viewer->viewport.min_ts,
           (long long)loading->trace_viewer->viewport.max_ts);
}

static void trace_loading_worker(TraceLoadingState* loading) {
  LOG_DEBUG("trace loading worker started");
  while (!loading->worker_should_abort) {
    TraceChunk chunk = {};
    {
      std::unique_lock<std::mutex> lock(loading->chunk_queue.mutex);
      loading->chunk_queue.cv.wait(lock, [loading] {
        return !loading->chunk_queue.queue.empty() ||
               loading->chunk_queue.closed || loading->worker_should_abort;
      });

      if (loading->worker_should_abort) break;

      if (loading->chunk_queue.queue.empty()) {
        if (loading->chunk_queue.closed) break;
        continue;
      }

      chunk = loading->chunk_queue.queue.front();
      loading->chunk_queue.queue.pop();
    }

    if (chunk.data) {
      trace_parser_feed(&loading->parser, chunk.data, chunk.size, chunk.is_eof);
      allocator_free(loading->allocator, chunk.data, chunk.size);
    } else if (chunk.is_eof) {
      trace_parser_feed(&loading->parser, nullptr, 0, true);
    }

    TraceEvent event;
    while (trace_parser_next(&loading->parser, &event)) {
      if (loading->worker_should_abort) break;
      loading->event_count++;
      trace_data_add_event(loading->trace_data, loading->allocator,
                           loading->theme, &event);
    }
    loading->request_update = true;

    if (chunk.is_eof) {
      double duration_ms = platform_get_now() - loading->start_time;
      double duration_s = duration_ms / 1000.0;
      double speed_mb_s =
          duration_s > 0.0
              ? ((double)loading->total_bytes / (1024.0 * 1024.0)) / duration_s
              : 0.0;
      LOG_INFO("parsed %zu events (total) in %.3f ms (%.2f mb/s)",
               loading->event_count.load(), duration_ms, speed_mb_s);
      trace_parser_deinit(&loading->parser);
      trace_loading_organize_tracks(loading);
      loading->active = false;
      loading->request_update = true;
      break;
    }
  }
  LOG_DEBUG("trace loading worker finished");
}

static void app_apply_theme(App* app, const Theme* theme) {
  if (app->theme == theme) return;
  app->theme = theme;
  if (theme == theme_get_dark()) {
    ImGui::StyleColorsDark();
  } else {
    ImGui::StyleColorsLight();
  }

  // Re-compute all event colors when theme changes
  for (size_t i = 0; i < app->trace_data.events.size; i++) {
    trace_data_update_event_color(&app->trace_data, (uint32_t)i, app->theme);
  }

  // Re-compute all counter track colors when theme changes
  track_update_colors(&app->trace_viewer.tracks, &app->trace_data, app->theme);
}

static void app_stop_worker(App* app) {
  if (app->loading.worker_thread.joinable()) {
    app->loading.worker_should_abort = true;
    {
      std::lock_guard<std::mutex> lock(app->loading.chunk_queue.mutex);
      app->loading.chunk_queue.cv.notify_all();
    }
    app->loading.worker_thread.join();
  }
}

void app_init(App* app, Allocator allocator) {
  app->allocator = allocator;
  app->theme_mode = THEME_MODE_AUTO;
  app->theme = nullptr;
  app_apply_theme(
      app, platform_is_dark_mode() ? theme_get_dark() : theme_get_light());
  app->power_save_mode = true;
  app->first_frame = true;
  app->show_metrics_window = false;
  app->show_about_window = false;

  app->loading.event_count = 0;
  app->loading.total_bytes = 0;
  app->loading.start_time = 0;
  app->loading.active = false;
  app->loading.session_id = 0;
  app->loading.filename = {};
  app->loading.worker_should_abort = false;
  app->loading.request_update = false;

  app->loading.allocator = app->allocator;
  app->loading.trace_data = &app->trace_data;
  app->loading.trace_viewer = &app->trace_viewer;

  trace_data_init(&app->trace_data, app->allocator);
  trace_viewer_init(&app->trace_viewer, app->allocator);
}

void app_deinit(App* app) {
  app_stop_worker(app);
  if (app->loading.active) {
    trace_parser_deinit(&app->loading.parser);
  }
  trace_data_deinit(&app->trace_data, app->allocator);
  array_list_deinit(&app->loading.filename, app->allocator);
  trace_viewer_deinit(&app->trace_viewer, app->allocator);
}

void app_update(App* app) {
  // 0. Main Menu Bar
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("View")) {
      if (ImGui::MenuItem("Reset View")) {
        trace_viewer_reset_view(&app->trace_viewer);
      }
      ImGui::Separator();

      ImGui::MenuItem("Power-save Mode", nullptr, &app->power_save_mode);
      ImGui::MenuItem("Details Panel", nullptr, &app->trace_viewer.show_details_panel);

      ImGui::Separator();
      if (ImGui::BeginMenu("Theme")) {
        if (ImGui::MenuItem("Auto", nullptr, app->theme_mode == THEME_MODE_AUTO)) {
          app->theme_mode = THEME_MODE_AUTO;
          app_on_theme_changed(app);
        }
        if (ImGui::MenuItem("Dark", nullptr, app->theme_mode == THEME_MODE_DARK)) {
          app->theme_mode = THEME_MODE_DARK;
          app_apply_theme(app, theme_get_dark());
        }
        if (ImGui::MenuItem("Light", nullptr, app->theme_mode == THEME_MODE_LIGHT)) {
          app->theme_mode = THEME_MODE_LIGHT;
          app_apply_theme(app, theme_get_light());
        }
        ImGui::EndMenu();
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Tools")) {
      ImGui::MenuItem("Metrics/Debugger", nullptr, &app->show_metrics_window);
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
      ImGui::MenuItem("About Dear ImGui", nullptr, &app->show_about_window);
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }

  // 1. Setup DockSpace
  ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
  ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(
      0, ImGui::GetMainViewport(), dockspace_flags);

  if (app->first_frame) {
    app->first_frame = false;
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id,
                              dockspace_flags | ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

    ImGuiID dock_id_main = dockspace_id;
    ImGuiID dock_id_bottom = ImGui::DockBuilderSplitNode(
        dock_id_main, ImGuiDir_Down, 0.25f, nullptr, &dock_id_main);
    ImGui::DockBuilderDockWindow("Details", dock_id_bottom);

    // Hide tab bar and prevent docking over the main viewport area
    ImGuiDockNode* main_node = ImGui::DockBuilderGetNode(dock_id_main);
    if (main_node) {
      main_node->LocalFlags |=
          ImGuiDockNodeFlags_NoTabBar | ImGuiDockNodeFlags_NoDockingOverMe;
    }

    ImGui::DockBuilderDockWindow("Main Viewport", dock_id_main);
    ImGui::DockBuilderFinish(dockspace_id);
  }

  if (app->show_metrics_window) ImGui::ShowMetricsWindow(&app->show_metrics_window);
  if (app->show_about_window) ImGui::ShowAboutWindow(&app->show_about_window);

  // 2. Scene Rendering
  ImGuiWindowFlags viewport_flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoScrollbar;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

  if (ImGui::Begin("Main Viewport", nullptr, viewport_flags)) {
    if (app->loading.active) {
      const char* filename =
          app->loading.filename.size > 0 ? app->loading.filename.data : "";
      loading_screen_draw(filename, (size_t)app->loading.event_count,
                          (size_t)app->loading.total_bytes, app->theme);
    } else if (app->trace_data.events.size > 0) {
      trace_viewer_draw(&app->trace_viewer, &app->trace_data, app->allocator,
                        app->theme);
    } else {
      welcome_screen_draw(app->theme);
    }
  }
  ImGui::End();
  ImGui::PopStyleVar(3);
}

void app_on_theme_changed(App* app) {
  if (app->theme_mode == THEME_MODE_AUTO) {
    app_apply_theme(
        app, platform_is_dark_mode() ? theme_get_dark() : theme_get_light());
  }
}

void app_begin_session(App* app, int session_id, const char* filename) {
  app_stop_worker(app);

  trace_parser_init(&app->loading.parser, app->allocator);
  trace_data_clear(&app->trace_data, app->allocator);
  app->loading.event_count = 0;
  app->loading.total_bytes = 0;
  app->loading.start_time = platform_get_now();
  app->loading.active = true;
  app->loading.session_id = session_id;
  app->loading.theme = app->theme;
  app->trace_viewer.selected_event_index = -1;

  app->loading.worker_should_abort = false;
  {
    std::lock_guard<std::mutex> lock(app->loading.chunk_queue.mutex);
    while (!app->loading.chunk_queue.queue.empty()) {
      TraceChunk chunk = app->loading.chunk_queue.queue.front();
      if (chunk.data) allocator_free(app->allocator, chunk.data, chunk.size);
      app->loading.chunk_queue.queue.pop();
    }
    app->loading.chunk_queue.closed = false;
  }

  array_list_clear(&app->loading.filename);
  if (filename) {
    array_list_append(&app->loading.filename, app->allocator, filename,
                      strlen(filename) + 1);
  }

  app->loading.worker_thread = std::thread(trace_loading_worker, &app->loading);
}

void app_handle_file_chunk(App* app, int session_id, const char* data,
                           size_t size, bool is_eof) {
  if (session_id != app->loading.session_id) {
    return;
  }

  TraceChunk chunk = {};
  chunk.size = size;
  chunk.is_eof = is_eof;
  if (size > 0) {
    app->loading.total_bytes += size;
    chunk.data = (char*)allocator_alloc(app->allocator, size);
    memcpy(chunk.data, data, size);
  }

  {
    std::lock_guard<std::mutex> lock(app->loading.chunk_queue.mutex);
    app->loading.chunk_queue.queue.push(chunk);
    if (is_eof) app->loading.chunk_queue.closed = true;
    app->loading.chunk_queue.cv.notify_one();
  }
}
