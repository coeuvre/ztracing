#include "src/app.h"

#include <stdio.h>
#include <algorithm>
#include <math.h>

#include "src/format.h"
#include "src/platform.h"

#include "src/logging.h"
#include "third_party/imgui/imgui.h"
#include "third_party/imgui/imgui_internal.h"

void app_init(App* app, Allocator allocator) {
  *app = {};
  app->allocator = allocator;
  app->power_save_mode = true;
  app->first_frame = true;
  app->show_demo_window = false;
  app->selected_event_index = -1;
  trace_data_init(&app->trace_data, app->allocator);
}

void app_deinit(App* app) {
  if (app->trace_parser_active) {
    trace_parser_deinit(&app->trace_parser);
  }
  trace_data_deinit(&app->trace_data, app->allocator);
  array_list_deinit(&app->trace_filename, app->allocator);
  for (size_t i = 0; i < app->tracks.size; i++) {
    track_deinit(&app->tracks[i], app->allocator);
  }
  array_list_deinit(&app->tracks, app->allocator);
}

static void app_organize_tracks(App* app) {
  for (size_t i = 0; i < app->tracks.size; i++) {
    track_deinit(&app->tracks[i], app->allocator);
  }
  array_list_clear(&app->tracks);
  if (app->trace_data.events.size == 0) return;

  app->viewport.min_ts = app->trace_data.events[0].ts;
  app->viewport.max_ts = app->trace_data.events[0].ts + app->trace_data.events[0].dur;

  // Simple track cache to speed up O(N*M) search
  size_t last_track_idx = (size_t)-1;

  for (size_t i = 0; i < app->trace_data.events.size; i++) {
    const TraceEventPersisted& e = app->trace_data.events[i];
    if (e.ts < app->viewport.min_ts) app->viewport.min_ts = e.ts;
    if (e.ts + e.dur > app->viewport.max_ts) app->viewport.max_ts = e.ts + e.dur;

    size_t track_idx = (size_t)-1;
    if (last_track_idx != (size_t)-1 && 
        app->tracks[last_track_idx].pid == e.pid && 
        app->tracks[last_track_idx].tid == e.tid) {
      track_idx = last_track_idx;
    } else {
      for (size_t j = 0; j < app->tracks.size; j++) {
        if (app->tracks[j].pid == e.pid && app->tracks[j].tid == e.tid) {
          track_idx = j;
          last_track_idx = j;
          break;
        }
      }
    }

    if (track_idx == (size_t)-1) {
      Track t = {};
      t.pid = e.pid;
      t.tid = e.tid;
      array_list_push_back(&app->tracks, app->allocator, t);
      track_idx = app->tracks.size - 1;
      last_track_idx = track_idx;
    }

    array_list_push_back(&app->tracks[track_idx].event_indices, app->allocator, i);
  }

  for (size_t i = 0; i < app->tracks.size; i++) {
    track_sort_events(&app->tracks[i], &app->trace_data);
    track_update_max_dur(&app->tracks[i], &app->trace_data);
  }

  app->viewport.start_time = (double)app->viewport.min_ts;
  app->viewport.end_time = (double)app->viewport.max_ts;
  LOG_INFO("organized %zu tracks, time range: [%lld, %lld]", app->tracks.size, (long long)app->viewport.min_ts, (long long)app->viewport.max_ts);
}

