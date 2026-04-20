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

static void app_organize_tracks(App* app) {
  track_organize(&app->trace_data, app->allocator, app->theme,
                 &app->trace_viewer.tracks, &app->trace_viewer.viewport.min_ts,
                 &app->trace_viewer.viewport.max_ts);

  trace_viewer_reset_view(&app->trace_viewer);
  LOG_INFO("organized %zu tracks, time range: [%lld, %lld]",
           app->trace_viewer.tracks.size,
           (long long)app->trace_viewer.viewport.min_ts,
           (long long)app->trace_viewer.viewport.max_ts);
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

void app_init(App* app, Allocator allocator) {
  *app = {};
  app->allocator = allocator;
  app->theme_mode = THEME_MODE_AUTO;
  app_apply_theme(
      app, platform_is_dark_mode() ? theme_get_dark() : theme_get_light());
  app->power_save_mode = true;
  app->first_frame = true;
  app->show_metrics_window = false;
  app->show_about_window = false;

  trace_data_init(&app->trace_data, app->allocator);
  trace_viewer_init(&app->trace_viewer, app->allocator);
}

void app_deinit(App* app) {
  if (app->trace_parser_active) {
    trace_parser_deinit(&app->trace_parser);
  }
  trace_data_deinit(&app->trace_data, app->allocator);
  array_list_deinit(&app->trace_filename, app->allocator);
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

    // Hide tab bar for the main viewport area
    ImGuiDockNode* main_node = ImGui::DockBuilderGetNode(dock_id_main);
    if (main_node) main_node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;

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
    if (app->trace_parser_active) {
      const char* filename =
          app->trace_filename.size > 0 ? app->trace_filename.data : "";
      loading_screen_draw(filename, app->trace_event_count, app->trace_total_bytes,
                          app->theme);
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
  if (app->trace_parser_active) {
    trace_parser_deinit(&app->trace_parser);
  }
  trace_parser_init(&app->trace_parser, app->allocator);
  trace_data_clear(&app->trace_data, app->allocator);
  app->trace_event_count = 0;
  app->trace_total_bytes = 0;
  app->trace_start_time = platform_get_now();
  app->trace_parser_active = true;
  app->current_session_id = session_id;
  app->trace_viewer.selected_event_index = -1;

  array_list_clear(&app->trace_filename);
  if (filename) {
    array_list_append(&app->trace_filename, app->allocator, filename,
                      strlen(filename) + 1);
  }
}

void app_handle_file_chunk(App* app, int session_id, const char* data,
                           size_t size, bool is_eof) {
  if (session_id != app->current_session_id) {
    return;
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
    trace_data_add_event(&app->trace_data, app->allocator, app->theme, &event);
  }

  if (is_eof) {
    double duration_ms = platform_get_now() - app->trace_start_time;
    double duration_s = duration_ms / 1000.0;
    double speed_mb_s =
        duration_s > 0.0
            ? ((double)app->trace_total_bytes / (1024.0 * 1024.0)) / duration_s
            : 0.0;
    LOG_INFO("parsed %zu events (total) in %.3f ms (%.2f mb/s)",
             app->trace_event_count, duration_ms, speed_mb_s);
    trace_parser_deinit(&app->trace_parser);
    app_organize_tracks(app);
    app->trace_parser_active = false;
  }
}
