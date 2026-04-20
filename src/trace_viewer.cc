#include "src/trace_viewer.h"

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

const double TRACE_VIEWER_MAX_ZOOM_FACTOR = 1.2;
const double TRACE_VIEWER_MIN_ZOOM_DURATION = 1000.0;  // 1ms = 1000us

void trace_viewer_init(TraceViewer* tv, Allocator allocator) {
  (void)allocator;
  *tv = {};
  tv->selected_event_index = -1;
  tv->show_details_panel = true;
}

void trace_viewer_deinit(TraceViewer* tv, Allocator allocator) {
  for (size_t i = 0; i < tv->tracks.size; i++) {
    track_deinit(&tv->tracks[i], allocator);
  }
  array_list_deinit(&tv->tracks, allocator);
  track_renderer_state_deinit(&tv->track_renderer_state, allocator);
  array_list_deinit(&tv->render_blocks, allocator);
  array_list_deinit(&tv->counter_render_blocks, allocator);
  array_list_deinit(&tv->hover_matches, allocator);
}

static void trace_viewer_draw_time_ruler(TraceViewer* tv, ImDrawList* draw_list,
                                         ImVec2 pos, ImVec2 size,
                                         const Theme& theme) {
  double start_time = tv->viewport.start_time;
  double end_time = tv->viewport.end_time;
  double duration = end_time - start_time;
  if (duration <= 0) return;

  // Ruler background
  draw_list->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                           theme.ruler_bg);
  draw_list->AddLine(ImVec2(pos.x, pos.y + size.y - 1),
                     ImVec2(pos.x + size.x, pos.y + size.y - 1),
                     theme.ruler_border);

  // Determine tick interval
  double tick_interval = calculate_tick_interval(duration, size.x, 100.0);

  double display_start = start_time - (double)tv->viewport.min_ts;
  double display_end = end_time - (double)tv->viewport.min_ts;

  double first_tick_rel = ceil(display_start / tick_interval) * tick_interval;
  for (double t_rel = first_tick_rel; t_rel <= display_end;
       t_rel += tick_interval) {
    double t = t_rel + (double)tv->viewport.min_ts;
    float x = (float)(pos.x + (t - start_time) / duration * size.x);
    if (x < pos.x || x > pos.x + size.x) continue;

    draw_list->AddLine(ImVec2(x, pos.y + size.y * 0.6f),
                       ImVec2(x, pos.y + size.y - 1), theme.ruler_tick);

    char label[32];
    format_duration(label, sizeof(label), t_rel, tick_interval);

    draw_list->AddText(ImVec2(x + 3, pos.y + 2), theme.ruler_text, label);
  }
}

static void trace_viewer_draw_event(TraceViewer* tv, TraceData* td,
                                    ImDrawList* draw_list, float x1, float x2,
                                    float y1, float y2, ImU32 col,
                                    bool is_selected, StringRef name_ref,
                                    float inner_width,
                                    float tracks_canvas_pos_x,
                                    const Theme& theme) {
  (void)tv;
  float lane_height = y2 - y1 + 1.0f;

  float border_thickness = is_selected ? TRACK_MIN_EVENT_WIDTH : 1.0f;
  float min_width = 2.0f * border_thickness + 1.0f;
  bool draw_border = true;
  if (x2 - x1 < min_width) {
    x2 = x1 + min_width;
    draw_border = false;
  }

  draw_list->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2), col);

  float event_width = x2 - x1;
  if (is_selected || draw_border) {
    ImU32 border_col =
        is_selected ? theme.event_border_selected : theme.event_border;
    draw_list->AddRect(ImVec2(x1, y1), ImVec2(x2, y2), border_col, 0.0f, 0,
                       border_thickness);
  }

  // Draw event name if there is enough space and it's not a merged block
  // (name_ref != 0)
  if (name_ref != 0) {
    float padding_h = 6.0f;

    if (event_width > padding_h * 2.0f + 10.0f) {
      float visible_x1 = std::max(x1, tracks_canvas_pos_x);
      float visible_x2 = std::min(x2, tracks_canvas_pos_x + inner_width);

      if (visible_x2 > visible_x1) {
        Str name = trace_data_get_string(td, name_ref);
        if (name.len > 0) {
          ImU32 text_col =
              is_selected ? theme.event_text_selected : theme.event_text;
          float event_font_size = ImGui::GetFontSize();
          float text_y = y1 + (lane_height - event_font_size) * 0.5f;

          float text_width = ImGui::GetFont()
                                 ->CalcTextSizeA(event_font_size, FLT_MAX, 0.0f,
                                                 name.buf, name.buf + name.len)
                                 .x;

          float text_x = x1 + (event_width - text_width) * 0.5f;

          ImVec4 fine_clip_rect(visible_x1 + padding_h, y1,
                                visible_x2 - padding_h, y2);
          const ImVec4* clip_ptr = &fine_clip_rect;

          draw_list->AddText(ImGui::GetFont(), event_font_size,
                             ImVec2(text_x, text_y), text_col, name.buf,
                             name.buf + name.len, 0.0f, clip_ptr);
        }
      }
    }
  }
}

