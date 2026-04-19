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
#include "third_party/imgui/imgui.h"
#include "third_party/imgui/imgui_internal.h"

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
}

void app_init(App* app, Allocator allocator) {
  *app = {};
  app->allocator = allocator;
  app->theme_mode = THEME_MODE_AUTO;
  app_apply_theme(
      app, platform_is_dark_mode() ? theme_get_dark() : theme_get_light());
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
  track_renderer_state_deinit(&app->track_renderer_state, app->allocator);
  array_list_deinit(&app->render_blocks, app->allocator);
  array_list_deinit(&app->hover_matches, app->allocator);
}

static void app_organize_tracks(App* app) {
  track_organize(&app->trace_data, app->allocator, &app->tracks,
                 &app->viewport.min_ts, &app->viewport.max_ts);

  app->viewport.start_time = (double)app->viewport.min_ts;
  app->viewport.end_time = (double)app->viewport.max_ts;
  LOG_INFO("organized %zu tracks, time range: [%lld, %lld]", app->tracks.size,
           (long long)app->viewport.min_ts, (long long)app->viewport.max_ts);
}

static void app_draw_time_ruler(App* app, ImDrawList* draw_list, ImVec2 pos,
                                ImVec2 size) {
  double start_time = app->viewport.start_time;
  double end_time = app->viewport.end_time;
  double duration = end_time - start_time;
  if (duration <= 0) return;

  const Theme& theme = *app->theme;

  // Ruler background
  draw_list->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                           theme.ruler_bg);
  draw_list->AddLine(ImVec2(pos.x, pos.y + size.y - 1),
                     ImVec2(pos.x + size.x, pos.y + size.y - 1),
                     theme.ruler_border);

  // Determine tick interval
  double tick_interval = calculate_tick_interval(duration, size.x, 100.0);

  double display_start = start_time - (double)app->viewport.min_ts;
  double display_end = end_time - (double)app->viewport.min_ts;

  double first_tick_rel = ceil(display_start / tick_interval) * tick_interval;
  for (double t_rel = first_tick_rel; t_rel <= display_end;
       t_rel += tick_interval) {
    double t = t_rel + (double)app->viewport.min_ts;
    float x = (float)(pos.x + (t - start_time) / duration * size.x);
    if (x < pos.x || x > pos.x + size.x) continue;

    draw_list->AddLine(ImVec2(x, pos.y + size.y * 0.6f),
                       ImVec2(x, pos.y + size.y - 1), theme.ruler_tick);

    char label[32];
    format_duration(label, sizeof(label), t_rel, tick_interval);

    draw_list->AddText(ImVec2(x + 3, pos.y + 2), theme.ruler_text, label);
  }
}

static void app_draw_event(App* app, ImDrawList* draw_list, float x1, float x2,
                           float y1, float y2, ImU32 col, bool is_selected,
                           StringRef name_ref, float inner_width,
                           float tracks_canvas_pos_x) {
  const Theme& theme = *app->theme;
  float lane_height = y2 - y1 + 1.0f;

  float border_thickness = is_selected ? TRACK_MIN_EVENT_WIDTH : 1.0f;
  float min_width = 2.0f * border_thickness + 1.0f;
  if (x2 - x1 < min_width) x2 = x1 + min_width;

  draw_list->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2), col);

  ImU32 border_col =
      is_selected ? theme.event_border_selected : theme.event_border;
  draw_list->AddRect(ImVec2(x1, y1), ImVec2(x2, y2), border_col, 0.0f, 0,
                     border_thickness);

  // Draw event name if there is enough space and it's not a merged block
  // (name_ref != 0)
  if (name_ref != 0) {
    float visible_x1 = std::max(x1, tracks_canvas_pos_x);
    float visible_x2 = std::min(x2, tracks_canvas_pos_x + inner_width);
    float padding_h = 6.0f;

    if (visible_x2 - visible_x1 > padding_h * 2.0f + 20.0f) {
      Str name = trace_data_get_string(&app->trace_data, name_ref);
      if (name.len > 0) {
        ImU32 text_col =
            is_selected ? theme.event_text_selected : theme.event_text;
        float event_font_size = ImGui::GetFontSize();
        float text_y = y1 + (lane_height - event_font_size) * 0.5f;

        float text_width = ImGui::GetFont()
                               ->CalcTextSizeA(event_font_size, FLT_MAX, 0.0f,
                                               name.buf, name.buf + name.len)
                               .x;
        float available_width =
            (visible_x2 - padding_h) - (visible_x1 + padding_h);

        ImVec4 fine_clip_rect(visible_x1, y1, visible_x2 - padding_h, y2);
        const ImVec4* clip_ptr =
            (text_width > available_width) ? &fine_clip_rect : nullptr;

        draw_list->AddText(ImGui::GetFont(), event_font_size,
                           ImVec2(visible_x1 + padding_h, text_y), text_col,
                           name.buf, name.buf + name.len, 0.0f, clip_ptr);
      }
    }
  }
}

