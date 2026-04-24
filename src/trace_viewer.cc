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

static double trace_viewer_px_to_ts(double start_time, double end_time,
                                    float width, float origin_x, float px) {
  double duration = end_time - start_time;
  if (width <= 0) return start_time;
  return start_time + ((double)(px - origin_x) / (double)width) * duration;
}

static void trace_viewer_snapping_reset(TraceViewer* tv, double mouse_ts,
                                       float threshold_px) {
  tv->snap_best_ts = mouse_ts;
  tv->snap_best_dist_px = threshold_px;
  tv->snap_threshold_px = threshold_px;
  tv->snap_has_snap = false;
  tv->snap_px = 0.0f;
  tv->snap_y1 = 0.0f;
  tv->snap_y2 = 0.0f;
}

static void trace_viewer_snapping_suggest(TraceViewer* tv, double candidate_ts,
                                          float candidate_px, float mouse_px,
                                          float y1, float y2) {
  float dist_px = std::abs(candidate_px - mouse_px);
  if (dist_px < tv->snap_best_dist_px) {
    tv->snap_best_dist_px = dist_px;
    tv->snap_best_ts = candidate_ts;
    tv->snap_has_snap = true;
    tv->snap_px = candidate_px;
    tv->snap_y1 = y1;
    tv->snap_y2 = y2;
  }
}

struct SelectionProximity {
  bool near_start;
  bool near_end;
};

static SelectionProximity trace_viewer_selection_check_proximity(
    const TraceViewer* tv, double mouse_ts, double threshold_ts) {
  SelectionProximity result = {false, false};
  if (!tv->selection_active) return result;

  double dist_start = std::abs(mouse_ts - tv->selection_start_time);
  double dist_end = std::abs(mouse_ts - tv->selection_end_time);

  if (dist_start < threshold_ts) result.near_start = true;
  if (dist_end < threshold_ts) result.near_end = true;

  if (result.near_start && result.near_end) {
    if (dist_start < dist_end)
      result.near_end = false;
    else
      result.near_start = false;
  }
  return result;
}

static bool trace_viewer_selection_is_mouse_inside(const TraceViewer* tv,
                                                   double mouse_ts) {
  if (!tv->selection_active) return true;
  double t1 = tv->selection_start_time;
  double t2 = tv->selection_end_time;
  if (t1 > t2) std::swap(t1, t2);
  return mouse_ts >= t1 && mouse_ts <= t2;
}

const double TRACE_VIEWER_MAX_ZOOM_FACTOR = 1.2;
const double TRACE_VIEWER_MIN_ZOOM_DURATION = 1000.0;  // 1ms = 1000us

void trace_viewer_deinit(TraceViewer* tv, Allocator allocator) {
  for (size_t i = 0; i < tv->tracks.size; i++) {
    track_deinit(&tv->tracks[i], allocator);
  }
  array_list_deinit(&tv->tracks, allocator);
  array_list_deinit(&tv->track_infos, allocator);
  array_list_deinit(&tv->ruler_ticks, allocator);
  track_renderer_state_deinit(&tv->track_renderer_state, allocator);
  array_list_deinit(&tv->render_blocks, allocator);
  array_list_deinit(&tv->counter_render_blocks, allocator);
  array_list_deinit(&tv->hover_matches, allocator);
}

static void trace_viewer_draw_time_ruler(TraceViewer* tv, ImDrawList* draw_list,
                                         ImVec2 pos, ImVec2 size,
                                         float canvas_x, float canvas_width,
                                         const Theme& theme) {
  // Ruler background
  draw_list->AddRectFilled(ImVec2(canvas_x, pos.y),
                           ImVec2(canvas_x + canvas_width, pos.y + size.y),
                           theme.ruler_bg);
  draw_list->AddLine(ImVec2(canvas_x, pos.y + size.y - 1),
                     ImVec2(canvas_x + canvas_width, pos.y + size.y - 1),
                     theme.ruler_border);

  for (size_t i = 0; i < tv->ruler_ticks.size; i++) {
    const RulerTick& tick = tv->ruler_ticks[i];
    draw_list->AddLine(ImVec2(tick.x, pos.y + size.y * 0.6f),
                       ImVec2(tick.x, pos.y + size.y - 1), theme.ruler_tick);
    draw_list->AddText(ImVec2(tick.x + 3, pos.y + 2), theme.ruler_text, tick.label);
  }

  trace_viewer_draw_selection_overlay(tv, draw_list, ImVec2(canvas_x, pos.y),
                                      ImVec2(canvas_width, size.y), theme,
                                      false);
}