static void app_draw_time_ruler(App* app, ImDrawList* draw_list, ImVec2 pos,
                                ImVec2 size) {
  double start_time = app->viewport.start_time;
  double end_time = app->viewport.end_time;
  double duration = end_time - start_time;
  if (duration <= 0) return;

  // Ruler background
  draw_list->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                           IM_COL32(40, 40, 40, 255));
  draw_list->AddLine(ImVec2(pos.x, pos.y + size.y - 1),
                     ImVec2(pos.x + size.x, pos.y + size.y - 1),
                     IM_COL32(100, 100, 100, 255));

  // Determine tick interval
  double tick_interval = calculate_tick_interval(duration, size.x, 100.0);

  double display_start = start_time - (double)app->viewport.min_ts;
  double display_end = end_time - (double)app->viewport.min_ts;

  double first_tick_rel = ceil(display_start / tick_interval) * tick_interval;
  for (double t_rel = first_tick_rel; t_rel <= display_end; t_rel += tick_interval) {
    double t = t_rel + (double)app->viewport.min_ts;
    float x = (float)(pos.x + (t - start_time) / duration * size.x);
    if (x < pos.x || x > pos.x + size.x) continue;

    draw_list->AddLine(ImVec2(x, pos.y + size.y * 0.6f),
                       ImVec2(x, pos.y + size.y - 1),
                       IM_COL32(150, 150, 150, 255));

    char label[32];
    format_duration(label, sizeof(label), t_rel, tick_interval);

    draw_list->AddText(ImVec2(x + 3, pos.y + 2), IM_COL32(200, 200, 200, 255),
                       label);
  }
}