void app_update(App* app) {
  const Theme& theme = *app->theme;
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
    ImGuiWindowFlags viewport_flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    if (ImGui::Begin("Trace Viewport", nullptr, viewport_flags)) {
      if (app->trace_data.events.size > 0) {
        ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
        ImVec2 canvas_size = ImGui::GetContentRegionAvail();

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(
            canvas_pos,
            ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
            theme.viewport_bg);
        double duration = app->viewport.end_time - app->viewport.start_time;
        if (duration <= 0) duration = 1.0;

        float ruler_height = 28.0f;
        app_draw_time_ruler(app, draw_list, canvas_pos,
                            ImVec2(canvas_size.x, ruler_height));

        float lane_height = 28.0f;

        ImGuiWindowFlags child_flags = ImGuiWindowFlags_NoMove;
        if (ImGui::IsKeyDown(ImGuiMod_Ctrl))
          child_flags |= ImGuiWindowFlags_NoScrollWithMouse;

        ImGui::SetCursorScreenPos(
            ImVec2(canvas_pos.x, canvas_pos.y + ruler_height));
        if (ImGui::BeginChild("TrackList",
                              ImVec2(0, canvas_size.y - ruler_height),
                              ImGuiChildFlags_None, child_flags)) {
          if (ImGui::IsWindowHovered() &&
              ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImGui::SetScrollY(ImGui::GetScrollY() -
                              ImGui::GetIO().MouseDelta.y);
          }

          ImDrawList* track_draw_list = ImGui::GetWindowDrawList();

          float total_height = 0.0f;
          for (size_t i = 0; i < app->tracks.size; i++) {
            total_height += (float)(app->tracks[i].max_depth + 2) * lane_height;
          }
          ImGui::Dummy(ImVec2(0.0f, total_height));
          ImGui::SetCursorPos(ImVec2(0, 0));

          ImVec2 tracks_canvas_pos = ImGui::GetCursorScreenPos();
          float inner_width = ImGui::GetContentRegionAvail().x;
          double inv_duration = inner_width / duration;
          ImVec2 mouse_pos = ImGui::GetMousePos();
          bool track_list_hovered =
              ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

          ImVec2 drag_delta = ImGui::GetMouseDragDelta(0);
          float drag_threshold = ImGui::GetIO().MouseDragThreshold;
          bool was_drag = (std::abs(drag_delta.x) >= drag_threshold ||
                           std::abs(drag_delta.y) >= drag_threshold);

          float cumulative_y = 0.0f;
          bool sel_found = false;
          float sel_x1 = 0, sel_x2 = 0, sel_y1 = 0, sel_y2 = 0;
          ImU32 sel_col = 0;
          StringRef sel_name_ref = 0;

          array_list_clear(&app->hover_matches);

          for (size_t i = 0; i < app->tracks.size; i++) {
            const Track& t = app->tracks[i];
            float track_height = (float)(t.max_depth + 2) * lane_height;
            ImVec2 track_pos =
                ImVec2(tracks_canvas_pos.x, tracks_canvas_pos.y + cumulative_y);

            // Frustum culling: skip tracks that are not visible
            if (track_pos.y + track_height < canvas_pos.y + ruler_height ||
                track_pos.y > canvas_pos.y + canvas_size.y) {
              cumulative_y += track_height;
              continue;
            }

            track_draw_list->AddRectFilled(
                track_pos,
                ImVec2(track_pos.x + inner_width, track_pos.y + track_height),
                theme.track_bg);

            // Render track header
            track_draw_list->AddRectFilled(
                track_pos,
                ImVec2(track_pos.x + inner_width, track_pos.y + lane_height),
                theme.track_header_bg);
            track_draw_list->AddLine(
                ImVec2(track_pos.x, track_pos.y + lane_height - 1),
                ImVec2(track_pos.x + inner_width,
                       track_pos.y + lane_height - 1),
                theme.track_separator);

            // Sticky header text
            float sticky_x = std::max(track_pos.x, tracks_canvas_pos.x);

            char default_name[32];
            Str thread_name =
                trace_data_get_string(&app->trace_data, t.name_ref);
            const char* display_name = thread_name.buf;
            size_t display_name_len = thread_name.len;

            if (display_name_len == 0) {
              snprintf(default_name, sizeof(default_name), "Thread %d", t.tid);
              display_name = default_name;
              display_name_len = strlen(default_name);
            }

            float font_size = ImGui::GetFontSize();
            ImVec2 text_pos = ImVec2(
                sticky_x + 5, track_pos.y + (lane_height - font_size) * 0.5f);
            track_draw_list->AddText(ImGui::GetFont(), font_size, text_pos,
                                     theme.track_text, display_name,
                                     display_name + display_name_len);

            ImVec2 text_size = ImGui::GetFont()->CalcTextSizeA(
                font_size, FLT_MAX, 0.0f, display_name,
                display_name + display_name_len);
            if (ImGui::IsMouseHoveringRect(text_pos,
                                           ImVec2(text_pos.x + text_size.x,
                                                  text_pos.y + text_size.y))) {
              ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
              ImGui::BeginTooltip();
              ImGui::Text("PID: %d", t.pid);
              ImGui::Text("TID: %d", t.tid);
              if (thread_name.len > 0) {
                ImGui::Separator();
                ImGui::Text("Name: %.*s", (int)thread_name.len,
                            thread_name.buf);
              }
              ImGui::EndTooltip();
              ImGui::PopStyleVar();
            }

            track_compute_render_blocks(
                &t, &app->trace_data, app->viewport.start_time,
                app->viewport.end_time, inner_width, tracks_canvas_pos.x,
                app->selected_event_index, &app->track_renderer_state,
                &app->render_blocks, app->allocator);

            bool mouse_in_track_y = (mouse_pos.y >= track_pos.y + lane_height &&
                                     mouse_pos.y < track_pos.y + track_height);

            for (size_t k = 0; k < app->render_blocks.size; k++) {
              const TrackRenderBlock& rb = app->render_blocks[k];

              float y1 = track_pos.y + (float)(rb.depth + 1) * lane_height;
              float y2 = y1 + lane_height - 1.0f;

              float x1 = rb.x1;
              float x2 = rb.x2;
              float border_thickness = rb.is_selected ? TRACK_MIN_EVENT_WIDTH : 1.0f;
              float min_width = 2.0f * border_thickness + 1.0f;
              if (x2 - x1 < min_width) x2 = x1 + min_width;

              if (track_list_hovered && mouse_in_track_y) {
                if (mouse_pos.x >= x1 && mouse_pos.x < x2 &&
                    mouse_pos.y >= y1 && mouse_pos.y < y2) {
                  HoverMatch hm = {i, k, y1, y2, rb};
                  array_list_push_back(&app->hover_matches, app->allocator, hm);
                }
              }

              if (rb.is_selected) {
                sel_found = true;
                sel_x1 = rb.x1;
                sel_x2 = rb.x2;
                sel_y1 = y1;
                sel_y2 = y2;
                sel_col = rb.color;
                sel_name_ref = rb.name_ref;
              } else {
                app_draw_event(app, track_draw_list, rb.x1, rb.x2, y1, y2,
                               rb.color, false, rb.name_ref, inner_width,
                               tracks_canvas_pos.x);
              }
            }

            cumulative_y += track_height;
          }

          // Handle hover highlighting and tooltip
          bool something_hovered = false;
          if (app->hover_matches.size > 0) {
            something_hovered = true;
            // Best match is the one drawn last
            const HoverMatch* best_hm =
                &app->hover_matches[app->hover_matches.size - 1];

            const TrackRenderBlock& rb = best_hm->rb;
            const Track& t = app->tracks[best_hm->track_idx];

            // Re-draw hovered block with highlight
            ImU32 col = rb.color;
            if (!rb.is_selected) {
              ImVec4 col_v = ImGui::ColorConvertU32ToFloat4(col);
              col_v.x = std::min(col_v.x + 0.15f, 1.0f);
              col_v.y = std::min(col_v.y + 0.15f, 1.0f);
              col_v.z = std::min(col_v.z + 0.15f, 1.0f);
              col = ImGui::ColorConvertFloat4ToU32(col_v);

              app_draw_event(app, track_draw_list, rb.x1, rb.x2, best_hm->y1,
                             best_hm->y2, col, false, rb.name_ref, inner_width,
                             tracks_canvas_pos.x);
            }

            // Show tooltip
            if (rb.count == 1) {
              size_t start_idx = track_find_visible_start_index(
                  &t, &app->trace_data, (int64_t)app->viewport.start_time);
              for (size_t idx_k = start_idx; idx_k < t.event_indices.size;
                   idx_k++) {
                size_t event_idx = t.event_indices[idx_k];
                const TraceEventPersisted& e =
                    app->trace_data.events[event_idx];
                if (e.ts > (int64_t)app->viewport.end_time) break;
                if (t.depths[idx_k] != rb.depth) continue;

                float ex1 =
                    (float)(tracks_canvas_pos.x +
                            ((double)e.ts - app->viewport.start_time) *
                                inv_duration);
                float ex2 = (float)(ex1 + (double)e.dur * inv_duration);
                if (ex2 - ex1 < TRACK_MIN_EVENT_WIDTH)
                  ex2 = ex1 + TRACK_MIN_EVENT_WIDTH;

                if (mouse_pos.x >= ex1 && mouse_pos.x < ex2) {
                  Str name = trace_data_get_string(&app->trace_data, e.name_ref);
                  Str cat = trace_data_get_string(&app->trace_data, e.cat_ref);

                  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
                  ImGui::BeginTooltip();
                  ImGui::TextUnformatted(name.buf, name.buf + name.len);
                  if (cat.len > 0) {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                       "Category: %.*s", (int)cat.len,
                                       cat.buf);
                  }
                  ImGui::Separator();

                  char ts_buf[32];
                  format_duration(ts_buf, sizeof(ts_buf),
                                  (double)e.ts - (double)app->viewport.min_ts);
                  ImGui::Text("Start: %s", ts_buf);

                  char dur_buf[32];
                  format_duration(dur_buf, sizeof(dur_buf), (double)e.dur);
                  ImGui::Text("Duration: %s", dur_buf);

                  if (e.args_count > 0) {
                    ImGui::Separator();
                    for (uint32_t arg_idx = 0; arg_idx < e.args_count;
                         arg_idx++) {
                      const TraceArgPersisted& arg =
                          app->trace_data.args[e.args_offset + arg_idx];
                      Str key =
                          trace_data_get_string(&app->trace_data, arg.key_ref);
                      Str val =
                          trace_data_get_string(&app->trace_data, arg.val_ref);
                      ImGui::Text("%.*s: %.*s", (int)key.len, key.buf,
                                  (int)val.len, val.buf);
                    }
                  }
                  ImGui::EndTooltip();
                  ImGui::PopStyleVar();
                  break;
                }
              }
            } else {
              ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
              ImGui::BeginTooltip();
              ImGui::Text("%u merged events", rb.count);
              ImGui::EndTooltip();
              ImGui::PopStyleVar();
            }

            // Handle selection
            if (ImGui::IsMouseReleased(0) && !was_drag) {
              size_t start_idx = track_find_visible_start_index(
                  &t, &app->trace_data, (int64_t)app->viewport.start_time);
              for (size_t idx_k = start_idx; idx_k < t.event_indices.size;
                   idx_k++) {
                size_t event_idx = t.event_indices[idx_k];
                const TraceEventPersisted& e =
                    app->trace_data.events[event_idx];
                if (e.ts > (int64_t)app->viewport.end_time) break;
                if (t.depths[idx_k] != rb.depth) continue;

                float ex1 =
                    (float)(tracks_canvas_pos.x +
                            ((double)e.ts - app->viewport.start_time) *
                                inv_duration);
                float ex2 = (float)(ex1 + (double)e.dur * inv_duration);
                if (ex2 - ex1 < TRACK_MIN_EVENT_WIDTH)
                  ex2 = ex1 + TRACK_MIN_EVENT_WIDTH;

                if (mouse_pos.x >= ex1 && mouse_pos.x < ex2) {
                  app->selected_event_index = (int64_t)event_idx;
                  break;
                }
              }
            }
          }

          if (sel_found) {
            app_draw_event(app, track_draw_list, sel_x1, sel_x2, sel_y1, sel_y2,
                           sel_col, true, sel_name_ref, inner_width,
                           tracks_canvas_pos.x);
          }

          if (track_list_hovered && !something_hovered &&
              ImGui::IsMouseReleased(0) && !was_drag) {
            app->selected_event_index = -1;
          }
        }
        ImGui::EndChild();

        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
          if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            double dx = (double)ImGui::GetIO().MouseDelta.x;
            double dt = (dx / (double)canvas_size.x) *
                        (app->viewport.end_time - app->viewport.start_time);
            app->viewport.start_time -= dt;
            app->viewport.end_time -= dt;
          }

          float wheel_v = ImGui::GetIO().MouseWheel;
          float wheel_h = ImGui::GetIO().MouseWheelH;

          if (wheel_v != 0.0f && ImGui::IsKeyDown(ImGuiMod_Ctrl)) {
            double mouse_x_rel =
                (double)(ImGui::GetIO().MousePos.x - canvas_pos.x) /
                (double)canvas_size.x;
            double current_duration =
                app->viewport.end_time - app->viewport.start_time;
            double mouse_ts =
                app->viewport.start_time + mouse_x_rel * current_duration;
            double zoom_factor = (wheel_v > 0.0f) ? 0.8 : 1.2;
            double new_duration = current_duration * zoom_factor;
            app->viewport.start_time = mouse_ts - mouse_x_rel * new_duration;
            app->viewport.end_time = app->viewport.start_time + new_duration;
          } else if (wheel_h != 0.0f ||
                     (wheel_v != 0.0f && ImGui::IsKeyDown(ImGuiMod_Shift))) {
            float delta = (wheel_h != 0.0f) ? wheel_h : wheel_v;
            double dx =
                (double)delta * 100.0;  // 100 pixels per tick sensitivity
            double dt = (dx / (double)canvas_size.x) *
                        (app->viewport.end_time - app->viewport.start_time);
            app->viewport.start_time -= dt;
            app->viewport.end_time -= dt;
          }
        }
      } else {
        ImVec2 size = ImGui::GetContentRegionAvail();
        const char* msg = "Drop a Chrome Trace file here to begin";
        ImVec2 text_size = ImGui::CalcTextSize(msg);
        ImGui::SetCursorPos(ImVec2((size.x - text_size.x) * 0.5f,
                                   (size.y - text_size.y) * 0.5f));
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

      int current_theme_idx = (int)app->theme_mode;
      const char* theme_names[] = {"Auto", "Dark", "Light"};
      if (ImGui::Combo("Theme", &current_theme_idx, theme_names,
                       IM_ARRAYSIZE(theme_names))) {
        app->theme_mode = (ThemeMode)current_theme_idx;
        if (app->theme_mode == THEME_MODE_DARK) {
          app_apply_theme(app, theme_get_dark());
        } else if (app->theme_mode == THEME_MODE_LIGHT) {
          app_apply_theme(app, theme_get_light());
        } else {
          // THEME_MODE_AUTO: handled in app_update or immediately here
          app_apply_theme(app, platform_is_dark_mode() ? theme_get_dark()
                                                       : theme_get_light());
        }
      }

      if (ImGui::Button("Reset Viewport")) {
        app->viewport.start_time = (double)app->viewport.min_ts;
        app->viewport.end_time = (double)app->viewport.max_ts;
      }
      ImGui::SameLine();
      if (ImGui::Button("Demo")) app->show_demo_window = !app->show_demo_window;

      if (app->trace_parser_active || app->trace_data.events.size > 0) {
        ImGui::Separator();
        ImGui::Text("Trace: %s", app->trace_filename.size > 0
                                     ? app->trace_filename.data
                                     : "unknown");
        ImGui::Text("Events: %zu", app->trace_data.events.size);
        if (app->trace_parser_active) {
          ImGui::TextColored(theme.status_loading, "LOADING...");
        }
      }
    }
    ImGui::End();
  }

  // 4. Details Overlay
  if (app->selected_event_index != -1) {
    if (ImGui::Begin("Details")) {
      const TraceEventPersisted& e =
          app->trace_data.events[(size_t)app->selected_event_index];
      Str name = trace_data_get_string(&app->trace_data, e.name_ref);
      Str cat = trace_data_get_string(&app->trace_data, e.cat_ref);
      Str ph = trace_data_get_string(&app->trace_data, e.ph_ref);

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
          const TraceArgPersisted& arg =
              app->trace_data.args[e.args_offset + k];
          Str key = trace_data_get_string(&app->trace_data, arg.key_ref);
          Str val = trace_data_get_string(&app->trace_data, arg.val_ref);
          ImGui::BulletText("%.*s: %.*s", (int)key.len, key.buf, (int)val.len,
                            val.buf);
        }
      }
    }
    ImGui::End();
  }
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