static void trace_viewer_draw_selection_overlay(
    TraceViewer* tv, ImDrawList* draw_list, ImVec2 pos, ImVec2 size,
    const Theme& theme, bool draw_duration_text) {
  const SelectionOverlayLayout& lo = tv->selection_layout;
  if (!lo.active) return;

  // Dim areas outside of the selection
  float dim_l_x1 = pos.x;
  float dim_l_x2 = lo.x1;
  if (dim_l_x2 < pos.x) dim_l_x2 = pos.x;
  if (dim_l_x2 > pos.x + size.x) dim_l_x2 = pos.x + size.x;

  float dim_r_x1 = lo.x2;
  if (dim_r_x1 < pos.x) dim_r_x1 = pos.x;
  if (dim_r_x1 > pos.x + size.x) dim_r_x1 = pos.x + size.x;
  float dim_r_x2 = pos.x + size.x;

  if (dim_l_x1 < dim_l_x2) {
    draw_list->AddRectFilled(ImVec2(dim_l_x1, pos.y),
                             ImVec2(dim_l_x2, pos.y + size.y),
                             theme.timeline_selection_bg);
  }
  if (dim_r_x1 < dim_r_x2) {
    draw_list->AddRectFilled(ImVec2(dim_r_x1, pos.y),
                             ImVec2(dim_r_x2, pos.y + size.y),
                             theme.timeline_selection_bg);
  }

  // Draw vertical lines
  float draw_x1 = lo.x1;
  float draw_x2 = lo.x2 - 1.0f;

  if (lo.x1 >= pos.x && lo.x1 <= pos.x + size.x) {
    draw_list->AddLine(ImVec2(draw_x1, pos.y), ImVec2(draw_x1, pos.y + size.y),
                       theme.timeline_selection_line, 1.0f);
  }
  if (lo.x2 >= pos.x && lo.x2 <= pos.x + size.x) {
    draw_list->AddLine(ImVec2(draw_x2, pos.y), ImVec2(draw_x2, pos.y + size.y),
                       theme.timeline_selection_line, 1.0f);
  }

  if (draw_duration_text) {
    ImVec2 text_size = ImGui::CalcTextSize(lo.duration_label);
    float text_x = (lo.x1 + lo.x2) * 0.5f - text_size.x * 0.5f;
    float text_y = pos.y + (size.y - text_size.y) * 0.5f;

    // Ensure text is visible even if partially off-screen
    text_x = std::max(
        pos.x + 5.0f, std::min(text_x, pos.x + size.x - text_size.x - 5.0f));

    draw_list->AddText(ImVec2(text_x, text_y), theme.timeline_selection_text,
                       lo.duration_label);

    // Draw arrow lines to both sides of the text
    float line_y = text_y + text_size.y * 0.5f;
    float arrow_size = 5.0f;
    ImU32 line_col = theme.timeline_selection_line;

    // Left line and arrow
    float left_line_end_x = text_x - 5.0f;
    if (left_line_end_x > draw_x1) {
      draw_list->AddLine(ImVec2(draw_x1, line_y), ImVec2(left_line_end_x, line_y),
                         line_col, 1.0f);
      draw_list->AddLine(ImVec2(draw_x1, line_y), ImVec2(draw_x1 + arrow_size, line_y - arrow_size),
                         line_col, 1.0f);
      draw_list->AddLine(ImVec2(draw_x1, line_y), ImVec2(draw_x1 + arrow_size, line_y + arrow_size),
                         line_col, 1.0f);
    }

    // Right line and arrow
    float right_line_start_x = text_x + text_size.x + 5.0f;
    if (right_line_start_x < draw_x2) {
      draw_list->AddLine(ImVec2(right_line_start_x, line_y), ImVec2(draw_x2, line_y),
                         line_col, 1.0f);
      draw_list->AddLine(ImVec2(draw_x2, line_y), ImVec2(draw_x2 - arrow_size, line_y - arrow_size),
                         line_col, 1.0f);
      draw_list->AddLine(ImVec2(draw_x2, line_y), ImVec2(draw_x2 - arrow_size, line_y + arrow_size),
                         line_col, 1.0f);
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
  // Use a small epsilon to prevent floating point jitter from flipping the
  // border on/off during panning.
  bool draw_border = (event_width > TRACK_MIN_EVENT_WIDTH + 0.01f);

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

static void trace_viewer_draw_args_table(const TraceData* td,
                                         const TraceEventPersisted& e,
                                         const StringRef* skip_keys,
                                         size_t skip_count) {
  uint32_t visible_args = 0;
  for (uint32_t k = 0; k < e.args_count; k++) {
    const TraceArgPersisted& arg = td->args[e.args_offset + k];
    bool skip = false;
    for (size_t i = 0; i < skip_count; i++) {
      if (arg.key_ref == skip_keys[i]) {
        skip = true;
        break;
      }
    }
    if (!skip) visible_args++;
  }

  if (visible_args == 0) return;

  ImGui::Separator();
  if (ImGui::BeginTable("##args", 2, ImGuiTableFlags_SizingFixedFit)) {
    ImGui::TableSetupColumn("Key");
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

    for (uint32_t k = 0; k < e.args_count; k++) {
      const TraceArgPersisted& arg = td->args[e.args_offset + k];
      bool skip = false;
      for (size_t i = 0; i < skip_count; i++) {
        if (arg.key_ref == skip_keys[i]) {
          skip = true;
          break;
        }
      }
      if (skip) continue;

      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      std::string_view key = trace_data_get_string(td, arg.key_ref);
      ImGui::TextUnformatted(key.data(), key.data() + key.size());

      ImGui::TableNextColumn();
      if (arg.val_ref != 0) {
        std::string_view val = trace_data_get_string(td, arg.val_ref);
        ImGui::TextUnformatted(val.data(), val.data() + val.size());
      } else {
        ImGui::Text("%.2f", arg.val_double);
      }
    }
    ImGui::EndTable();
  }
}

static void trace_viewer_draw_tooltip(TraceViewer* tv, TraceData* td,
                                      const HoverMatch* best_hm, float inner_width,
                                      float tracks_canvas_pos_x) {
  const TrackRenderBlock& rb = best_hm->rb;
  if (rb.count == 1) {
    const Track& t = tv->tracks[best_hm->track_idx];
    const TraceEventPersisted& e = td->events[rb.event_idx];

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
    ImGui::BeginTooltip();

    if (t.type == TRACK_TYPE_COUNTER) {
      char ts_buf[32];
      format_duration(ts_buf, sizeof(ts_buf),
                      (double)e.ts - (double)tv->viewport.min_ts);
      ImGui::Text("Time: %s", ts_buf);
      ImGui::Separator();

      std::string_view t_name = trace_data_get_string(td, t.name_ref);
      bool single_series_redundant = false;
      if (t.counter_series.size == 1) {
        std::string_view s_name = trace_data_get_string(td, t.counter_series[0]);
        if (s_name == t_name || s_name == "" || s_name == "value" || s_name == "Value") {
          single_series_redundant = true;
        }
      }

      if (single_series_redundant) {
        double val = 0.0;
        StringRef val_s_ref = 0;
        for (uint32_t arg_k = 0; arg_k < e.args_count; arg_k++) {
          const TraceArgPersisted& arg = td->args[e.args_offset + arg_k];
          if (arg.key_ref == t.counter_series[0]) {
            val = arg.val_double;
            val_s_ref = arg.val_ref;
            break;
          }
        }
        if (val_s_ref != 0) {
          std::string_view val_s = trace_data_get_string(td, val_s_ref);
          ImGui::Text("Value: %.*s", (int)val_s.size(), val_s.data());
        } else {
          ImGui::Text("Value: %.2f", val);
        }
      } else if (ImGui::BeginTable("##counter_series", 3, ImGuiTableFlags_None)) {
        ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthFixed,
                                ImGui::GetTextLineHeight());
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed);

        double total = 0.0;
        bool has_total_series = false;

        for (size_t s_i = 0; s_i < t.counter_series.size; s_i++) {
          size_t s_idx = t.counter_series.size - 1 - s_i;
          StringRef key_ref = t.counter_series[s_idx];
          
          std::string_view s_name = trace_data_get_string(td, key_ref);
          if (s_name == "total" || s_name == "Total") has_total_series = true;

          double val = 0.0;
          StringRef val_s_ref = 0;
          for (uint32_t arg_k = 0; arg_k < e.args_count; arg_k++) {
            const TraceArgPersisted& arg = td->args[e.args_offset + arg_k];
            if (arg.key_ref == key_ref) {
              val = arg.val_double;
              val_s_ref = arg.val_ref;
              break;
            }
          }

          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImVec2 p = ImGui::GetCursorScreenPos();
          float sz = ImGui::GetTextLineHeight() * 0.7f;
          float offset = (ImGui::GetTextLineHeight() - sz) * 0.5f;
          ImGui::GetWindowDrawList()->AddRectFilled(
              ImVec2(p.x, p.y + offset), ImVec2(p.x + sz, p.y + offset + sz),
              t.counter_colors[s_idx], 2.0f);

          ImGui::TableNextColumn();
          ImGui::TextUnformatted(s_name.data(), s_name.data() + s_name.size());

          ImGui::TableNextColumn();
          if (val_s_ref != 0) {
            std::string_view val_s = trace_data_get_string(td, val_s_ref);
            ImGui::TextUnformatted(val_s.data(), val_s.data() + val_s.size());
          } else {
            ImGui::Text("%.2f", val);
          }
          total += val;
        }

        if (t.counter_series.size > 1 && !has_total_series) {
          ImGui::TableNextRow();
          for (int col = 0; col < 3; col++) {
            ImGui::TableSetColumnIndex(col);
            ImGui::Separator();
          }
          ImGui::TableNextRow();
          ImGui::TableNextColumn(); // Color
          ImGui::TableNextColumn(); // Name
          ImGui::TextUnformatted("Total");
          ImGui::TableNextColumn(); // Value
          ImGui::Text("%.2f", total);
        }
        ImGui::EndTable();
      }

      // Show extra arguments (excluding counter series)
      trace_viewer_draw_args_table(td, e, t.counter_series.data,
                                   t.counter_series.size);
    } else {
      // Thread Event Tooltip
      std::string_view name = trace_data_get_string(td, e.name_ref);
      std::string_view cat = trace_data_get_string(td, e.cat_ref);
      std::string_view id = trace_data_get_string(td, e.id_ref);

      // Color box
      ImVec2 p = ImGui::GetCursorScreenPos();
      float sz = ImGui::GetTextLineHeight() * 0.7f;
      float offset = (ImGui::GetTextLineHeight() - sz) * 0.5f;
      ImGui::GetWindowDrawList()->AddRectFilled(
          ImVec2(p.x, p.y + offset), ImVec2(p.x + sz, p.y + offset + sz),
          rb.color, 2.0f);
      
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + sz + 5.0f);
      ImGui::TextUnformatted(name.data(), name.data() + name.size());
      if (id.size() > 0) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(%.*s)", (int)id.size(), id.data());
      }

      if (cat.size() > 0) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                           "Category: %.*s", (int)cat.size(), cat.data());
      }
      ImGui::Separator();

      char ts_buf[32];
      format_duration(ts_buf, sizeof(ts_buf),
                      (double)e.ts - (double)tv->viewport.min_ts);
      ImGui::Text("Start: %s", ts_buf);

      if (e.dur > 0) {
        char dur_buf[32];
        format_duration(dur_buf, sizeof(dur_buf), (double)e.dur);
        ImGui::Text("Duration: %s", dur_buf);
      }

      trace_viewer_draw_args_table(td, e, nullptr, 0);
    }
    ImGui::EndTooltip();
    ImGui::PopStyleVar();
  } else if (rb.count > 1) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
    ImGui::BeginTooltip();
    ImGui::Text("%u merged events", rb.count);

    double ts1 = trace_viewer_px_to_ts(tv->viewport.start_time, tv->viewport.end_time,
                                       inner_width, tracks_canvas_pos_x, rb.x1);
    double ts2 = trace_viewer_px_to_ts(tv->viewport.start_time, tv->viewport.end_time,
                                       inner_width, tracks_canvas_pos_x, rb.x2);
    char dur_buf[32];
    format_duration(dur_buf, sizeof(dur_buf), ts2 - ts1);
    ImGui::Text("Duration: %s", dur_buf);

    ImGui::EndTooltip();
    ImGui::PopStyleVar();
  }
}