void app_update(App* app) {
  // 1. Setup DockSpace
  ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
  ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), dockspace_flags);

  if (app->first_frame) {
    app->first_frame = false;
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, dockspace_flags | ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

    ImGuiID dock_id_main = dockspace_id;
    ImGuiID dock_id_bottom = ImGui::DockBuilderSplitNode(dock_id_main, ImGuiDir_Down, 0.25f, nullptr, &dock_id_main);

    // Hide tab bar for the main viewport area
    ImGuiDockNode* main_node = ImGui::DockBuilderGetNode(dock_id_main);
    if (main_node) main_node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;

    ImGui::DockBuilderDockWindow("Trace Viewport", dock_id_main);
    ImGui::DockBuilderDockWindow("Status", dock_id_bottom);
    ImGui::DockBuilderDockWindow("Details", dock_id_bottom);
    ImGui::DockBuilderFinish(dockspace_id);
  }

  if (app->show_demo_window) ImGui::ShowDemoWindow(&app->show_demo_window);

  // 2. Fullscreen Trace Viewport
  {
    ImGuiWindowFlags viewport_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar;
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    if (ImGui::Begin("Trace Viewport", nullptr, viewport_flags)) {
      if (app->trace_data.events.size > 0) {
        ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
        ImVec2 canvas_size = ImGui::GetContentRegionAvail();

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), IM_COL32(30, 30, 30, 255));

        double duration = app->viewport.end_time - app->viewport.start_time;
        if (duration <= 0) duration = 1.0;

        float ruler_height = 25.0f;
        app_draw_time_ruler(app, draw_list, canvas_pos, ImVec2(canvas_size.x, ruler_height));

        float track_height = 25.0f;
        float track_spacing = 2.0f;

        ImGuiWindowFlags child_flags = ImGuiWindowFlags_NoMove;
        if (ImGui::GetIO().KeyCtrl) child_flags |= ImGuiWindowFlags_NoScrollWithMouse;

        ImGui::SetCursorScreenPos(ImVec2(canvas_pos.x, canvas_pos.y + ruler_height));
        if (ImGui::BeginChild("TrackList", ImVec2(0, canvas_size.y - ruler_height), ImGuiChildFlags_None, child_flags)) {
          ImDrawList* track_draw_list = ImGui::GetWindowDrawList();
          // Set content size so scrollbars work and ImGui is happy.
          // Use 0.0f width to avoid forcing horizontal scrollbar.
          ImGui::Dummy(ImVec2(0.0f, (float)app->tracks.size * (track_height + track_spacing)));
          ImGui::SetCursorPos(ImVec2(0, 0));

          ImVec2 tracks_canvas_pos = ImGui::GetCursorScreenPos();
          float inner_width = ImGui::GetContentRegionAvail().x;

          for (size_t i = 0; i < app->tracks.size; i++) {
            const Track& t = app->tracks[i];
            ImVec2 track_pos = ImVec2(tracks_canvas_pos.x, tracks_canvas_pos.y + (float)i * (track_height + track_spacing));
            
            // Frustum culling: skip tracks that are not visible
            if (track_pos.y + track_height < canvas_pos.y + ruler_height || track_pos.y > canvas_pos.y + canvas_size.y) {
              continue;
            }

            track_draw_list->AddRectFilled(track_pos, ImVec2(track_pos.x + inner_width, track_pos.y + track_height), IM_COL32(50, 50, 50, 255));

            size_t start_idx = track_find_visible_start_index(&t, &app->trace_data, (int64_t)app->viewport.start_time);

            float last_draw_x2 = -1e10f;
            for (size_t k = start_idx; k < t.event_indices.size; k++) {
              size_t event_idx = t.event_indices[k];
              const TraceEventPersisted& e = app->trace_data.events[event_idx];
              
              if (e.ts > (int64_t)app->viewport.end_time) break;

              double start_x_rel = ((double)e.ts - app->viewport.start_time) / duration;
              double end_x_rel = ((double)e.ts + (double)e.dur - app->viewport.start_time) / duration;
              
              float x1 = (float)(tracks_canvas_pos.x + start_x_rel * inner_width);
              float x2 = (float)(tracks_canvas_pos.x + end_x_rel * inner_width);
              
              // LOD: skip if the entire event is significantly to the left of what we already drew
              // and it's very small. This helps when zoomed out.
              if (x2 < last_draw_x2 + 0.1f) continue;

              if (x2 - x1 < 0.1f) x2 = x1 + 0.1f;
              last_draw_x2 = x2;
              
              ImU32 col = (app->selected_event_index == (int64_t)event_idx) ? IM_COL32(255, 255, 0, 255) : IM_COL32(100, 180, 100, 255);
              track_draw_list->AddRectFilled(ImVec2(x1, track_pos.y + 1), ImVec2(x2, track_pos.y + track_height - 1), col);

              if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered()) {
                ImVec2 mouse_pos = ImGui::GetMousePos();
                if (mouse_pos.x >= x1 && mouse_pos.x <= x2 && mouse_pos.y >= track_pos.y && mouse_pos.y <= track_pos.y + track_height) {
                  app->selected_event_index = (int64_t)event_idx;
                }
              }
            }

            char label[64];
            snprintf(label, sizeof(label), "TID:%d", t.tid);
            track_draw_list->AddText(ImVec2(track_pos.x + 5, track_pos.y + 2), IM_COL32(255, 255, 255, 255), label);
          }
        }
        ImGui::EndChild();

        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
          if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            double dx = (double)ImGui::GetIO().MouseDelta.x;
            double dt = (dx / (double)canvas_size.x) * (app->viewport.end_time - app->viewport.start_time);
            app->viewport.start_time -= dt;
            app->viewport.end_time -= dt;
          }

          float wheel_v = ImGui::GetIO().MouseWheel;
          float wheel_h = ImGui::GetIO().MouseWheelH;

          if (wheel_v != 0.0f && ImGui::GetIO().KeyCtrl) {
            double mouse_x_rel = (double)(ImGui::GetIO().MousePos.x - canvas_pos.x) / (double)canvas_size.x;
            double current_duration = app->viewport.end_time - app->viewport.start_time;
            double mouse_ts = app->viewport.start_time + mouse_x_rel * current_duration;
            double zoom_factor = (wheel_v > 0.0f) ? 0.8 : 1.2;
            double new_duration = current_duration * zoom_factor;
            app->viewport.start_time = mouse_ts - mouse_x_rel * new_duration;
            app->viewport.end_time = app->viewport.start_time + new_duration;
          } else if (wheel_h != 0.0f || (wheel_v != 0.0f && ImGui::GetIO().KeyShift)) {
            float delta = (wheel_h != 0.0f) ? wheel_h : wheel_v;
            double dx = (double)delta * 100.0; // 100 pixels per tick sensitivity
            double dt = (dx / (double)canvas_size.x) * (app->viewport.end_time - app->viewport.start_time);
            app->viewport.start_time -= dt;
            app->viewport.end_time -= dt;
          }
        }
      } else {
        ImVec2 size = ImGui::GetContentRegionAvail();
        const char* msg = "Drop a Chrome Trace file here to begin";
        ImVec2 text_size = ImGui::CalcTextSize(msg);
        ImGui::SetCursorPos(ImVec2((size.x - text_size.x) * 0.5f, (size.y - text_size.y) * 0.5f));
        ImGui::Text("%s", msg);
      }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
  }

  // 3. Status/Control Overlay
  {
    if (ImGui::Begin("Status")) {
      ImGui::Checkbox("Power-save Mode", &app->power_save_mode);
      if (ImGui::Button("Reset Viewport")) {
          app->viewport.start_time = (double)app->viewport.min_ts;
          app->viewport.end_time = (double)app->viewport.max_ts;
      }
      ImGui::SameLine();
      if (ImGui::Button("Demo")) app->show_demo_window = !app->show_demo_window;

      if (app->trace_parser_active || app->trace_data.events.size > 0) {
        ImGui::Separator();
        ImGui::Text("Trace: %s", app->trace_filename.size > 0 ? app->trace_filename.data : "unknown");
        ImGui::Text("Events: %zu", app->trace_data.events.size);
        if (app->trace_parser_active) {
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "LOADING...");
        }
      }
    }
    ImGui::End();
  }

  // 4. Details Overlay
  if (app->selected_event_index != -1) {
    if (ImGui::Begin("Details")) {
      const TraceEventPersisted& e = app->trace_data.events[(size_t)app->selected_event_index];
      Str name = trace_data_get_string(&app->trace_data, e.name_offset);
      Str cat = trace_data_get_string(&app->trace_data, e.cat_offset);
      Str ph = trace_data_get_string(&app->trace_data, e.ph_offset);

      ImGui::Text("Name: %.*s", (int)name.len, name.buf);
      ImGui::Text("Category: %.*s", (int)cat.len, cat.buf);
      ImGui::Text("PH: %.*s", (int)ph.len, ph.buf);
      ImGui::Text("Timestamp: %lld", (long long)e.ts);
      ImGui::Text("Duration: %lld", (long long)e.dur);
      ImGui::Text("PID: %d, TID: %d", e.pid, e.tid);

      if (e.args_count > 0) {
        ImGui::Separator();
        ImGui::Text("Arguments:");
        for (uint32_t k = 0; k < e.args_count; k++) {
          const TraceArgPersisted& arg = app->trace_data.args[e.args_offset + k];
          Str key = trace_data_get_string(&app->trace_data, arg.key_offset);
          Str val = trace_data_get_string(&app->trace_data, arg.val_offset);
          ImGui::BulletText("%.*s: %.*s", (int)key.len, key.buf, (int)val.len, val.buf);
        }
      }
    }
    ImGui::End();
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
  app->selected_event_index = -1;

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
    trace_data_add_event(&app->trace_data, app->allocator, &event);
  }

  if (is_eof) {
    double duration_ms = platform_get_now() - app->trace_start_time;
    double duration_s = duration_ms / 1000.0;
    double speed_mb_s =
        duration_s > 0.0
            ? ((double)app->trace_total_bytes / (1024.0 * 1024.0)) / duration_s
            : 0.0;
    LOG_INFO("parsed %zu events (total) in %.3f ms (%.2f mb/s)", app->trace_event_count,
             duration_ms, speed_mb_s);
    trace_parser_deinit(&app->trace_parser);
    app_organize_tracks(app);
    app->trace_parser_active = false;
  }
}