static void trace_viewer_draw_counter_track(
    TraceViewer* tv, TraceData* td, ImDrawList* draw_list, const Track& t,
    ImVec2 pos, float width, float height, double viewport_start,
    double viewport_end, double viewport_min_ts, const Theme& theme,
    ImVec2 mouse_pos, bool track_list_hovered, bool was_drag,
    bool* something_hovered, Allocator allocator) {
  if (t.event_indices.size == 0) return;

  double duration = viewport_end - viewport_start;
  if (duration <= 0) return;

  double max_total = t.counter_max_total;
  if (max_total <= 0) max_total = 1.0;

  TrackRendererState* state = &tv->track_renderer_state;
  array_list_resize(&state->counter_values, allocator, t.counter_series.size);

  track_compute_counter_render_blocks(&t, td, viewport_start, viewport_end,
                                      width, pos.x, &state->counter_peaks,
                                      &tv->counter_render_blocks, allocator);

  if (tv->counter_render_blocks.size == 0) return;

  size_t n_blocks = tv->counter_render_blocks.size;
  size_t n_series = t.counter_series.size;
  array_list_resize(&state->counter_visual_offsets, allocator,
                    n_blocks * (n_series + 1));

  float min_h = 1.0f;
  for (size_t i = 0; i < n_blocks; i++) {
    float current_y_offset_px = 0.0f;
    state->counter_visual_offsets[i * (n_series + 1)] = 0.0f;
    for (size_t s_idx = 0; s_idx < n_series; s_idx++) {
      double val = state->counter_peaks[i * n_series + s_idx];
      double visual_val = std::max(val, (double)min_h / height * max_total);
      current_y_offset_px += (float)(visual_val / max_total * height);
      state->counter_visual_offsets[i * (n_series + 1) + s_idx + 1] =
          current_y_offset_px;
    }
  }

  // Pass 1: Filled areas and hover highlight
  for (size_t i = 0; i < n_blocks; i++) {
    const CounterRenderBlock& rb = tv->counter_render_blocks[i];

    bool hovered = track_list_hovered && mouse_pos.x >= rb.x1 &&
                   mouse_pos.x < rb.x2 && mouse_pos.y >= pos.y &&
                   mouse_pos.y < pos.y + height;

    // Draw stack
    for (size_t s_idx = 0; s_idx < n_series; s_idx++) {
      float off_bottom = state->counter_visual_offsets[i * (n_series + 1) + s_idx];
      float off_top = state->counter_visual_offsets[i * (n_series + 1) + s_idx + 1];

      float y_top = pos.y + height - off_top;
      float y_bottom = pos.y + height - off_bottom;

      draw_list->AddRectFilled(ImVec2(rb.x1, y_top), ImVec2(rb.x2, y_bottom),
                               t.counter_colors[s_idx]);
    }

    if (hovered) {
      *something_hovered = true;
      draw_list->AddRectFilled(ImVec2(rb.x1, pos.y),
                               ImVec2(rb.x2, pos.y + height),
                               IM_COL32(255, 255, 255, 30));

      if (ImGui::IsMouseReleased(0) && !was_drag && rb.event_idx != (size_t)-1) {
        tv->selected_event_index = (int64_t)rb.event_idx;
      }

      if (rb.event_idx != (size_t)-1) {
        const TraceEventPersisted& e = td->events[rb.event_idx];
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
        ImGui::BeginTooltip();
        char ts_buf[32];
        format_duration(ts_buf, sizeof(ts_buf),
                        (double)e.ts - (double)viewport_min_ts);
        ImGui::Text("Time: %s", ts_buf);
        ImGui::Separator();

        double total = 0.0;
        for (size_t s_idx = 0; s_idx < t.counter_series.size; s_idx++) {
          StringRef key_ref = t.counter_series[s_idx];
          double val = 0.0;
          for (uint32_t arg_k = 0; arg_k < e.args_count; arg_k++) {
            const TraceArgPersisted& arg = td->args[e.args_offset + arg_k];
            if (arg.key_ref == key_ref) {
              val = arg.val_double;
              break;
            }
          }
          Str key = trace_data_get_string(td, key_ref);
          ImGui::Text("%.*s: %g", (int)key.len, key.buf, val);
          total += val;
        }
        if (t.counter_series.size > 1) {
          ImGui::Separator();
          ImGui::Text("Total: %g", total);
        }
        ImGui::EndTooltip();
        ImGui::PopStyleVar();
      }
    }
  }

  // Pass 2: Step lines (no anti-aliasing for sharp lines)
  ImDrawListFlags old_flags = draw_list->Flags;
  draw_list->Flags &= ~ImDrawListFlags_AntiAliasedLines;

  for (size_t s_idx = 0; s_idx < n_series; s_idx++) {
    float prev_y_top = -1.0f;
    for (size_t i = 0; i < n_blocks; i++) {
      const CounterRenderBlock& rb = tv->counter_render_blocks[i];
      bool event_selected = (rb.event_idx != (size_t)-1 &&
                             (int64_t)rb.event_idx == tv->selected_event_index);

      float off_top = state->counter_visual_offsets[i * (n_series + 1) + s_idx + 1];
      float y_top = pos.y + height - off_top;

      ImU32 line_col = theme.event_border;
      float thickness = 1.0f;
      if (event_selected) {
        line_col = theme.event_border_selected;
        thickness = 2.0f;
      }

      // Horizontal segment
      draw_list->AddLine(ImVec2(rb.x1, y_top), ImVec2(rb.x2, y_top), line_col,
                         thickness);

      // Vertical segment (connect to previous bucket)
      if (prev_y_top != -1.0f && y_top != prev_y_top) {
        draw_list->AddLine(ImVec2(rb.x1, prev_y_top), ImVec2(rb.x1, y_top),
                           theme.event_border);
      }
      prev_y_top = y_top;
    }
  }

  draw_list->Flags = old_flags;
}

