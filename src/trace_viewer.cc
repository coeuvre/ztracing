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

static void trace_viewer_draw_selection_overlay(
    TraceViewer* tv, ImDrawList* draw_list, ImVec2 pos, ImVec2 size,
    const Theme& theme, bool draw_duration_text);

const double TRACE_VIEWER_MAX_ZOOM_FACTOR = 1.2;
const double TRACE_VIEWER_MIN_ZOOM_DURATION = 1000.0;  // 1ms = 1000us

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

  trace_viewer_draw_selection_overlay(tv, draw_list, pos, size, theme, false);
}

static void trace_viewer_draw_selection_overlay(
    TraceViewer* tv, ImDrawList* draw_list, ImVec2 pos, ImVec2 size,
    const Theme& theme, bool draw_duration_text) {
  if (!tv->timeline_selection.active) return;

  double t1 = tv->timeline_selection.start_time;
  double t2 = tv->timeline_selection.end_time;
  if (t1 > t2) std::swap(t1, t2);

  double viewport_duration = tv->viewport.end_time - tv->viewport.start_time;
  float x1 = (float)(pos.x + (t1 - tv->viewport.start_time) /
                                 viewport_duration * size.x);
  float x2 = (float)(pos.x + (t2 - tv->viewport.start_time) /
                                 viewport_duration * size.x);

  // Dim areas outside of the selection
  float dim_x1_left = pos.x;
  float dim_x2_left = std::max(pos.x, std::min(x1, pos.x + size.x));
  float dim_x1_right = std::min(pos.x + size.x, std::max(x2, pos.x));
  float dim_x2_right = pos.x + size.x;

  if (dim_x1_left < dim_x2_left) {
    draw_list->AddRectFilled(ImVec2(dim_x1_left, pos.y),
                             ImVec2(dim_x2_left, pos.y + size.y),
                             theme.timeline_selection_bg);
  }
  if (dim_x1_right < dim_x2_right) {
    draw_list->AddRectFilled(ImVec2(dim_x1_right, pos.y),
                             ImVec2(dim_x2_right, pos.y + size.y),
                             theme.timeline_selection_bg);
  }

  // Draw vertical lines
  if (x1 >= pos.x && x1 <= pos.x + size.x) {
    draw_list->AddLine(ImVec2(x1, pos.y), ImVec2(x1, pos.y + size.y),
                       theme.timeline_selection_line, 1.0f);
  }
  if (x2 >= pos.x && x2 <= pos.x + size.x) {
    draw_list->AddLine(ImVec2(x2 - 1.0f, pos.y), ImVec2(x2 - 1.0f, pos.y + size.y),
                       theme.timeline_selection_line, 1.0f);
  }

  if (draw_duration_text) {
    char duration_label[64];
    format_duration(duration_label, sizeof(duration_label), t2 - t1, t2 - t1);
    ImVec2 text_size = ImGui::CalcTextSize(duration_label);
    float text_x = (x1 + x2) * 0.5f - text_size.x * 0.5f;
    float text_y = pos.y + (size.y - text_size.y) * 0.5f;

    // Ensure text is visible even if partially off-screen
    text_x = std::max(
        pos.x + 5.0f, std::min(text_x, pos.x + size.x - text_size.x - 5.0f));

    draw_list->AddText(ImVec2(text_x, text_y), theme.timeline_selection_text,
                       duration_label);

    // Draw arrow lines to both sides of the text
    float line_y = text_y + text_size.y * 0.5f;
    float arrow_size = 5.0f;
    ImU32 line_col = theme.timeline_selection_line;

    // Left line and arrow
    float left_line_end_x = text_x - 5.0f;
    if (left_line_end_x > x1) {
      draw_list->AddLine(ImVec2(x1, line_y), ImVec2(left_line_end_x, line_y),
                         line_col, 1.0f);
      draw_list->AddLine(ImVec2(x1, line_y), ImVec2(x1 + arrow_size, line_y - arrow_size),
                         line_col, 1.0f);
      draw_list->AddLine(ImVec2(x1, line_y), ImVec2(x1 + arrow_size, line_y + arrow_size),
                         line_col, 1.0f);
    }

    // Right line and arrow
    float right_line_start_x = text_x + text_size.x + 5.0f;
    float adjusted_x2 = x2 - 1.0f;
    if (adjusted_x2 > right_line_start_x) {
      draw_list->AddLine(ImVec2(right_line_start_x, line_y),
                         ImVec2(adjusted_x2, line_y), line_col, 1.0f);
      draw_list->AddLine(
          ImVec2(adjusted_x2, line_y),
          ImVec2(adjusted_x2 - arrow_size, line_y - arrow_size), line_col,
          1.0f);
      draw_list->AddLine(
          ImVec2(adjusted_x2, line_y),
          ImVec2(adjusted_x2 - arrow_size, line_y + arrow_size), line_col,
          1.0f);
    }
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

  float border_thickness = is_selected ? 2.0f : 1.0f;
  float event_width = x2 - x1;
  bool draw_border = (event_width > TRACK_MIN_EVENT_WIDTH);

  draw_list->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2), col);

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
        std::string_view name = trace_data_get_string(td, name_ref);
        if (name.size() > 0) {
          ImU32 text_col =
              is_selected ? theme.event_text_selected : theme.event_text;
          float event_font_size = ImGui::GetFontSize();
          float text_y = y1 + (lane_height - event_font_size) * 0.5f;

          float text_width = ImGui::GetFont()
                                 ->CalcTextSizeA(event_font_size, FLT_MAX, 0.0f,
                                                 name.data(), name.data() + name.size())
                                 .x;

          float text_x = std::max(x1 + padding_h, x1 + (event_width - text_width) * 0.5f);

          ImVec4 fine_clip_rect(visible_x1 + padding_h, y1,
                                visible_x2 - padding_h, y2);
          const ImVec4* clip_ptr = &fine_clip_rect;

          draw_list->AddText(ImGui::GetFont(), event_font_size,
                             ImVec2(text_x, text_y), text_col, name.data(),
                             name.data() + name.size(), 0.0f, clip_ptr);
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
  array_list_resize(&state->counter_current_values, allocator, t.counter_series.size);

  track_compute_counter_render_blocks(&t, td, viewport_start, viewport_end,
                                      width, pos.x, state,
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
        tv->show_details_panel = true;
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
          std::string_view key = trace_data_get_string(td, key_ref);
          ImGui::Text("%.*s: %g", (int)key.size(), key.data(), val);
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
    float threshold = 5.0f;

    // Use stationary origins for both ruler and tracks to ensure perfect alignment.
    // We use the results from the LAST frame to map the Ruler for THIS frame's interaction.
    float tracks_origin_x = tv->last_tracks_x > 0 ? tv->last_tracks_x : canvas_pos.x;
    float tracks_inner_width = tv->last_inner_width > 0 ? tv->last_inner_width : canvas_size.x;

    TimelineViewportMapping mapping = {
        tv->viewport.start_time, tv->viewport.end_time, tracks_origin_x,
        tracks_inner_width};

    // Ruler interaction - Capture state
    ImGui::SetCursorScreenPos(ImVec2(tracks_origin_x, canvas_pos.y));
    ImGui::InvisibleButton("##Ruler", ImVec2(tracks_inner_width, ruler_height));

    TimelineInteraction interaction_r = {};
    interaction_r.area = TimelineInteraction::AREA_RULER;
    interaction_r.mouse_px = ImGui::GetMousePos().x;
    interaction_r.click_px = ImGui::GetIO().MouseClickedPos[0].x;
    interaction_r.mouse_wheel = ImGui::GetIO().MouseWheel;
    interaction_r.mouse_wheel_h = ImGui::GetIO().MouseWheelH;
    interaction_r.is_ctrl_down = ImGui::IsKeyDown(ImGuiMod_Ctrl);
    interaction_r.is_shift_down = ImGui::IsKeyDown(ImGuiMod_Shift);
    interaction_r.drag_delta_x = ImGui::GetMouseDragDelta(0).x;
    interaction_r.drag_threshold = ImGui::GetIO().MouseDragThreshold;
    interaction_r.ruler_active = ImGui::IsItemActive();
    interaction_r.ruler_activated = ImGui::IsItemActivated();
    interaction_r.ruler_deactivated = ImGui::IsItemDeactivated();

    // Mapping for current frame's mouse (un-snapped)
    double mouse_ts_ruler = timeline_mapping_px_to_ts(mapping, interaction_r.mouse_px);

    TimelineSelectionProximity proximity_r = timeline_selection_check_proximity(
        tv->timeline_selection, mouse_ts_ruler, (threshold / (double)tracks_inner_width) * duration);

    if (proximity_r.near_start || proximity_r.near_end ||
        tv->timeline_selection.drag_mode == TimelineDragMode::RULER_START ||
        tv->timeline_selection.drag_mode == TimelineDragMode::RULER_END) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }

    float lane_height = ImGui::GetFrameHeight();
    float counter_track_height = 3.0f * lane_height;

    // Snapping state for the frame
    TimelineSnappingState snapping = {};
    timeline_snapping_init(&snapping, mouse_ts_ruler, threshold);

    ImGuiWindowFlags child_flags = ImGuiWindowFlags_NoMove;
    if (ImGui::IsKeyDown(ImGuiMod_Ctrl))
      child_flags |= ImGuiWindowFlags_NoScrollWithMouse;

    // Tracks interaction state to capture
    TimelineInteraction interaction_t = interaction_r;
    interaction_t.area = TimelineInteraction::AREA_TRACKS;

    ImGui::SetCursorScreenPos(
        ImVec2(canvas_pos.x, canvas_pos.y + ruler_height));
    if (ImGui::BeginChild("TrackList", ImVec2(0, canvas_size.y - ruler_height),
                          ImGuiChildFlags_None, child_flags)) {
      if (ImGui::IsWindowHovered() &&
          ImGui::IsMouseDragging(ImGuiMouseButton_Left) &&
          tv->timeline_selection.drag_mode == TimelineDragMode::NONE) {
        ImGui::SetScrollY(ImGui::GetScrollY() - ImGui::GetIO().MouseDelta.y);
      }

      ImDrawList* track_draw_list = ImGui::GetWindowDrawList();

      float total_height = 0.0f;
      for (size_t i = 0; i < tv->tracks.size; i++) {
        const Track& t = tv->tracks[i];
        if (t.type == TRACK_TYPE_COUNTER) {
          total_height += counter_track_height;
        } else {
          total_height += (float)(t.max_depth + 2) * lane_height;
        }
      }
      ImGui::Dummy(ImVec2(0.0f, total_height));
      ImGui::SetCursorPos(ImVec2(0, 0));

      ImVec2 tracks_canvas_pos = ImGui::GetCursorScreenPos();
      float inner_width = ImGui::GetContentRegionAvail().x;
      tv->last_inner_width = inner_width;
      tv->last_tracks_x = tracks_canvas_pos.x;

      // Update mapping for tracks area if it changed (e.g. scrollbar appeared)
      mapping.origin_x = tracks_canvas_pos.x;
      mapping.width = inner_width;

      // Timeline selection interaction (tracks area) - Capture state
      interaction_t.mouse_ts = timeline_mapping_px_to_ts(mapping, interaction_t.mouse_px);
      interaction_t.tracks_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
      
      // Only capture click/down if the tracks area is hovered and ruler is NOT active
      // to avoid conflicting with ruler and allow panning.
      if (interaction_t.tracks_hovered && !interaction_r.ruler_active) {
        interaction_t.is_mouse_clicked = ImGui::IsMouseClicked(0);
        interaction_t.is_mouse_down = ImGui::IsMouseDown(0);
        interaction_t.is_mouse_released = ImGui::IsMouseReleased(0);
      }

      // Refine snapping default for tracks area if relevant
      if (interaction_t.tracks_hovered) {
        snapping.best_snap_ts = interaction_t.mouse_ts;
      }

      TimelineSelectionProximity proximity_t =
          timeline_selection_check_proximity(tv->timeline_selection,
                                             interaction_t.mouse_ts,
                                             (threshold / (double)inner_width) * duration);

      if (proximity_t.near_start || proximity_t.near_end ||
          tv->timeline_selection.drag_mode == TimelineDragMode::TRACKS_START ||
          tv->timeline_selection.drag_mode == TimelineDragMode::TRACKS_END) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
      }

      ImVec2 mouse_pos = ImGui::GetMousePos();
      bool mouse_in_selection = timeline_selection_is_mouse_inside(
          tv->timeline_selection, interaction_t.mouse_ts);

      bool track_list_hovered =
          interaction_t.tracks_hovered &&
          mouse_in_selection &&
          tv->timeline_selection.drag_mode == TimelineDragMode::NONE &&
          !proximity_t.near_start && !proximity_t.near_end;

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
                                 ? counter_track_height
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
        std::string_view name_str = trace_data_get_string(td, t.name_ref);
        std::string_view id_str = trace_data_get_string(td, t.id_ref);
        const char* display_name = name_str.data();
        size_t display_name_len = name_str.size();

        if (display_name_len == 0) {
          if (t.type == TRACK_TYPE_THREAD) {
            snprintf(default_name, sizeof(default_name), "Thread %d", t.tid);
          } else {
            snprintf(default_name, sizeof(default_name), "Counter");
          }
          display_name = default_name;
          display_name_len = strlen(default_name);
        } else if (id_str.size() > 0) {
          snprintf(default_name, sizeof(default_name), "%.*s (%.*s)",
                   (int)name_str.size(), name_str.data(), (int)id_str.size(), id_str.data());
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
        if (mouse_in_selection && ImGui::IsMouseHoveringRect(
                text_pos, ImVec2(text_pos.x + text_size.x,
                                 text_pos.y + text_size.y))) {
          ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                              ImVec2(10.0f, 10.0f));
          ImGui::BeginTooltip();
          ImGui::Text("PID: %d", t.pid);
          if (t.type == TRACK_TYPE_THREAD) {
            ImGui::Text("TID: %d", t.tid);
          }
          if (name_str.size() > 0) {
            ImGui::Separator();
            ImGui::Text("Name: %.*s", (int)name_str.size(), name_str.data());
          }
          if (id_str.size() > 0) {
            ImGui::Text("ID: %.*s", (int)id_str.size(), id_str.data());
          }
          ImGui::EndTooltip();
          ImGui::PopStyleVar();
        }

        if (t.type == TRACK_TYPE_THREAD) {
          track_compute_render_blocks(
              &t, td, tv->viewport.start_time, tv->viewport.end_time,
              inner_width, tracks_canvas_pos.x, tv->selected_event_index,
              &tv->track_renderer_state, &tv->render_blocks, allocator);

          // Snapping: check thread event boundaries
          for (size_t k = 0; k < tv->render_blocks.size; k++) {
            const TrackRenderBlock& rb = tv->render_blocks[k];

            float y1 = track_pos.y + (float)(rb.depth + 1) * lane_height;
            float y2 = y1 + lane_height - 1.0f;

            // Snap to block start
            double ts1 = tv->viewport.start_time +
                         ((double)(rb.x1 - tracks_canvas_pos.x) /
                          (double)inner_width) *
                             duration;
            timeline_snapping_suggest(&snapping, ts1, rb.x1, interaction_t.mouse_px,
                                       y1, y2);
            // Snap to block end
            double ts2 = tv->viewport.start_time +
                         ((double)(rb.x2 - tracks_canvas_pos.x) /
                          (double)inner_width) *
                             duration;
            timeline_snapping_suggest(&snapping, ts2, rb.x2, interaction_t.mouse_px,
                                       y1, y2);
          }

          bool mouse_in_track_y = (mouse_pos.y >= track_pos.y + lane_height &&
                                   mouse_pos.y < track_pos.y + track_height);

          for (size_t k = 0; k < tv->render_blocks.size; k++) {
            const TrackRenderBlock& rb = tv->render_blocks[k];

            float y1 = track_pos.y + (float)(rb.depth + 1) * lane_height;
            float y2 = y1 + lane_height - 1.0f;

            float x1 = rb.x1;
            float x2 = rb.x2;

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
                                      y2, rb.color, false,
                                      rb.name_ref, inner_width,
                                      tracks_canvas_pos.x, theme);
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
          const TraceEventPersisted& e = td->events[rb.event_idx];
          std::string_view name = trace_data_get_string(td, e.name_ref);
          std::string_view cat = trace_data_get_string(td, e.cat_ref);

          ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                              ImVec2(10.0f, 10.0f));
          ImGui::BeginTooltip();
          ImGui::TextUnformatted(name.data(), name.data() + name.size());
          if (cat.size() > 0) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                               "Category: %.*s", (int)cat.size(), cat.data());
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
              std::string_view key = trace_data_get_string(td, arg.key_ref);
              if (arg.val_ref != 0) {
                std::string_view val = trace_data_get_string(td, arg.val_ref);
                ImGui::Text("%.*s: %.*s", (int)key.size(), key.data(),
                            (int)val.size(), val.data());
              } else {
                ImGui::Text("%.*s: %g", (int)key.size(), key.data(),
                            arg.val_double);
              }
            }
          }
          ImGui::EndTooltip();
          ImGui::PopStyleVar();
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
          tv->selected_event_index = (int64_t)rb.event_idx;
          tv->show_details_panel = true;
        }
      }

      if (sel_found) {
        trace_viewer_draw_event(tv, td, track_draw_list, sel_x1, sel_x2, sel_y1,
                                sel_y2, sel_col, true, sel_name_ref,
                                inner_width, tracks_canvas_pos.x, theme);
      }

      if (track_list_hovered && !something_hovered &&
          ImGui::IsMouseReleased(0) && !was_drag) {
        tv->selected_event_index = -1;
      }

      // Render timeline selection overlay for tracks area (clipped to this child window)
      // Use tracks_canvas_pos.x and inner_width to stay aligned with tracks.
      // Use GetWindowPos().y to keep it stationary during vertical scroll.
      trace_viewer_draw_selection_overlay(
          tv, track_draw_list, ImVec2(tracks_canvas_pos.x, ImGui::GetWindowPos().y),
          ImVec2(inner_width, ImGui::GetWindowSize().y), theme, true);

      // Highlight the snapped edge if dragging
      if (snapping.has_snap &&
          tv->timeline_selection.drag_mode != TimelineDragMode::NONE) {
        track_draw_list->AddLine(ImVec2(snapping.snap_px, snapping.snap_y1),
                                 ImVec2(snapping.snap_px, snapping.snap_y2),
                                 IM_COL32(255, 0, 0, 255), 3.0f);
      }
    }
    ImGui::EndChild();

    // Process interaction logic consolidated here
    timeline_selection_step(&tv->timeline_selection, interaction_r, mapping, snapping);
    timeline_selection_step(&tv->timeline_selection, interaction_t, mapping, snapping);

    // Process viewport logic (panning/zooming)
    ViewportState vs = {tv->viewport.start_time, tv->viewport.end_time,
                        tv->viewport.min_ts, tv->viewport.max_ts};
    viewport_step(&vs, interaction_t, mapping, tv->timeline_selection);

    // Handle panning manually for now as it uses per-frame delta
    if (interaction_t.tracks_hovered && !interaction_r.ruler_active &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left) &&
        tv->timeline_selection.drag_mode == TimelineDragMode::NONE) {
      double dx = (double)ImGui::GetIO().MouseDelta.x;
      double current_dur = vs.end_time - vs.start_time;
      double dt = (dx / (double)canvas_size.x) * current_dur;
      vs.start_time -= dt;

      // Clamp start_time to keep selection visible with gaps if active
      if (tv->timeline_selection.active) {
        double t1 = tv->timeline_selection.start_time;
        double t2 = tv->timeline_selection.end_time;
        if (t1 > t2) std::swap(t1, t2);
        double gap = current_dur * 0.05;  // 5% gap
        if (vs.start_time > t1 - gap) vs.start_time = t1 - gap;
        if (vs.start_time + current_dur < t2 + gap)
          vs.start_time = t2 + gap - current_dur;
      }
      vs.end_time = vs.start_time + current_dur;
    }

    tv->viewport.start_time = vs.start_time;
    tv->viewport.end_time = vs.end_time;

    // Draw ruler on top of the tracks
    trace_viewer_draw_time_ruler(
        tv, draw_list, ImVec2(tracks_origin_x, canvas_pos.y),
        ImVec2(tracks_inner_width, ruler_height), theme);

    tv->last_best_snap_ts = snapping.best_snap_ts;
  }

  // Details Panel
  if (tv->show_details_panel) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
    if (ImGui::Begin("Details", &tv->show_details_panel,
                     ImGuiWindowFlags_NoFocusOnAppearing)) {
      if (tv->selected_event_index != -1) {
        const TraceEventPersisted& e = td->events[(size_t)tv->selected_event_index];
        std::string_view name = trace_data_get_string(td, e.name_ref);
        std::string_view cat = trace_data_get_string(td, e.cat_ref);
        std::string_view ph = trace_data_get_string(td, e.ph_ref);

        ImGui::Text("Name: %.*s", (int)name.size(), name.data());
        ImGui::Text("Category: %.*s", (int)cat.size(), cat.data());
        ImGui::Text("PH: %.*s", (int)ph.size(), ph.data());
        ImGui::Text("Timestamp: %lld", (long long)e.ts);
        ImGui::Text("Duration: %lld", (long long)e.dur);
        ImGui::Text("PID: %d, TID: %d", e.pid, e.tid);

        if (e.args_count > 0) {
          ImGui::Separator();
          ImGui::Text("Arguments:");
          for (uint32_t k = 0; k < e.args_count; k++) {
            const TraceArgPersisted& arg = td->args[e.args_offset + k];
            std::string_view key = trace_data_get_string(td, arg.key_ref);
            if (arg.val_ref != 0) {
              std::string_view val = trace_data_get_string(td, arg.val_ref);
              ImGui::BulletText("%.*s: %.*s", (int)key.size(), key.data(),
                                (int)val.size(), val.data());
            } else {
              ImGui::BulletText("%.*s: %g", (int)key.size(), key.data(),
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
