#include "src/app.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>

#include "src/colors.h"
#include "src/loading_screen.h"
#include "src/logging.h"
#include "src/platform.h"
#include "src/welcome_screen.h"
#include "third_party/imgui/imgui.h"
#include "third_party/imgui/imgui_internal.h"

static void trace_loading_worker(TraceLoadingState* loading) {
  LOG_DEBUG("trace_loading_worker started (session_id: %d)",
            loading->session_id);

  size_t total_discarded_bytes = 0;

  while (true) {
    TraceChunk chunk = {};
    {
      std::unique_lock<std::mutex> lock(loading->chunk_queue.mutex);
      loading->chunk_queue.cv.wait(lock, [loading] {
        return !loading->chunk_queue.queue.empty() ||
               loading->worker_should_abort;
      });

      if (loading->worker_should_abort) break;

      chunk = loading->chunk_queue.queue.front();
      loading->chunk_queue.queue.pop();
    }

    if (chunk.data) {
      total_discarded_bytes += trace_parser_feed(
          &loading->parser, loading->allocator, chunk.data, chunk.size,
          chunk.is_eof);
      allocator_free(loading->allocator, chunk.data, chunk.size);

      loading->input_consumed_bytes.store(chunk.input_consumed_bytes,
                                            std::memory_order_relaxed);

      {
        std::lock_guard<std::mutex> lock(loading->chunk_queue.mutex);
        loading->chunk_queue.queue_size_bytes -= chunk.size;
      }
    }

    TraceEvent event;
    while (trace_parser_next(&loading->parser, loading->allocator, &event)) {
      trace_data_add_event(loading->trace_data, loading->allocator,
                           loading->theme, &event);
      loading->event_count.fetch_add(1, std::memory_order_relaxed);
      loading->total_bytes.store(total_discarded_bytes + loading->parser.pos,
                                 std::memory_order_relaxed);
    }

    loading->request_update.store(true, std::memory_order_relaxed);

    if (chunk.is_eof) break;
  }

  if (!loading->worker_should_abort) {
    double duration_ms = platform_get_now() - loading->start_time;
    double duration_s = duration_ms / 1000.0;
    double speed_mb_s =
        duration_s > 0.0
            ? ((double)loading->total_bytes / (1024.0 * 1024.0)) / duration_s
            : 0.0;
    LOG_INFO("parsed %zu events (total) in %.3f ms (%.2f mb/s)",
             loading->event_count.load(), duration_ms, speed_mb_s);

    int64_t min_ts, max_ts;
    track_organize(loading->trace_data, loading->allocator, loading->theme,
                   &loading->trace_viewer->tracks, &min_ts, &max_ts);
    loading->trace_viewer->viewport.min_ts = min_ts;
    loading->trace_viewer->viewport.max_ts = max_ts;
    trace_viewer_reset_view(loading->trace_viewer);
    loading->active = false;
    loading->request_update = true;
    LOG_DEBUG("trace_loading_worker finished (session_id: %d)",
              loading->session_id);
  } else {
    LOG_DEBUG("trace_loading_worker aborted (session_id: %d)",
              loading->session_id);
  }
}

static void app_apply_theme(App* app, const Theme* theme) {
  if (app->theme == theme) return;
  app->theme = theme;
  if (theme == theme_get_dark()) {
    ImGui::StyleColorsDark();
  } else {
    ImGui::StyleColorsLight();
  }

  if (app->loading.active) return;

  // Re-compute all event colors when theme changes
  for (size_t i = 0; i < app->trace_data.events.size; i++) {
    trace_data_update_event_color(&app->trace_data, (uint32_t)i, app->theme);
  }

  // Re-compute all counter track colors when theme changes
  track_update_colors(&app->trace_viewer.tracks, &app->trace_data, app->theme);
}

static void app_stop_worker(App* app) {
  if (app->loading.active) {
    app->loading.worker_should_abort = true;
    {
      std::lock_guard<std::mutex> lock(app->loading.chunk_queue.mutex);
      app->loading.chunk_queue.cv.notify_one();
    }
    if (app->loading.worker_thread.joinable()) {
      app->loading.worker_thread.join();
    }
    trace_parser_deinit(&app->loading.parser, app->allocator);
    app->loading.active = false;
  }
}

App app_init(Allocator parent) {
  return App{
      .counting_allocator = counting_allocator_init(parent),
      .power_save_mode = true,
      .first_frame = true,
  };
}

void app_deinit(App* app) {
  app_stop_worker(app);
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

    // Right-aligned memory usage
    size_t allocated =
        counting_allocator_get_allocated_bytes(&app->counting_allocator);
    char mem_buf[64];
    snprintf(mem_buf, sizeof(mem_buf), "%.1f MB",
             (double)allocated / (1024.0 * 1024.0));
    float text_width = ImGui::CalcTextSize(mem_buf).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - text_width -
                    ImGui::GetStyle().ItemSpacing.x * 2.0f);
    ImGui::TextDisabled("%s", mem_buf);

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
                          (size_t)app->loading.total_bytes,
                          (size_t)app->loading.input_consumed_bytes,
                          (size_t)app->loading.input_total_bytes, app->theme);
    } else if (app->trace_data.events.size > 0 && !app->loading.active) {
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

void app_begin_session(App* app, int session_id, const char* filename,
                       size_t input_total_bytes) {
  app_stop_worker(app);

  app->loading.trace_data = &app->trace_data;
  app->loading.trace_viewer = &app->trace_viewer;

  app->loading.parser = {};
  trace_data_clear(&app->trace_data, app->allocator);
  app->loading.event_count = 0;
  app->loading.total_bytes = 0;
  app->loading.input_total_bytes = input_total_bytes;
  app->loading.input_consumed_bytes = 0;
  app->loading.start_time = platform_get_now();
  app->loading.active = true;
  app->loading.session_id = session_id;
  app->loading.allocator = app->allocator;
  app->loading.theme = app->theme;
  app->trace_viewer.focused_event_idx = -1;
  array_list_clear(&app->trace_viewer.selected_event_indices);

  app->loading.worker_should_abort = false;
  {
    std::lock_guard<std::mutex> lock(app->loading.chunk_queue.mutex);
    while (!app->loading.chunk_queue.queue.empty()) {
      TraceChunk chunk = app->loading.chunk_queue.queue.front();
      if (chunk.data) {
        allocator_free(app->allocator, chunk.data, chunk.size);
      }
      app->loading.chunk_queue.queue_size_bytes -= chunk.size;
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

size_t app_handle_file_chunk(App* app, int session_id, char* data,
                           size_t size, size_t input_consumed_bytes,
                           bool is_eof) {
  if (session_id != app->loading.session_id) {
    // If this is an old session, free the data immediately.
    if (data && size > 0) {
      allocator_free(app->allocator, data, size);
    }
    return app->loading.chunk_queue.queue_size_bytes;
  }

  TraceChunk chunk = {};
  chunk.size = size;
  chunk.input_consumed_bytes = input_consumed_bytes;
  chunk.is_eof = is_eof;
  chunk.data = data;

  {
    std::lock_guard<std::mutex> lock(app->loading.chunk_queue.mutex);
    app->loading.chunk_queue.queue.push(chunk);
    app->loading.chunk_queue.queue_size_bytes += size;
    if (is_eof) app->loading.chunk_queue.closed = true;
    app->loading.chunk_queue.cv.notify_one();
  }

  return app->loading.chunk_queue.queue_size_bytes;
}

size_t app_get_queue_size(App* app) {
  return app->loading.chunk_queue.queue_size_bytes;
}