void trace_viewer_draw(TraceViewer* tv, TraceData* td, Allocator allocator,
                       const Theme* theme_ptr) {
  const Theme& theme = *theme_ptr;

  if (td->events.size > 0) {
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(
        canvas_pos,
        ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
        theme.viewport_bg);
    double duration = tv->viewport.end_time - tv->viewport.start_time;
    if (duration <= 0) duration = 1.0;

    float ruler_height = ImGui::GetFrameHeight();
    trace_viewer_draw_time_ruler(tv, draw_list, canvas_pos,
                                 ImVec2(canvas_size.x, ruler_height), theme);

    float lane_height = ImGui::GetFrameHeight();

    ImGuiWindowFlags child_flags = ImGuiWindowFlags_NoMove;
    if (ImGui::IsKeyDown(ImGuiMod_Ctrl))
      child_flags |= ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::SetCursorScreenPos(
        ImVec2(canvas_pos.x, canvas_pos.y + ruler_height));
    if (ImGui::BeginChild("TrackList", ImVec2(0, canvas_size.y - ruler_height),
                          ImGuiChildFlags_None, child_flags)) {
      if (ImGui::IsWindowHovered() &&
          ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImGui::SetScrollY(ImGui::GetScrollY() - ImGui::GetIO().MouseDelta.y);
      }

      ImDrawList* track_draw_list = ImGui::GetWindowDrawList();

      float total_height = 0.0f;
      for (size_t i = 0; i < tv->tracks.size; i++) {
        const Track& t = tv->tracks[i];
        if (t.type == TRACK_TYPE_COUNTER) {
          total_height += 2.0f * lane_height;
        } else {
          total_height += (float)(t.max_depth + 2) * lane_height;
        }
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
      bool something_hovered = false;

      array_list_clear(&tv->hover_matches);

      for (size_t i = 0; i < tv->tracks.size; i++) {
        const Track& t = tv->tracks[i];
        float track_height = (t.type == TRACK_TYPE_COUNTER)
                                 ? 2.0f * lane_height
                                 : (float)(t.max_depth + 2) * lane_height;
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
            ImVec2(track_pos.x + inner_width, track_pos.y + lane_height - 1),
            theme.track_separator);

        // Sticky header text
        float sticky_x = std::max(track_pos.x, tracks_canvas_pos.x);

        char default_name[128];
        Str name_str = trace_data_get_string(td, t.name_ref);
        Str id_str = trace_data_get_string(td, t.id_ref);
        const char* display_name = name_str.buf;
        size_t display_name_len = name_str.len;

        if (display_name_len == 0) {
          if (t.type == TRACK_TYPE_THREAD) {
            snprintf(default_name, sizeof(default_name), "Thread %d", t.tid);
          } else {
            snprintf(default_name, sizeof(default_name), "Counter");
          }
          display_name = default_name;
          display_name_len = strlen(default_name);
        } else if (id_str.len > 0) {
          snprintf(default_name, sizeof(default_name), "%.*s (%.*s)",
                   (int)name_str.len, name_str.buf, (int)id_str.len, id_str.buf);
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
        if (ImGui::IsMouseHoveringRect(
                text_pos, ImVec2(text_pos.x + text_size.x,
                                 text_pos.y + text_size.y))) {
          ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                              ImVec2(10.0f, 10.0f));
          ImGui::BeginTooltip();
          ImGui::Text("PID: %d", t.pid);
          if (t.type == TRACK_TYPE_THREAD) {
            ImGui::Text("TID: %d", t.tid);
          }
          if (name_str.len > 0) {
            ImGui::Separator();
            ImGui::Text("Name: %.*s", (int)name_str.len, name_str.buf);
          }
          if (id_str.len > 0) {
            ImGui::Text("ID: %.*s", (int)id_str.len, id_str.buf);
          }
          ImGui::EndTooltip();
          ImGui::PopStyleVar();
        }

        if (t.type == TRACK_TYPE_THREAD) {
          track_compute_render_blocks(
              &t, td, tv->viewport.start_time, tv->viewport.end_time,
              inner_width, tracks_canvas_pos.x, tv->selected_event_index,
              &tv->track_renderer_state, &tv->render_blocks, allocator);

          bool mouse_in_track_y = (mouse_pos.y >= track_pos.y + lane_height &&
                                   mouse_pos.y < track_pos.y + track_height);

          for (size_t k = 0; k < tv->render_blocks.size; k++) {
            const TrackRenderBlock& rb = tv->render_blocks[k];

            float y1 = track_pos.y + (float)(rb.depth + 1) * lane_height;
            float y2 = y1 + lane_height - 1.0f;

            float x1 = rb.x1;
            float x2 = rb.x2;
            float border_thickness =
                rb.is_selected ? TRACK_MIN_EVENT_WIDTH : 1.0f;
            float min_width = 2.0f * border_thickness + 1.0f;
            if (x2 - x1 < min_width) x2 = x1 + min_width;

            if (track_list_hovered && mouse_in_track_y) {
              if (mouse_pos.x >= x1 && mouse_pos.x < x2 && mouse_pos.y >= y1 &&
                  mouse_pos.y < y2) {
                HoverMatch hm = {i, k, y1, y2, rb};
                array_list_push_back(&tv->hover_matches, allocator, hm);
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
              trace_viewer_draw_event(tv, td, track_draw_list, rb.x1, rb.x2, y1,
                                      y2, rb.color, false, rb.name_ref,
                                      inner_width, tracks_canvas_pos.x, theme);
            }
          }
        } else {
          trace_viewer_draw_counter_track(
              tv, td, track_draw_list, t,
              ImVec2(track_pos.x, track_pos.y + lane_height), inner_width,
              track_height - lane_height, tv->viewport.start_time,
              tv->viewport.end_time, (double)tv->viewport.min_ts, theme,
              mouse_pos, track_list_hovered, was_drag, &something_hovered,
              allocator);
        }

        cumulative_y += track_height;
      }

      // Handle hover highlighting and tooltip
      if (tv->hover_matches.size > 0) {
        something_hovered = true;
        // Best match is the one drawn last
        const HoverMatch* best_hm =
            &tv->hover_matches[tv->hover_matches.size - 1];

        const TrackRenderBlock& rb = best_hm->rb;
        const Track& t = tv->tracks[best_hm->track_idx];

        // Re-draw hovered block with highlight
        ImU32 col = rb.color;
        if (!rb.is_selected) {
          ImVec4 col_v = ImGui::ColorConvertU32ToFloat4(col);
          col_v.x = std::min(col_v.x + 0.15f, 1.0f);
          col_v.y = std::min(col_v.y + 0.15f, 1.0f);
          col_v.z = std::min(col_v.z + 0.15f, 1.0f);
          col = ImGui::ColorConvertFloat4ToU32(col_v);

          trace_viewer_draw_event(tv, td, track_draw_list, rb.x1, rb.x2,
                                  best_hm->y1, best_hm->y2, col, false,
                                  rb.name_ref, inner_width,
                                  tracks_canvas_pos.x, theme);
        }

        // Show tooltip
        if (rb.count == 1) {
          size_t start_idx = track_find_visible_start_index(
              &t, td, (int64_t)tv->viewport.start_time);
          for (size_t idx_k = start_idx; idx_k < t.event_indices.size;
               idx_k++) {
            size_t event_idx = t.event_indices[idx_k];
            const TraceEventPersisted& e = td->events[event_idx];
            if (e.ts > (int64_t)tv->viewport.end_time) break;
            if (t.depths[idx_k] != rb.depth) continue;

            float ex1 =
                (float)(tracks_canvas_pos.x +
                        ((double)e.ts - tv->viewport.start_time) *
                            inv_duration);
            float ex2 = (float)(ex1 + (double)e.dur * inv_duration);
            if (ex2 - ex1 < TRACK_MIN_EVENT_WIDTH)
              ex2 = ex1 + TRACK_MIN_EVENT_WIDTH;

            if (mouse_pos.x >= ex1 && mouse_pos.x < ex2) {
              Str name = trace_data_get_string(td, e.name_ref);
              Str cat = trace_data_get_string(td, e.cat_ref);

              ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                                  ImVec2(10.0f, 10.0f));
              ImGui::BeginTooltip();
              ImGui::TextUnformatted(name.buf, name.buf + name.len);
              if (cat.len > 0) {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                   "Category: %.*s", (int)cat.len, cat.buf);
              }
              ImGui::Separator();

              char ts_buf[32];
              format_duration(ts_buf, sizeof(ts_buf),
                              (double)e.ts - (double)tv->viewport.min_ts);
              ImGui::Text("Start: %s", ts_buf);

              char dur_buf[32];
              format_duration(dur_buf, sizeof(dur_buf), (double)e.dur);
              ImGui::Text("Duration: %s", dur_buf);

              if (e.args_count > 0) {
                ImGui::Separator();
                for (uint32_t arg_idx = 0; arg_idx < e.args_count;
                     arg_idx++) {
                  const TraceArgPersisted& arg =
                      td->args[e.args_offset + arg_idx];
                  Str key = trace_data_get_string(td, arg.key_ref);
                  if (arg.val_ref != 0) {
                    Str val = trace_data_get_string(td, arg.val_ref);
                    ImGui::Text("%.*s: %.*s", (int)key.len, key.buf,
                                (int)val.len, val.buf);
                  } else {
                    ImGui::Text("%.*s: %g", (int)key.len, key.buf,
                                arg.val_double);
                  }
                }
              }
              ImGui::EndTooltip();
              ImGui::PopStyleVar();
              break;
            }
          }
        } else {
          ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                              ImVec2(10.0f, 10.0f));
          ImGui::BeginTooltip();
          ImGui::Text("%u merged events", rb.count);
          ImGui::EndTooltip();
          ImGui::PopStyleVar();
        }

        // Handle selection
        if (ImGui::IsMouseReleased(0) && !was_drag) {
          size_t start_idx = track_find_visible_start_index(
              &t, td, (int64_t)tv->viewport.start_time);
          for (size_t idx_k = start_idx; idx_k < t.event_indices.size;
               idx_k++) {
            size_t event_idx = t.event_indices[idx_k];
            const TraceEventPersisted& e = td->events[event_idx];
            if (e.ts > (int64_t)tv->viewport.end_time) break;
            if (t.depths[idx_k] != rb.depth) continue;

            float ex1 =
                (float)(tracks_canvas_pos.x +
                        ((double)e.ts - tv->viewport.start_time) *
                            inv_duration);
            float ex2 = (float)(ex1 + (double)e.dur * inv_duration);
            if (ex2 - ex1 < TRACK_MIN_EVENT_WIDTH)
              ex2 = ex1 + TRACK_MIN_EVENT_WIDTH;

            if (mouse_pos.x >= ex1 && mouse_pos.x < ex2) {
              tv->selected_event_index = (int64_t)event_idx;
              break;
            }
          }
        }
      }

      if (sel_found) {
        trace_viewer_draw_event(tv, td, track_draw_list, sel_x1, sel_x2,
                                sel_y1, sel_y2, sel_col, true, sel_name_ref,
                                inner_width, tracks_canvas_pos.x, theme);
      }

      if (track_list_hovered && !something_hovered &&
          ImGui::IsMouseReleased(0) && !was_drag) {
        tv->selected_event_index = -1;
      }
    }
    ImGui::EndChild();

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
      if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        double dx = (double)ImGui::GetIO().MouseDelta.x;
        double dt = (dx / (double)canvas_size.x) *
                    (tv->viewport.end_time - tv->viewport.start_time);
        tv->viewport.start_time -= dt;
        tv->viewport.end_time -= dt;
      }

      float wheel_v = ImGui::GetIO().MouseWheel;
      float wheel_h = ImGui::GetIO().MouseWheelH;

      if (wheel_v != 0.0f && ImGui::IsKeyDown(ImGuiMod_Ctrl)) {
        double mouse_x_rel =
            (double)(ImGui::GetIO().MousePos.x - canvas_pos.x) /
            (double)canvas_size.x;
        double current_duration =
            tv->viewport.end_time - tv->viewport.start_time;
        double mouse_ts =
            tv->viewport.start_time + mouse_x_rel * current_duration;
        double zoom_factor = (wheel_v > 0.0f) ? 0.8 : TRACE_VIEWER_MAX_ZOOM_FACTOR;
        double new_duration = current_duration * zoom_factor;

        // Enforce zoom limits
        double trace_duration =
            (double)(tv->viewport.max_ts - tv->viewport.min_ts);
        double max_duration = trace_duration * TRACE_VIEWER_MAX_ZOOM_FACTOR;
        double min_duration = TRACE_VIEWER_MIN_ZOOM_DURATION;
        if (max_duration < min_duration) max_duration = min_duration;

        if (new_duration < min_duration) new_duration = min_duration;
        if (new_duration > max_duration) new_duration = max_duration;

        tv->viewport.start_time = mouse_ts - mouse_x_rel * new_duration;
        tv->viewport.end_time = tv->viewport.start_time + new_duration;
      } else if (wheel_h != 0.0f ||
                 (wheel_v != 0.0f && ImGui::IsKeyDown(ImGuiMod_Shift))) {
        float delta = (wheel_h != 0.0f) ? wheel_h : wheel_v;
        double dx =
            (double)delta * 100.0;  // 100 pixels per tick sensitivity
        double dt = (dx / (double)canvas_size.x) *
                    (tv->viewport.end_time - tv->viewport.start_time);
        tv->viewport.start_time -= dt;
        tv->viewport.end_time -= dt;
      }
    }
  }

  // Details Panel
  if (tv->show_details_panel) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
    if (ImGui::Begin("Details", &tv->show_details_panel)) {
      if (tv->selected_event_index != -1) {
        const TraceEventPersisted& e = td->events[(size_t)tv->selected_event_index];
        Str name = trace_data_get_string(td, e.name_ref);
        Str cat = trace_data_get_string(td, e.cat_ref);
        Str ph = trace_data_get_string(td, e.ph_ref);

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
            const TraceArgPersisted& arg = td->args[e.args_offset + k];
            Str key = trace_data_get_string(td, arg.key_ref);
            if (arg.val_ref != 0) {
              Str val = trace_data_get_string(td, arg.val_ref);
              ImGui::BulletText("%.*s: %.*s", (int)key.len, key.buf,
                                (int)val.len, val.buf);
            } else {
              ImGui::BulletText("%.*s: %g", (int)key.len, key.buf,
                                arg.val_double);
            }
          }
        }
      } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                           "Select an event to see details.");
      }
    }
    ImGui::End();
    ImGui::PopStyleVar();
  }
}

void trace_viewer_reset_view(TraceViewer* tv) {
  double trace_start = (double)tv->viewport.min_ts;
  double trace_end = (double)tv->viewport.max_ts;
  double trace_duration = trace_end - trace_start;

  double max_duration = trace_duration * TRACE_VIEWER_MAX_ZOOM_FACTOR;
  double min_duration = TRACE_VIEWER_MIN_ZOOM_DURATION;
  if (max_duration < min_duration) max_duration = min_duration;

  double center = (trace_start + trace_end) * 0.5;
  tv->viewport.start_time = center - max_duration * 0.5;
  tv->viewport.end_time = center + max_duration * 0.5;
}
