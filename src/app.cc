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

static void trace_loading_job(void* user_data) {
  TraceLoadingState* loading = (TraceLoadingState*)user_data;
  int my_session_id = loading->session_id;
  Allocator allocator = loading->allocator;

  LOG_DEBUG("trace_loading_job started (session_id: %d)",
            my_session_id);

  size_t total_discarded_bytes = 0;

  while (true) {
    TraceChunk chunk = {};
    {
      std::unique_lock<std::mutex> lock(loading->chunk_queue.mutex);
      loading->chunk_queue.cv.wait(lock, [loading, my_session_id] {
        return !loading->chunk_queue.queue.empty() ||
               loading->jobs_should_abort.load() ||
               loading->session_id != my_session_id;
      });

      if (loading->jobs_should_abort.load() ||
          loading->session_id != my_session_id)
        break;

      chunk = loading->chunk_queue.queue.front();
      loading->chunk_queue.queue.pop();
    }

    if (chunk.data) {
      total_discarded_bytes += trace_parser_feed(
          &loading->parser, allocator, chunk.data, chunk.size,
          chunk.is_eof);
      allocator_free(allocator, chunk.data, chunk.size);

      loading->input_consumed_bytes.store(chunk.input_consumed_bytes,
                                            std::memory_order_relaxed);

      {
        std::lock_guard<std::mutex> lock(loading->chunk_queue.mutex);
        loading->chunk_queue.queue_size_bytes.fetch_sub(chunk.size,
                                                          std::memory_order_relaxed);
      }
    }

    TraceEvent event;
    while (trace_parser_next(&loading->parser, allocator, &event)) {
      trace_data_add_event(loading->trace_data, allocator,
                           loading->theme, &event);
      loading->event_count.fetch_add(1, std::memory_order_relaxed);
      loading->total_bytes.store(total_discarded_bytes + loading->parser.pos,
                                 std::memory_order_relaxed);
    }

    loading->request_update.store(true, std::memory_order_relaxed);

    if (chunk.is_eof) break;
  }

  if (!loading->jobs_should_abort.load() &&
      loading->session_id == my_session_id) {
    double duration_ms = platform_get_now() - loading->start_time;
    double duration_s = duration_ms / 1000.0;
    double speed_mb_s =
        duration_s > 0.0
            ? ((double)loading->total_bytes.load() / (1024.0 * 1024.0)) /
                  duration_s
            : 0.0;
    LOG_INFO("parsed %zu events (total) in %.3f ms (%.2f mb/s)",
             loading->event_count.load(), duration_ms, speed_mb_s);

    int64_t min_ts, max_ts;
    track_organize(loading->trace_data, allocator, loading->theme,
                   &loading->trace_viewer->tracks, &min_ts, &max_ts);
    loading->trace_viewer->viewport.min_ts = min_ts;
    loading->trace_viewer->viewport.max_ts = max_ts;
    trace_viewer_reset_view(loading->trace_viewer);
    loading->request_update.store(true, std::memory_order_relaxed);
    LOG_DEBUG("trace_loading_job finished (session_id: %d)",
              my_session_id);
  } else {
    LOG_DEBUG("trace_loading_job aborted/superseded (session_id: %d)",
              my_session_id);
  }

  trace_parser_deinit(&loading->parser, allocator);

  {
    std::lock_guard<std::mutex> lock(loading->quit_mutex);
    loading->active.store(false);
    loading->quit_cv.notify_all();
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

  if (app->loading.active.load()) return;

  // Re-compute all event colors when theme changes
  for (size_t i = 0; i < app->trace_data.events.size; i++) {
    trace_data_update_event_color(&app->trace_data, (uint32_t)i, app->theme);
  }

  // Re-compute all counter track colors when theme changes
  track_update_colors(&app->trace_viewer.tracks, &app->trace_data, app->theme);
}

static void app_stop_jobs(App* app) {
  app->loading.jobs_should_abort.store(true);
  {
    std::lock_guard<std::mutex> lock(app->loading.chunk_queue.mutex);
    app->loading.chunk_queue.cv.notify_all();
  }

  if (app->loading.active.load()) {
    std::unique_lock<std::mutex> lock(app->loading.quit_mutex);
    app->loading.quit_cv.wait(lock, [app] { return !app->loading.active.load(); });
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
  app_stop_jobs(app);

  Allocator allocator = counting_allocator_get_allocator(&app->counting_allocator);
  trace_data_deinit(&app->trace_data, allocator);
  array_list_deinit(&app->loading.filename, allocator);
  trace_viewer_deinit(&app->trace_viewer, allocator);
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
      if (ImGui::MenuItem("Shortcuts", "?")) {
        app->show_shortcuts_window = true;
      }
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

  if (ImGui::IsKeyPressed(ImGuiKey_Slash) && ImGui::GetIO().KeyShift && !ImGui::GetIO().WantTextInput) {
    app->show_shortcuts_window = !app->show_shortcuts_window;
  }

  ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImVec2 viewport_size = ImGui::GetMainViewport()->Size;
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(viewport_size.x * 0.7f, viewport_size.y * 0.7f), ImGuiCond_Appearing);

  if (app->show_shortcuts_window) {
    ImGui::OpenPopup("Shortcuts");
  }

  ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertU32ToFloat4(app->theme->viewport_bg));
  if (ImGui::BeginPopupModal("Shortcuts", &app->show_shortcuts_window, 
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
    // Close when clicking outside
    if (ImGui::IsMouseClicked(0)) {
        ImVec2 mouse_pos = ImGui::GetMousePos();
        ImVec2 window_pos = ImGui::GetWindowPos();
        ImVec2 window_size = ImGui::GetWindowSize();
        bool inside = mouse_pos.x >= window_pos.x && mouse_pos.x <= window_pos.x + window_size.x &&
                     mouse_pos.y >= window_pos.y && mouse_pos.y <= window_pos.y + window_size.y;
        if (!inside) {
            app->show_shortcuts_window = false;
            app->trace_viewer.ignore_next_release = true;
            ImGui::CloseCurrentPopup();
        }
    }

    if (ImGui::BeginChild("CheatsheetContent", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar)) {
      auto add_section = [&](const char* title, auto content_func) {
        ImGui::BeginGroup();
        ImGui::Spacing();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(app->theme->track_text), "%s", title);
        ImGui::Separator();
        ImGui::Spacing();
        
        if (ImGui::BeginTable(title, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch);
            content_func();
            ImGui::EndTable();
        }
        ImGui::EndGroup();
      };

      auto add_row = [&](const char* action, const char* shortcut) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(action);
        ImGui::TableNextColumn();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(app->theme->ruler_text), "%s", shortcut);
      };

      float width = ImGui::GetContentRegionAvail().x;
      float gap = 40.0f;
      float col_w = (width - gap) * 0.5f;

      // Left Column
      ImGui::BeginChild("LeftCol", ImVec2(col_w, 0), false, ImGuiWindowFlags_NoScrollbar);
      add_section("GENERAL", [&]() {
        add_row("Toggle Shortcuts", "?");
        add_row("Toggle Details", "Menu > View");
        add_row("Metrics / Debug", "Menu > Tools");
      });
      add_section("NAVIGATION", [&]() {
        add_row("Zoom In/Out", "Ctrl + Scroll");
        add_row("Zoom to Event", "Double Click");
        add_row("Pan Horizontally", "Shift + Scroll");
        add_row("Pan (Any Direction)", "Left Drag");
        add_row("Reset View", "Menu > View");
      });
      ImGui::EndChild();

      ImGui::SameLine(0, gap);

      // Right Column
      ImGui::BeginChild("RightCol", ImVec2(col_w, 0), false, ImGuiWindowFlags_NoScrollbar);
      add_section("SELECTION", [&]() {
        add_row("Timeline Selection", "Drag on Ruler");
        add_row("Rectangle Select", "Shift + Drag");
        add_row("Select Event", "Left Click");
        add_row("Clear Selection", "Click Background");
        add_row("Clear Focused", "Click Background");
      });
      ImGui::EndChild();

      ImGui::EndChild();
    }
    ImGui::EndPopup();
  }
  ImGui::PopStyleColor();

  // 2. Scene Rendering
  ImGuiWindowFlags viewport_flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoScrollbar;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

  if (ImGui::Begin("Main Viewport", nullptr, viewport_flags)) {
    if (app->loading.active.load()) {
      const char* filename =
          app->loading.filename.size > 0 ? app->loading.filename.data : "";
      loading_screen_draw(filename, (size_t)app->loading.event_count.load(),
                          (size_t)app->loading.total_bytes.load(),
                          (size_t)app->loading.input_consumed_bytes.load(),
                          (size_t)app->loading.input_total_bytes.load(), app->theme);
    } else if (app->trace_data.events.size > 0 && !app->loading.active.load()) {
      Allocator allocator = counting_allocator_get_allocator(&app->counting_allocator);
      trace_viewer_draw(&app->trace_viewer, &app->trace_data, allocator,
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
  app_stop_jobs(app);

  app->loading.trace_data = &app->trace_data;
  app->loading.trace_viewer = &app->trace_viewer;
  app->loading.allocator = counting_allocator_get_allocator(&app->counting_allocator);

  app->loading.parser = {};
  Allocator allocator = app->loading.allocator;
  trace_data_clear(&app->trace_data, allocator);
  app->loading.event_count.store(0, std::memory_order_relaxed);
  app->loading.total_bytes.store(0, std::memory_order_relaxed);
  app->loading.input_total_bytes.store(input_total_bytes, std::memory_order_relaxed);
  app->loading.input_consumed_bytes.store(0, std::memory_order_relaxed);
  app->loading.start_time = platform_get_now();
  app->loading.active.store(true, std::memory_order_relaxed);
  app->loading.session_id = session_id;
  app->loading.theme = app->theme;
  app->trace_viewer.focused_event_idx = -1;
  app->trace_viewer.request_scroll_to_focused_event = false;
  array_list_clear(&app->trace_viewer.selected_event_indices);

  app->loading.jobs_should_abort.store(false, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(app->loading.chunk_queue.mutex);
    while (!app->loading.chunk_queue.queue.empty()) {
      TraceChunk chunk = app->loading.chunk_queue.queue.front();
      if (chunk.data) {
        allocator_free(allocator, chunk.data, chunk.size);
      }
      app->loading.chunk_queue.queue.pop();
    }
    app->loading.chunk_queue.queue_size_bytes.store(0, std::memory_order_relaxed);
    app->loading.chunk_queue.closed = false;
  }

  array_list_clear(&app->loading.filename);
  if (filename) {
    array_list_append(&app->loading.filename, allocator, filename,
                      strlen(filename) + 1);
  }

  {
    std::lock_guard<std::mutex> lock(app->loading.chunk_queue.mutex);
    app->loading.chunk_queue.cv.notify_all();
  }

  platform_submit_job((PlatformJobFn)trace_loading_job, &app->loading);
}

size_t app_handle_file_chunk(App* app, int session_id, char* data,
                           size_t size, size_t input_consumed_bytes,
                           bool is_eof) {
  if (session_id != app->loading.session_id) {
    // If this is an old session, free the data immediately.
    if (data && size > 0) {
      Allocator allocator = counting_allocator_get_allocator(&app->counting_allocator);
      allocator_free(allocator, data, size);
    }
    return app->loading.chunk_queue.queue_size_bytes.load(std::memory_order_relaxed);
  }

  TraceChunk chunk = {
      .data = data,
      .size = size,
      .input_consumed_bytes = input_consumed_bytes,
      .is_eof = is_eof,
  };

  {
    std::lock_guard<std::mutex> lock(app->loading.chunk_queue.mutex);
    app->loading.chunk_queue.queue.push(chunk);
    app->loading.chunk_queue.queue_size_bytes.fetch_add(size, std::memory_order_relaxed);
    if (is_eof) app->loading.chunk_queue.closed = true;
    app->loading.chunk_queue.cv.notify_all();
  }

  return app->loading.chunk_queue.queue_size_bytes.load(std::memory_order_relaxed);
}

size_t app_get_queue_size(App* app) {
  return app->loading.chunk_queue.queue_size_bytes.load(std::memory_order_relaxed);
}