static void trace_viewer_draw_counter_track(
    TraceViewer* tv, TraceData* td, ImDrawList* draw_list, const Track& t,
    ImVec2 pos, float width, float height, double viewport_start,
    double viewport_end, const Theme& theme,
    ImVec2 mouse_pos, bool track_list_hovered, Allocator allocator) {
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
      draw_list->AddRectFilled(ImVec2(rb.x1, pos.y),
                               ImVec2(rb.x2, pos.y + height),
                               IM_COL32(255, 255, 255, 30));
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

void trace_viewer_step(TraceViewer* tv, TraceData* td,
                       const TraceViewerInput& input, Allocator allocator) {
  double current_duration = tv->viewport.end_time - tv->viewport.start_time;
  if (current_duration <= 0) current_duration = 1.0;

  float tracks_origin_x = tv->last_tracks_x > 0 ? tv->last_tracks_x : input.canvas_x;
  float tracks_inner_width = tv->last_inner_width > 0 ? tv->last_inner_width : input.canvas_width;
  if (tracks_inner_width <= 0) tracks_inner_width = 1.0f;

  double mouse_ts = trace_viewer_px_to_ts(tv->viewport.start_time, tv->viewport.end_time,
                                          tracks_inner_width, tracks_origin_x, input.mouse_x);

  // 1. Snapping Initialization
  float snap_threshold_px = 5.0f;
  trace_viewer_snapping_reset(tv, mouse_ts, snap_threshold_px);

  // 2. Interaction Logic - Pre-pass
  double threshold_ts =
      ((double)tv->snap_threshold_px / (double)tracks_inner_width) *
      current_duration;

  SelectionProximity proximity =
      trace_viewer_selection_check_proximity(tv, mouse_ts, threshold_ts);

  // Ruler Interaction
  if (input.ruler_active) {
    if (input.ruler_activated) {
      if (proximity.near_start) {
        tv->selection_drag_mode = TimelineDragMode::RULER_START;
      } else if (proximity.near_end) {
        tv->selection_drag_mode = TimelineDragMode::RULER_END;
      } else {
        tv->selection_drag_mode = TimelineDragMode::RULER_NEW;
      }
    }

    if (tv->selection_drag_mode == TimelineDragMode::RULER_NEW) {
      if (std::abs(input.drag_delta_x) >= input.drag_threshold) {
        tv->selection_active = true;
        tv->selection_start_time = trace_viewer_px_to_ts(
            tv->viewport.start_time, tv->viewport.end_time, tracks_inner_width,
            tracks_origin_x, input.click_x);
      }
    }
  } else {
    if (input.ruler_deactivated) {
      if (std::abs(input.drag_delta_x) < input.drag_threshold) {
        if (tv->selection_drag_mode == TimelineDragMode::RULER_NEW) {
          tv->selection_active = false;
        }
      }
    }

    if (tv->selection_drag_mode == TimelineDragMode::RULER_NEW ||
        tv->selection_drag_mode == TimelineDragMode::RULER_START ||
        tv->selection_drag_mode == TimelineDragMode::RULER_END) {
      tv->selection_drag_mode = TimelineDragMode::NONE;
    }
  }

  // Tracks Interaction
  if (input.tracks_hovered && !input.ruler_active) {
    if (tv->selection_drag_mode == TimelineDragMode::NONE) {
      if (tv->selection_active && input.is_mouse_clicked &&
          (proximity.near_start || proximity.near_end)) {
        tv->selection_drag_mode = proximity.near_start ? TimelineDragMode::TRACKS_START
                                                       : TimelineDragMode::TRACKS_END;
      }
    }

    if (tv->selection_drag_mode == TimelineDragMode::TRACKS_START ||
        tv->selection_drag_mode == TimelineDragMode::TRACKS_END) {
      if (!input.is_mouse_down) {
        tv->selection_drag_mode = TimelineDragMode::NONE;
      }
    }

    // Zooming
    if (input.mouse_wheel != 0.0f && input.is_ctrl_down) {
      double mouse_x_rel = (double)(input.mouse_x - tracks_origin_x) / (double)tracks_inner_width;
      double zoom_factor = (input.mouse_wheel > 0.0f) ? 0.8 : TRACE_VIEWER_MAX_ZOOM_FACTOR;
      double new_duration = current_duration * zoom_factor;

      double trace_duration = (double)(tv->viewport.max_ts - tv->viewport.min_ts);
      double max_duration = trace_duration * TRACE_VIEWER_MAX_ZOOM_FACTOR;
      double min_duration = TRACE_VIEWER_MIN_ZOOM_DURATION;

      if (tv->selection_active) {
        double t1 = tv->selection_start_time;
        double t2 = tv->selection_end_time;
        if (t1 > t2) std::swap(t1, t2);
        double sel_dur = t2 - t1;
        if (sel_dur > 0) {
          if (sel_dur > min_duration) min_duration = sel_dur;
          double sel_max_dur = sel_dur * 10.0;
          if (sel_max_dur < max_duration) max_duration = sel_max_dur;
        }
      }

      if (max_duration < min_duration) max_duration = min_duration;
      if (new_duration < min_duration) new_duration = min_duration;
      if (new_duration > max_duration) new_duration = max_duration;

      tv->viewport.start_time = mouse_ts - mouse_x_rel * new_duration;

      if (tv->selection_active) {
        double t1 = tv->selection_start_time;
        double t2 = tv->selection_end_time;
        if (t1 > t2) std::swap(t1, t2);
        if (tv->viewport.start_time > t1) tv->viewport.start_time = t1;
        if (tv->viewport.start_time + new_duration < t2)
          tv->viewport.start_time = t2 - new_duration;
      }
      tv->viewport.end_time = tv->viewport.start_time + new_duration;
      current_duration = new_duration;
    }

    // Panning
    if (input.is_mouse_down && !input.is_mouse_clicked &&
        tv->selection_drag_mode == TimelineDragMode::NONE) {
      double dx = (double)input.mouse_delta_x;
      double dt = (dx / (double)tracks_inner_width) * current_duration;
      tv->viewport.start_time -= dt;

      if (tv->selection_active) {
        double t1 = tv->selection_start_time;
        double t2 = tv->selection_end_time;
        if (t1 > t2) std::swap(t1, t2);
        if (tv->viewport.start_time > t1) tv->viewport.start_time = t1;
        if (tv->viewport.start_time + current_duration < t2)
          tv->viewport.start_time = t2 - current_duration;
      }
      tv->viewport.end_time = tv->viewport.start_time + current_duration;
    }
  }

  // 3. Track Layout and Pass 1: Culling, Naming, Snapping, Hit-testing
  array_list_clear(&tv->hover_matches);
  array_list_resize(&tv->track_infos, allocator, tv->tracks.size);
  tv->total_tracks_height = 0.0f;
  
  float counter_track_height = 3.0f * input.lane_height;
  bool mouse_in_selection = trace_viewer_selection_is_mouse_inside(tv, mouse_ts);
  bool track_list_hovered =
      input.tracks_hovered &&
      mouse_in_selection &&
      tv->selection_drag_mode == TimelineDragMode::NONE &&
      !proximity.near_start && !proximity.near_end;

  bool is_dragging = (tv->selection_drag_mode != TimelineDragMode::NONE);
  bool was_drag = (std::abs(input.drag_delta_x) >= input.drag_threshold ||
                   std::abs(input.drag_delta_y) >= input.drag_threshold);
  bool should_snap = is_dragging && was_drag;

  for (size_t i = 0; i < tv->tracks.size; i++) {
    Track& t = tv->tracks[i];
    TrackViewInfo& vi = tv->track_infos[i];

    vi.height = (t.type == TRACK_TYPE_COUNTER)
                             ? counter_track_height
                             : (float)(t.max_depth + 2) * input.lane_height;
    vi.y = input.canvas_y + input.ruler_height + tv->total_tracks_height - input.tracks_scroll_y;
    tv->total_tracks_height += vi.height;
    
    // Frustum culling
    vi.visible = (vi.y + vi.height >= input.canvas_y + input.ruler_height &&
                  vi.y <= input.canvas_y + input.canvas_height);

    // Format header name
    std::string_view name_str = trace_data_get_string(td, t.name_ref);
    std::string_view id_str = trace_data_get_string(td, t.id_ref);

    if (name_str.size() == 0) {
      if (t.type == TRACK_TYPE_THREAD) {
        snprintf(vi.name, sizeof(vi.name), "Thread %d", t.tid);
      } else {
        snprintf(vi.name, sizeof(vi.name), "Counter");
      }
    } else if (id_str.size() > 0) {
      snprintf(vi.name, sizeof(vi.name), "%.*s (%.*s)",
               (int)name_str.size(), name_str.data(), (int)id_str.size(), id_str.data());
    } else {
      snprintf(vi.name, sizeof(vi.name), "%.*s", (int)name_str.size(), name_str.data());
    }

    if (vi.visible) {
      if (t.type == TRACK_TYPE_THREAD) {
        track_compute_render_blocks(
            &t, td, tv->viewport.start_time, tv->viewport.end_time,
            tracks_inner_width, tracks_origin_x, tv->selected_event_index,
            &tv->track_renderer_state, &tv->render_blocks, allocator);

        for (size_t k = 0; k < tv->render_blocks.size; k++) {
          const TrackRenderBlock& rb = tv->render_blocks[k];
          float y1 = vi.y + (float)(rb.depth + 1) * input.lane_height;
          float y2 = y1 + input.lane_height - 1.0f;

          // Snapping
          if (should_snap) {
            double ts1 = trace_viewer_px_to_ts(tv->viewport.start_time, tv->viewport.end_time,
                                               tracks_inner_width, tracks_origin_x, rb.x1);
            trace_viewer_snapping_suggest(tv, ts1, rb.x1, input.mouse_x, y1, y2);
            double ts2 = trace_viewer_px_to_ts(tv->viewport.start_time, tv->viewport.end_time,
                                               tracks_inner_width, tracks_origin_x, rb.x2);
            trace_viewer_snapping_suggest(tv, ts2, rb.x2, input.mouse_x, y1, y2);
          }

          // Hit-testing
          if (track_list_hovered && input.mouse_y >= y1 && input.mouse_y < y2 &&
              input.mouse_x >= rb.x1 && input.mouse_x < rb.x2) {
            HoverMatch hm = {i, k, y1, y2, rb};
            array_list_push_back(&tv->hover_matches, allocator, hm);
          }
        }
      } else {
        // Counter hit-testing
        track_compute_counter_render_blocks(&t, td, tv->viewport.start_time,
                                            tv->viewport.end_time, tracks_inner_width,
                                            tracks_origin_x, &tv->track_renderer_state,
                                            &tv->counter_render_blocks, allocator);
        
        float track_content_y = vi.y + input.lane_height;
        float track_content_h = vi.height - input.lane_height;

        if (track_list_hovered && input.mouse_y >= track_content_y &&
            input.mouse_y < track_content_y + track_content_h) {
          for (size_t k = 0; k < tv->counter_render_blocks.size; k++) {
            const CounterRenderBlock& rb = tv->counter_render_blocks[k];
            if (input.mouse_x >= rb.x1 && input.mouse_x < rb.x2) {
               HoverMatch hm = {i, k, track_content_y, track_content_y + track_content_h, {}};
               hm.rb.event_idx = rb.event_idx;
               hm.rb.count = (rb.event_idx != (size_t)-1) ? 1 : 0;
               array_list_push_back(&tv->hover_matches, allocator, hm);
               break;
            }
          }
        }
      }
    }
  }

  // 4. Boundary Updates (using pre-calculated snap_best_ts from THIS frame)
  if (input.ruler_active) {
    if (tv->selection_drag_mode == TimelineDragMode::RULER_NEW) {
      if (was_drag) {
        tv->selection_end_time = tv->snap_best_ts;
      }
    } else if (tv->selection_drag_mode == TimelineDragMode::RULER_START) {
      tv->selection_start_time = tv->snap_best_ts;
    } else if (tv->selection_drag_mode == TimelineDragMode::RULER_END) {
      tv->selection_end_time = tv->snap_best_ts;
    }
  } else if (input.tracks_hovered) {
    if (tv->selection_drag_mode == TimelineDragMode::TRACKS_START ||
        tv->selection_drag_mode == TimelineDragMode::TRACKS_END) {
        double ts = (input.is_mouse_clicked) ? mouse_ts : tv->snap_best_ts;
        if (tv->selection_drag_mode == TimelineDragMode::TRACKS_START) {
          tv->selection_start_time = ts;
        } else {
          tv->selection_end_time = ts;
        }
    }
  }

  // 5. Selection (Click to select event)
  if (input.is_mouse_released && !was_drag) {
    if (tv->hover_matches.size > 0) {
      const HoverMatch& hm = tv->hover_matches[tv->hover_matches.size - 1];
      if (hm.rb.event_idx != (size_t)-1) {
        tv->selected_event_index = (int64_t)hm.rb.event_idx;
        tv->show_details_panel = true;
      }
    } else if (input.tracks_hovered) {
      if (mouse_in_selection && !proximity.near_start && !proximity.near_end) {
        tv->selected_event_index = -1;
      } else if (tv->selection_active && !mouse_in_selection) {
        tv->selection_active = false;
      }
    }
  }

  // 6. Compute Layout for Drawing
  // Selection Layout
  tv->selection_layout.active = tv->selection_active;
  if (tv->selection_active) {
    double t1 = tv->selection_start_time;
    double t2 = tv->selection_end_time;
    if (t1 > t2) std::swap(t1, t2);

    tv->selection_layout.x1 = (float)(tracks_origin_x + (t1 - tv->viewport.start_time) / current_duration * tracks_inner_width);
    tv->selection_layout.x2 = (float)(tracks_origin_x + (t2 - tv->viewport.start_time) / current_duration * tracks_inner_width);

    format_duration(tv->selection_layout.duration_label, sizeof(tv->selection_layout.duration_label), t2 - t1, t2 - t1);
  }

  // Ruler Ticks
  array_list_clear(&tv->ruler_ticks);
  double tick_interval = calculate_tick_interval(current_duration, tracks_inner_width, 100.0);
  double display_start = tv->viewport.start_time - (double)tv->viewport.min_ts;
  double display_end = tv->viewport.end_time - (double)tv->viewport.min_ts;
  double first_tick_rel = ceil(display_start / tick_interval) * tick_interval;
  for (double t_rel = first_tick_rel; t_rel <= display_end; t_rel += tick_interval) {
    double t = t_rel + (double)tv->viewport.min_ts;
    float x = (float)(tracks_origin_x + (t - tv->viewport.start_time) / current_duration * tracks_inner_width);
    if (x < tracks_origin_x || x > tracks_origin_x + tracks_inner_width) continue;

    RulerTick tick;
    tick.x = x;
    tick.ts_rel = t_rel;
    format_duration(tick.label, sizeof(tick.label), t_rel, tick_interval);
    array_list_push_back(&tv->ruler_ticks, allocator, tick);
  }

  tv->last_best_snap_ts = tv->snap_best_ts;
}

void trace_viewer_draw(TraceViewer* tv, TraceData* td, Allocator allocator,
                       const Theme* theme_ptr) {
  const Theme& theme = *theme_ptr;

  if (td->events.size > 0) {
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();

    TraceViewerInput input = {};
    input.canvas_x = canvas_pos.x;
    input.canvas_y = canvas_pos.y;
    input.canvas_width = canvas_size.x;
    input.canvas_height = canvas_size.y;
    input.ruler_height = ImGui::GetFrameHeight();
    input.lane_height = ImGui::GetFrameHeight();

    input.mouse_x = ImGui::GetMousePos().x;
    input.mouse_y = ImGui::GetMousePos().y;
    input.mouse_wheel = ImGui::GetIO().MouseWheel;
    input.mouse_wheel_h = ImGui::GetIO().MouseWheelH;
    input.click_x = ImGui::GetIO().MouseClickedPos[0].x;
    input.click_y = ImGui::GetIO().MouseClickedPos[0].y;
    input.is_mouse_down = ImGui::IsMouseDown(0);
    input.is_mouse_clicked = ImGui::IsMouseClicked(0);
    input.is_mouse_released = ImGui::IsMouseReleased(0);
    input.mouse_delta_x = ImGui::GetIO().MouseDelta.x;
    input.mouse_delta_y = ImGui::GetIO().MouseDelta.y;
    input.drag_delta_x = ImGui::GetMouseDragDelta(0).x;
    input.drag_delta_y = ImGui::GetMouseDragDelta(0).y;
    input.drag_threshold = ImGui::GetIO().MouseDragThreshold;
    input.is_ctrl_down = ImGui::IsKeyDown(ImGuiMod_Ctrl);
    input.is_shift_down = ImGui::IsKeyDown(ImGuiMod_Shift);

    float tracks_origin_x = tv->last_tracks_x > 0 ? tv->last_tracks_x : canvas_pos.x;
    float tracks_inner_width = tv->last_inner_width > 0 ? tv->last_inner_width : canvas_size.x;
    if (tracks_inner_width <= 0) tracks_inner_width = 1.0f;

    // Pre-pass to capture interaction state
    ImGui::SetCursorScreenPos(ImVec2(tracks_origin_x, canvas_pos.y));
    ImGui::InvisibleButton("##Ruler", ImVec2(tracks_inner_width, input.ruler_height));
    input.ruler_active = ImGui::IsItemActive();
    input.ruler_activated = ImGui::IsItemActivated();
    input.ruler_deactivated = ImGui::IsItemDeactivated();

    ImGuiWindowFlags child_flags = ImGuiWindowFlags_NoMove;
    if (input.is_ctrl_down)
      child_flags |= ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::SetCursorScreenPos(ImVec2(canvas_pos.x, canvas_pos.y + input.ruler_height));
    if (ImGui::BeginChild("TrackList", ImVec2(0, canvas_size.y - input.ruler_height),
                          ImGuiChildFlags_None, child_flags)) {
      input.tracks_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
      input.tracks_scroll_y = ImGui::GetScrollY();
    }
    ImGui::EndChild();

    trace_viewer_step(tv, td, input, allocator);

    // --- Drawing Phase ---
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(
        canvas_pos,
        ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
        theme.viewport_bg);

    double current_mouse_ts = trace_viewer_px_to_ts(
        tv->viewport.start_time, tv->viewport.end_time, tracks_inner_width,
        tracks_origin_x, input.mouse_x);
    SelectionProximity proximity = trace_viewer_selection_check_proximity(
        tv, current_mouse_ts, (5.0f / tracks_inner_width) *
                                  (tv->viewport.end_time - tv->viewport.start_time));

    if ((tv->selection_drag_mode != TimelineDragMode::NONE &&
         tv->selection_drag_mode != TimelineDragMode::RULER_NEW) ||
        proximity.near_start || proximity.near_end) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }

    ImGui::SetCursorScreenPos(ImVec2(canvas_pos.x, canvas_pos.y + input.ruler_height));
    if (ImGui::BeginChild("TrackList", ImVec2(0, canvas_size.y - input.ruler_height),
                          ImGuiChildFlags_None, child_flags)) {
      // Handle vertical scroll if dragging tracks
      if (ImGui::IsWindowHovered() &&
          ImGui::IsMouseDragging(ImGuiMouseButton_Left) &&
          tv->selection_drag_mode == TimelineDragMode::NONE) {
        ImGui::SetScrollY(ImGui::GetScrollY() - ImGui::GetIO().MouseDelta.y);
      }

      ImDrawList* track_draw_list = ImGui::GetWindowDrawList();

      ImGui::Dummy(ImVec2(0.0f, tv->total_tracks_height));
      ImGui::SetCursorPos(ImVec2(0, 0));

      ImVec2 tracks_canvas_pos = ImGui::GetCursorScreenPos();
      float inner_width = ImGui::GetContentRegionAvail().x;
      tv->last_inner_width = inner_width;
      tv->last_tracks_x = tracks_canvas_pos.x;

      bool sel_found = false;
      float sel_x1 = 0, sel_x2 = 0, sel_y1 = 0, sel_y2 = 0;
      ImU32 sel_col = 0;
      StringRef sel_name_ref = 0;

      for (size_t i = 0; i < tv->tracks.size; i++) {
        const Track& t = tv->tracks[i];
        const TrackViewInfo& vi = tv->track_infos[i];

        if (!vi.visible) continue;

        ImVec2 track_pos(tracks_canvas_pos.x, vi.y);

        track_draw_list->AddRectFilled(
            track_pos,
            ImVec2(track_pos.x + inner_width, track_pos.y + vi.height),
            theme.track_bg);

        // Render track header
        track_draw_list->AddRectFilled(
            track_pos,
            ImVec2(track_pos.x + inner_width, track_pos.y + input.lane_height),
            theme.track_header_bg);
        track_draw_list->AddLine(
            ImVec2(track_pos.x, track_pos.y + input.lane_height - 1),
            ImVec2(track_pos.x + inner_width, track_pos.y + input.lane_height - 1),
            theme.track_separator);

        // Sticky header text
        float sticky_x = std::max(track_pos.x, tracks_canvas_pos.x);
        float font_size = ImGui::GetFontSize();
        ImVec2 text_pos = ImVec2(
            sticky_x + 5, track_pos.y + (input.lane_height - font_size) * 0.5f);
        
        size_t display_name_len = strlen(vi.name);
        track_draw_list->AddText(ImGui::GetFont(), font_size, text_pos,
                                 theme.track_text, vi.name,
                                 vi.name + display_name_len);

        ImVec2 text_size = ImGui::GetFont()->CalcTextSizeA(
            font_size, FLT_MAX, 0.0f, vi.name,
            vi.name + display_name_len);
        
        // Tooltip for header
        if (trace_viewer_selection_is_mouse_inside(tv, 
                trace_viewer_px_to_ts(tv->viewport.start_time, tv->viewport.end_time,
                                      inner_width, tracks_canvas_pos.x, input.mouse_x)) &&
            ImGui::IsMouseHoveringRect(
                text_pos, ImVec2(text_pos.x + text_size.x,
                                 text_pos.y + text_size.y))) {
          ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                              ImVec2(10.0f, 10.0f));
          ImGui::BeginTooltip();
          ImGui::Text("PID: %d", t.pid);
          if (t.type == TRACK_TYPE_THREAD) {
            ImGui::Text("TID: %d", t.tid);
          }
          ImGui::EndTooltip();
          ImGui::PopStyleVar();
        }

        if (t.type == TRACK_TYPE_THREAD) {
          track_compute_render_blocks(
              &t, td, tv->viewport.start_time, tv->viewport.end_time,
              inner_width, tracks_canvas_pos.x, tv->selected_event_index,
              &tv->track_renderer_state, &tv->render_blocks, allocator);

          for (size_t k = 0; k < tv->render_blocks.size; k++) {
            const TrackRenderBlock& rb = tv->render_blocks[k];
            float y1 = track_pos.y + (float)(rb.depth + 1) * input.lane_height;
            float y2 = y1 + input.lane_height - 1.0f;

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
          bool mouse_in_sel = trace_viewer_selection_is_mouse_inside(
              tv, trace_viewer_px_to_ts(tv->viewport.start_time,
                                        tv->viewport.end_time, inner_width,
                                        tracks_canvas_pos.x, input.mouse_x));
          trace_viewer_draw_counter_track(
              tv, td, track_draw_list, t,
              ImVec2(track_pos.x, track_pos.y + input.lane_height), inner_width,
              vi.height - input.lane_height, tv->viewport.start_time,
              tv->viewport.end_time, theme,
              ImVec2(input.mouse_x, input.mouse_y), mouse_in_sel, allocator);
        }
      }

      // Handle hover highlighting and tooltip
      if (tv->hover_matches.size > 0) {
        const HoverMatch* best_hm =
            &tv->hover_matches[tv->hover_matches.size - 1];
        const TrackRenderBlock& rb = best_hm->rb;

        // Re-draw hovered block with highlight (only for threads, counters do it themselves)
        if (tv->tracks[best_hm->track_idx].type == TRACK_TYPE_THREAD) {
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
        }

        // Show tooltip
        trace_viewer_draw_tooltip(tv, td, best_hm, inner_width, tracks_canvas_pos.x);
      }

      if (sel_found) {
        trace_viewer_draw_event(tv, td, track_draw_list, sel_x1, sel_x2, sel_y1,
                                sel_y2, sel_col, true, sel_name_ref,
                                inner_width, tracks_canvas_pos.x, theme);
      }

      trace_viewer_draw_selection_overlay(
          tv, track_draw_list, ImVec2(tracks_canvas_pos.x, ImGui::GetWindowPos().y),
          ImVec2(inner_width, ImGui::GetWindowSize().y), theme, true);

      if (tv->snap_has_snap &&
          tv->selection_drag_mode != TimelineDragMode::NONE) {
        track_draw_list->AddLine(ImVec2(tv->snap_px, tv->snap_y1),
                                 ImVec2(tv->snap_px, tv->snap_y2),
                                 IM_COL32(255, 0, 0, 255), 3.0f);
      }
    }
    ImGui::EndChild();

    trace_viewer_draw_time_ruler(
        tv, draw_list, ImVec2(tracks_origin_x, canvas_pos.y),
        ImVec2(tracks_inner_width, input.ruler_height), canvas_pos.x,
        canvas_size.x, theme);
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
        std::string_view id = trace_data_get_string(td, e.id_ref);

        if (ph != "C") {
          ImGui::Text("Name: %.*s", (int)name.size(), name.data());
        }
        if (id.size() > 0) {
          ImGui::Text("ID: %.*s", (int)id.size(), id.data());
        }
        if (cat.size() > 0) {
          ImGui::Text("Category: %.*s", (int)cat.size(), cat.data());
        }

        char ts_buf[32];
        format_duration(ts_buf, sizeof(ts_buf),
                        (double)e.ts - (double)tv->viewport.min_ts);
        ImGui::Text("Start: %s", ts_buf);

        if (e.dur > 0) {
          char dur_buf[32];
          format_duration(dur_buf, sizeof(dur_buf), (double)e.dur);
          ImGui::Text("Duration: %s", dur_buf);
        }
        ImGui::Text("PID: %d, TID: %d", e.pid, e.tid);

        trace_viewer_draw_args_table(td, e, nullptr, 0);
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
