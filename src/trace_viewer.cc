#include "src/trace_viewer.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string>

#include "src/colors.h"
#include "src/format.h"
#include "src/logging.h"
#include "src/platform.h"
#include "third_party/imgui/imgui.h"
#include "third_party/imgui/imgui_internal.h"

static void trace_viewer_draw_selection_overlay(
    TraceViewer* tv, ImDrawList* draw_list, ImVec2 pos, ImVec2 size,
    const Theme& theme, bool draw_duration_text);

static void trace_viewer_draw_search_section(TraceViewer* tv, Allocator allocator);
void trace_viewer_search_job(void* user_data);

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
  array_list_deinit(&tv->selected_event_indices, allocator);
  array_list_deinit(&tv->filtered_event_indices, allocator);
  array_list_deinit(&tv->search_query, allocator);
  tv->search.jobs_should_abort.store(true);
  {
    std::lock_guard<std::mutex> lock(tv->search.mutex);
    tv->search.quit_cv.notify_all();
  }
  if (tv->search.is_searching.load()) {
    std::unique_lock<std::mutex> lock(tv->search.quit_mutex);
    tv->search.quit_cv.wait(lock, [tv] { return !tv->search.is_searching.load(); });
  }
  array_list_deinit(&tv->search.pending_query, allocator);
  array_list_deinit(&tv->search.pending_results, allocator);
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
    float text_y = pos.y + size.y / 3.0f - text_size.y * 0.5f;

    // Ensure text is visible even if partially off-screen
    text_x = std::max(
        pos.x + 5.0f, std::min(text_x, pos.x + size.x - text_size.x - 5.0f));

    // Draw background and border for the text
    float padding_x = 4.0f;
    float padding_y = 2.0f;
    ImVec2 bg_min(text_x - padding_x, text_y - padding_y);
    ImVec2 bg_max(text_x + text_size.x + padding_x, text_y + text_size.y + padding_y);
    
    draw_list->AddRectFilled(bg_min, bg_max, theme.timeline_selection_text_bg, 4.0f);
    draw_list->AddRect(bg_min, bg_max, theme.timeline_selection_line, 4.0f, 0, 1.0f);

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
                                    bool is_selected, bool is_focused,
                                    StringRef name_ref, float inner_width,
                                    float tracks_canvas_pos_x,
                                    const Theme& theme) {
  (void)tv;
  float lane_height = y2 - y1 + 1.0f;

  float border_thickness = 1.0f;
  float event_width = x2 - x1;
  // Use a small epsilon to prevent floating point jitter from flipping the
  // border on/off during panning.
  bool draw_border = (event_width > TRACK_MIN_EVENT_WIDTH + 0.01f);

  draw_list->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2), col);

  if (is_focused || is_selected || draw_border) {
    ImU32 border_col = theme.event_border;
    if (is_focused) {
      border_col = theme.event_border_focused;
      border_thickness = 3.0f;
    } else if (is_selected) {
      border_col = theme.event_border_selected;
    }

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
          ImU32 text_col = theme.event_text;
          if (is_focused) {
            text_col = theme.event_text_focused;
          } else if (is_selected) {
            text_col = theme.event_text_selected;
          }
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

static void trace_viewer_draw_event_properties(const TraceData* td,
                                               const TraceEventPersisted& e,
                                               double viewport_min_ts,
                                               bool show_copy_buttons,
                                               const Track* t = nullptr) {
  std::string_view name = trace_data_get_string(td, e.name_ref);
  std::string_view cat = trace_data_get_string(td, e.cat_ref);
  std::string_view ph = trace_data_get_string(td, e.ph_ref);

  ImGui::Spacing();

  ImGuiTableFlags table_flags = ImGuiTableFlags_NoSavedSettings;
  if (!show_copy_buttons) {
    table_flags |= ImGuiTableFlags_SizingFixedFit;
  }

  if (ImGui::BeginTable("##focused_event_table_unified", 3, table_flags)) {
    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed);
    if (show_copy_buttons) {
      ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
    } else {
      ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed);
    }
    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed);

    auto add_row = [show_copy_buttons](const char* field_label, std::string_view value_str, const char* btn_id_suffix, uint32_t color = 0, bool has_color = false) {
      ImGui::TableNextRow();

      // Column 0: Label
      ImGui::TableNextColumn();
      if (has_color) {
        ImVec2 p_col = ImGui::GetCursorScreenPos();
        float sz_col = ImGui::GetTextLineHeight() * 0.7f;
        float offset_col = (ImGui::GetTextLineHeight() - sz_col) * 0.5f;
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(p_col.x, p_col.y + offset_col), ImVec2(p_col.x + sz_col, p_col.y + offset_col + sz_col),
            color, 2.0f);

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + sz_col + 4.0f);
      }
      ImGui::TextDisabled("%s", field_label);

      // Column 1: Value
      ImGui::TableNextColumn();
      if (show_copy_buttons) {
        ImGui::TextWrapped("%.*s", (int)value_str.size(), value_str.data());
      } else {
        ImGui::TextUnformatted(value_str.data(), value_str.data() + value_str.size());
      }

      // Column 2: Action
      ImGui::TableNextColumn();
      if (show_copy_buttons && btn_id_suffix) {
        char btn_label[64];
        snprintf(btn_label, sizeof(btn_label), "Copy##%s", btn_id_suffix);
        if (ImGui::SmallButton(btn_label)) {
          std::string text(value_str);
          ImGui::SetClipboardText(text.c_str());
        }
      }
    };

    // 1. Header Row / Event Name
    if (t != nullptr && t->type == TRACK_TYPE_COUNTER) {
      std::string_view t_name = trace_data_get_string(td, t->name_ref);
      add_row("Name", t_name, "Name");
    } else {
      if (ph != "C") {
        add_row("Name", name, "Name");
      } else {
        add_row("Name", "(Counter Event)", "Name");
      }
    }

    if (cat.size() > 0) {
      add_row("Category", cat, "Category");
    }

    char ts_buf[32];
    format_duration(ts_buf, sizeof(ts_buf), (double)e.ts - viewport_min_ts);
    add_row("Start", std::string_view(ts_buf), nullptr);

    if (e.dur > 0) {
      char dur_buf[32];
      format_duration(dur_buf, sizeof(dur_buf), (double)e.dur);
      add_row("Duration", std::string_view(dur_buf), nullptr);
    }

    if (t == nullptr || t->type == TRACK_TYPE_THREAD) {
      char pid_tid_buf[64];
      snprintf(pid_tid_buf, sizeof(pid_tid_buf), "%d / %d", e.pid, e.tid);
      add_row("PID / TID", std::string_view(pid_tid_buf), nullptr);
    }

    size_t skip_count = 0;
    const StringRef* skip_keys = nullptr;

    if (t != nullptr && t->type == TRACK_TYPE_COUNTER) {
      bool single_series_redundant = false;
      std::string_view t_name = trace_data_get_string(td, t->name_ref);

      if (t->counter_series.size == 1) {
        std::string_view s_name = trace_data_get_string(td, t->counter_series[0]);
        if (s_name == t_name || s_name == "" || s_name == "value" || s_name == "Value") {
          single_series_redundant = true;
        }
      }

      if (single_series_redundant) {
        double val = 0.0;
        StringRef val_s_ref = 0;
        for (uint32_t arg_k = 0; arg_k < e.args_count; arg_k++) {
          const TraceArgPersisted& arg = td->args[e.args_offset + arg_k];
          if (arg.key_ref == t->counter_series[0]) {
            val = arg.val_double;
            val_s_ref = arg.val_ref;
            break;
          }
        }
        
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Value");

        ImGui::TableNextColumn();
        if (val_s_ref != 0) {
          std::string_view val_s = trace_data_get_string(td, val_s_ref);
          if (show_copy_buttons) {
            ImGui::TextWrapped("%.*s", (int)val_s.size(), val_s.data());
          } else {
            ImGui::TextUnformatted(val_s.data(), val_s.data() + val_s.size());
          }
        } else {
          ImGui::Text("%.2f", val);
        }

        ImGui::TableNextColumn();
        // No Action column
      } else {
        double total = 0.0;
        bool has_total_series = false;

        for (size_t s_i = 0; s_i < t->counter_series.size; s_i++) {
          size_t s_idx = t->counter_series.size - 1 - s_i;
          StringRef key_ref = t->counter_series[s_idx];

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

          char series_val_buf[64];
          if (val_s_ref != 0) {
            std::string_view val_s = trace_data_get_string(td, val_s_ref);
            snprintf(series_val_buf, sizeof(series_val_buf), "%.*s", (int)val_s.size(), val_s.data());
          } else {
            snprintf(series_val_buf, sizeof(series_val_buf), "%.2f", val);
          }

          add_row(s_name.data(), std::string_view(series_val_buf), nullptr, t->counter_colors[s_idx], true);

          total += val;
        }

        if (t->counter_series.size > 1 && !has_total_series) {
          char total_buf[64];
          snprintf(total_buf, sizeof(total_buf), "%.2f", total);
          add_row("Total", std::string_view(total_buf), nullptr);
        }
      }

      skip_keys = t->counter_series.data;
      skip_count = t->counter_series.size;
    }

    // Event arguments
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

      std::string_view key = trace_data_get_string(td, arg.key_ref);

      char val_buf[256];
      bool is_str = (arg.val_ref != 0);
      
      if (is_str) {
        std::string_view val = trace_data_get_string(td, arg.val_ref);
        add_row(key.data(), val, std::string(key).c_str());
      } else {
        snprintf(val_buf, sizeof(val_buf), "%.2f", arg.val_double);
        add_row(key.data(), std::string_view(val_buf), nullptr);
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
      trace_viewer_draw_event_properties(td, e, (double)tv->viewport.min_ts, false, &t);
    } else {
      trace_viewer_draw_event_properties(td, e, (double)tv->viewport.min_ts, false, &t);
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
    double viewport_end, const Theme& theme, ImVec2 mouse_pos,
    bool track_list_hovered, int64_t focused_event_idx, Allocator allocator) {
  if (t.event_indices.size == 0) return;

  double duration = viewport_end - viewport_start;
  if (duration <= 0) return;

  double max_total = t.counter_max_total;
  if (max_total <= 0) max_total = 1.0;

  TrackRendererState* state = &tv->track_renderer_state;
  array_list_resize(&state->counter_current_values, allocator,
                    t.counter_series.size);

  track_compute_counter_render_blocks(
      &t, td, viewport_start, viewport_end, width, pos.x, focused_event_idx,
      state, &tv->counter_render_blocks, allocator);

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

    if (rb.is_focused) {
      draw_list->AddRectFilled(ImVec2(rb.x1, pos.y),
                               ImVec2(rb.x2, pos.y + height),
                               theme.event_focused_bg);
    }
  }

  // Pass 2: Step lines (no anti-aliasing for sharp lines)
  ImDrawListFlags old_flags = draw_list->Flags;
  draw_list->Flags &= ~ImDrawListFlags_AntiAliasedLines;

  for (size_t s_idx = 0; s_idx < n_series; s_idx++) {
    float prev_y_top = -1.0f;
    for (size_t i = 0; i < n_blocks; i++) {
      const CounterRenderBlock& rb = tv->counter_render_blocks[i];

      float off_top = state->counter_visual_offsets[i * (n_series + 1) + s_idx + 1];
      float y_top = pos.y + height - off_top;

      ImU32 line_col = theme.event_border;
      float thickness = 1.0f;
      if (rb.is_focused) {
        line_col = theme.event_border_focused;
        thickness = 3.0f;
      } else if (rb.is_selected) {
        line_col = theme.event_border_selected;
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

static void trace_viewer_box_select_update(TraceViewer* tv, TraceData* td, Allocator allocator) {
  float x1 = tv->box_select_start.x;
  float x2 = tv->box_select_end.x;
  float y1 = tv->box_select_start.y;
  float y2 = tv->box_select_end.y;
  if (x1 > x2) std::swap(x1, x2);
  if (y1 > y2) std::swap(y1, y2);

  array_list_clear(&tv->selected_event_indices);

  double ts1 = trace_viewer_px_to_ts(
      tv->viewport.start_time, tv->viewport.end_time, tv->last_inner_width,
      tv->last_tracks_x, x1);
  double ts2 = trace_viewer_px_to_ts(
      tv->viewport.start_time, tv->viewport.end_time, tv->last_inner_width,
      tv->last_tracks_x, x2);

  for (size_t i = 0; i < tv->tracks.size; i++) {
    const Track& t = tv->tracks[i];
    const TrackViewInfo& vi = tv->track_infos[i];
    if (!vi.visible) continue;

    // Check track Y overlap
    if (vi.y + vi.height < y1 || vi.y > y2) continue;

    if (t.type == TRACK_TYPE_THREAD) {
      auto it_start = std::lower_bound(
          t.event_indices.data, t.event_indices.data + t.event_indices.size,
          (int64_t)ts1 - t.max_dur, [&](size_t idx, int64_t val) {
            return td->events[idx].ts < val;
          });

      for (const size_t* it = it_start;
           it < t.event_indices.data + t.event_indices.size; ++it) {
        const TraceEventPersisted& e = td->events[*it];
        if (e.ts > (int64_t)ts2) break;

        // Explicitly check for time overlap since it_start is conservative
        if (e.ts + e.dur < (int64_t)ts1) continue;

        size_t event_idx_in_track = (size_t)(it - t.event_indices.data);
        uint32_t depth = t.depths[event_idx_in_track];
        float event_y1 = vi.y + (float)(depth + 1) * tv->last_lane_height;
        float event_y2 = event_y1 + tv->last_lane_height;

        if (event_y2 < y1 || event_y1 > y2) continue;

        array_list_push_back(&tv->selected_event_indices, allocator,
                             (int64_t)*it);
      }
    } else {
      // Counter track
      if (t.event_indices.size > 0) {
        // Only select if the box intersects the actual chart area (below header)
        float chart_y1 = vi.y + tv->last_lane_height;
        float chart_y2 = vi.y + vi.height;
        if (!(chart_y2 < y1 || chart_y1 > y2)) {
          auto it_start = std::lower_bound(
              t.event_indices.data, t.event_indices.data + t.event_indices.size,
              (int64_t)ts1, [&](size_t idx, int64_t val) {
                return td->events[idx].ts < val;
              });
          for (const size_t* it = it_start;
               it < t.event_indices.data + t.event_indices.size; ++it) {
            if (td->events[*it].ts > (int64_t)ts2) break;
            array_list_push_back(&tv->selected_event_indices, allocator,
                                 (int64_t)*it);
          }
        }
      }
    }
  }

  // Sort for binary search
  if (tv->selected_event_indices.size > 0) {
    std::sort(tv->selected_event_indices.data,
              tv->selected_event_indices.data + tv->selected_event_indices.size);
  }

  LOG_INFO("SYNC BOX SELECT COMPLETED! track size = %zu, found %zu selected event indices",
           tv->tracks.size, tv->selected_event_indices.size);

  tv->selected_histogram_bucket = -1;

  array_list_clear(&tv->filtered_event_indices);
  array_list_append(&tv->filtered_event_indices, allocator, tv->selected_event_indices.data, tv->selected_event_indices.size);

  {
    std::lock_guard<std::mutex> lock(tv->search.mutex);
    array_list_clear(&tv->search.pending_results);
    array_list_append(&tv->search.pending_results, tv->search.allocator, tv->selected_event_indices.data, tv->selected_event_indices.size);
    tv->search.new_box_selection_available.store(true);
    tv->search.results_ready.store(false);
  }

  tv->selected_events_dirty = true;
  platform_submit_job(trace_viewer_search_job, &tv->search);
}

static void trace_viewer_zoom_to_event(TraceViewer* tv,
                                        const TraceEventPersisted& e) {
  double event_start = (double)e.ts;
  double event_end = (double)(e.ts + e.dur);

  if (e.dur > 0) {
    // Zoom to event with 5% padding
    double event_dur = event_end - event_start;
    double padding = event_dur * 0.05;
    double target_dur = event_dur + padding * 2.0;
    if (target_dur < TRACE_VIEWER_MIN_ZOOM_DURATION) {
      target_dur = TRACE_VIEWER_MIN_ZOOM_DURATION;
      padding = (target_dur - event_dur) * 0.5;
    }

    tv->viewport.start_time = event_start - padding;
    tv->viewport.end_time = event_start - padding + target_dur;

    // Selection
    tv->selection_active = true;
    tv->selection_start_time = event_start;
    tv->selection_end_time = event_end;
  } else {
    // Center viewport on event without changing zoom
    double current_dur = tv->viewport.end_time - tv->viewport.start_time;
    tv->viewport.start_time = event_start - current_dur * 0.5;
    tv->viewport.end_time = tv->viewport.start_time + current_dur;
    tv->selection_active = false;
  }

  tv->selection_drag_mode = InteractionDragMode::NONE;
  tv->request_scroll_to_focused_event = true;
}

void trace_viewer_calculate_histogram(const ArrayList<int64_t>& results, const TraceData* td, DurationHistogram* h) {
  h->num_buckets = 0;
  h->max_bucket_count = 0;
  h->total_count = (uint32_t)results.size;
  h->has_non_zero_durations = false;

  if (results.size > 0) {
    int64_t min_dur = -1;
    int64_t max_dur = -1;
    uint32_t zero_count = 0;

    for (size_t i = 0; i < results.size; i++) {
      size_t idx = (size_t)results.data[i];
      if (idx >= td->events.size) continue;
      const TraceEventPersisted& e = td->events[idx];
      int64_t d = e.dur;

      if (d <= 0) {
        zero_count++;
      } else {
        if (min_dur == -1 || d < min_dur) min_dur = d;
        if (max_dur == -1 || d > max_dur) max_dur = d;
      }
    }

    int k_bins = 20;

    if (zero_count > 0) {
      h->buckets[0].min_dur = 0;
      h->buckets[0].max_dur = 0;
      h->buckets[0].count = zero_count;
      h->num_buckets = 1;
      h->max_bucket_count = zero_count;
    }

    if (min_dur != -1) {
      h->has_non_zero_durations = true;

      bool is_logarithmic = false;
      if (min_dur > 0 && max_dur > 0 && (double)max_dur / (double)min_dur > 100.0) {
        is_logarithmic = true;
      }

      int64_t non_zero_range = max_dur - min_dur;
      if (non_zero_range < k_bins) {
        k_bins = (int)non_zero_range + 1;
        is_logarithmic = false;
      }

      double log_min = 0.0;
      double log_max = 0.0;
      double log_width = 0.0;

      if (is_logarithmic) {
        log_min = log10((double)min_dur);
        log_max = log10((double)max_dur);
        log_width = (log_max - log_min) / k_bins;
      }

      int start_bin_idx = h->num_buckets;
      h->num_buckets += k_bins;

      int64_t L[36];
      for (int j = 0; j <= k_bins; j++) {
        if (j == k_bins) {
          L[j] = max_dur + 1;
        } else if (is_logarithmic) {
          double ld = log_min + j * log_width;
          L[j] = (int64_t)round(pow(10.0, ld));
        } else {
          L[j] = min_dur + (int64_t)((non_zero_range * j) / k_bins);
        }

        if (j > 0 && L[j] <= L[j-1]) {
          L[j] = L[j-1] + 1;
        }
      }

      for (int j = 0; j < k_bins; j++) {
        DurationHistogramBucket& b = h->buckets[start_bin_idx + j];
        b.min_dur = L[j];
        b.max_dur = L[j+1] - 1;
        b.count = 0;
      }

      for (size_t i = 0; i < results.size; i++) {
        size_t idx = (size_t)results.data[i];
        if (idx >= td->events.size) continue;
        const TraceEventPersisted& e = td->events[idx];
        int64_t d = e.dur;

        if (d <= 0) continue;

        for (int b_idx = start_bin_idx; b_idx < h->num_buckets; b_idx++) {
          DurationHistogramBucket& b = h->buckets[b_idx];
          if (d >= b.min_dur && d <= b.max_dur) {
            b.count++;
            if (b.count > h->max_bucket_count) {
              h->max_bucket_count = b.count;
            }
            break;
          }
        }
      }
    }
  }
}

void trace_viewer_step(TraceViewer* tv, TraceData* td,
                       const TraceViewerInput& input, Allocator allocator) {
  // 0. Handle focus requests
  if (tv->target_focused_event_idx != -1) {
    size_t event_idx = (size_t)tv->target_focused_event_idx;
    if (event_idx < td->events.size) {
      const TraceEventPersisted& e = td->events[event_idx];
      tv->focused_event_idx = (int64_t)event_idx;
      tv->show_details_panel = true;
      trace_viewer_zoom_to_event(tv, e);
    }
    tv->target_focused_event_idx = -1;
  }

  double current_duration = tv->viewport.end_time - tv->viewport.start_time;
  if (current_duration <= 0) current_duration = 1.0;

  float tracks_origin_x = input.canvas_x;
  float tracks_origin_y = input.canvas_y + input.ruler_height;
  float tracks_inner_width = input.canvas_width;
  float tracks_inner_height = input.canvas_height - input.ruler_height;
  if (tracks_inner_width <= 0) tracks_inner_width = 1.0f;
  tv->last_tracks_x = tracks_origin_x;
  tv->last_tracks_y = tracks_origin_y;
  tv->last_inner_width = tracks_inner_width;
  tv->last_inner_height = tracks_inner_height;
  tv->last_lane_height = input.lane_height;

  double mouse_ts = trace_viewer_px_to_ts(tv->viewport.start_time, tv->viewport.end_time,
                                          tracks_inner_width, tracks_origin_x, input.mouse_x);

  bool mouse_in_tracks_content =
      input.mouse_x >= tracks_origin_x &&
      input.mouse_x < tracks_origin_x + tracks_inner_width &&
      input.mouse_y >= tracks_origin_y &&
      input.mouse_y < tracks_origin_y + tracks_inner_height;

  // 1. Snapping Initialization
  float snap_threshold_px = 5.0f;
  trace_viewer_snapping_reset(tv, mouse_ts, snap_threshold_px);

  // 2. Interaction Logic - Pre-pass
  double threshold_ts =
      ((double)tv->snap_threshold_px / (double)tracks_inner_width) *
      current_duration;

  SelectionProximity proximity =
      trace_viewer_selection_check_proximity(tv, mouse_ts, threshold_ts);

  bool mouse_in_selection = trace_viewer_selection_is_mouse_inside(tv, mouse_ts);
  bool interaction_ignored = tv->ignore_next_release;

  // Ruler Interaction
  if (input.ruler_active) {
    if (!interaction_ignored && input.ruler_activated) {
      if (proximity.near_start) {
        tv->selection_drag_mode = InteractionDragMode::RULER_START;
      } else if (proximity.near_end) {
        tv->selection_drag_mode = InteractionDragMode::RULER_END;
      } else {
        tv->selection_drag_mode = InteractionDragMode::RULER_NEW;
      }
    }

    if (tv->selection_drag_mode == InteractionDragMode::RULER_NEW) {
      if (std::abs(input.drag_delta_x) >= input.drag_threshold) {
        tv->selection_active = true;
        tv->selection_start_time = trace_viewer_px_to_ts(
            tv->viewport.start_time, tv->viewport.end_time, tracks_inner_width,
            tracks_origin_x, input.click_x);
      }
    }
  } else {
    if (!interaction_ignored && input.ruler_deactivated) {
      if (std::abs(input.drag_delta_x) < input.drag_threshold) {
        if (tv->selection_drag_mode == InteractionDragMode::RULER_NEW) {
          tv->selection_active = false;
        }
      }
    }

    if (tv->selection_drag_mode == InteractionDragMode::RULER_NEW ||
        tv->selection_drag_mode == InteractionDragMode::RULER_START ||
        tv->selection_drag_mode == InteractionDragMode::RULER_END) {
      tv->selection_drag_mode = InteractionDragMode::NONE;
    }
  }

  // Tracks Interaction
  if (input.tracks_hovered && !input.ruler_active) {
    if (!interaction_ignored && tv->selection_drag_mode == InteractionDragMode::NONE) {
      if (input.is_shift_down && input.is_mouse_clicked) {
        tv->selection_drag_mode = InteractionDragMode::BOX_SELECT;
        tv->box_select_start = ImVec2(input.mouse_x, input.mouse_y);
        tv->box_select_end = tv->box_select_start;
      } else if (tv->selection_active && input.is_mouse_clicked &&
          (proximity.near_start || proximity.near_end)) {
        tv->selection_drag_mode = proximity.near_start ? InteractionDragMode::TRACKS_START
                                                       : InteractionDragMode::TRACKS_END;
      }
    }

    if (tv->selection_drag_mode == InteractionDragMode::TRACKS_START ||
        tv->selection_drag_mode == InteractionDragMode::TRACKS_END) {
      if (!input.is_mouse_down) {
        tv->selection_drag_mode = InteractionDragMode::NONE;
      }
    }

    if (tv->selection_drag_mode == InteractionDragMode::BOX_SELECT) {
      tv->box_select_end = ImVec2(input.mouse_x, input.mouse_y);
      if (!input.is_mouse_down) {
        trace_viewer_box_select_update(tv, td, allocator);
        tv->selection_drag_mode = InteractionDragMode::NONE;
        if (tv->selected_event_indices.size > 0) {
          tv->show_details_panel = true;
        }
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
        tv->selection_drag_mode == InteractionDragMode::NONE) {
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
  bool track_list_hovered =
      input.tracks_hovered &&
      mouse_in_tracks_content &&
      (mouse_in_selection || input.is_mouse_double_clicked) &&
      tv->selection_drag_mode == InteractionDragMode::NONE &&
      !proximity.near_start && !proximity.near_end;

  bool is_dragging = (tv->selection_drag_mode != InteractionDragMode::NONE);
  bool was_drag = (std::abs(input.drag_delta_x) >= input.drag_threshold ||
                   std::abs(input.drag_delta_y) >= input.drag_threshold);
  bool should_snap = is_dragging && was_drag &&
                     tv->selection_drag_mode != InteractionDragMode::BOX_SELECT;

  if (tv->selected_events_dirty) {
    track_renderer_update_selection_bitset(&tv->track_renderer_state, td, tv->selected_event_indices, allocator);
    tv->selected_events_dirty = false;
  }

  for (size_t i = 0; i < tv->tracks.size; i++) {
    Track& t = tv->tracks[i];
    TrackViewInfo& vi = tv->track_infos[i];

    vi.height = (t.type == TRACK_TYPE_COUNTER)
                             ? counter_track_height
                             : (float)(t.max_depth + 2) * input.lane_height;
    vi.y = input.canvas_y + input.ruler_height + tv->total_tracks_height - input.tracks_scroll_y;

    if (tv->request_scroll_to_focused_event) {
      for (size_t j = 0; j < t.event_indices.size; j++) {
        if (t.event_indices[j] == (size_t)tv->focused_event_idx) {
          float track_top = tv->total_tracks_height;
          float viewport_height = input.canvas_height - input.ruler_height;
          tv->target_scroll_y = track_top - (viewport_height - vi.height) * 0.5f;
          tv->request_scroll_to_focused_event = false;
          break;
        }
      }
    }

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
            tracks_inner_width, tracks_origin_x, tv->focused_event_idx,
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
        track_compute_counter_render_blocks(
            &t, td, tv->viewport.start_time, tv->viewport.end_time,
            tracks_inner_width, tracks_origin_x, tv->focused_event_idx,
            &tv->track_renderer_state, &tv->counter_render_blocks, allocator);
        
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
  if (!interaction_ignored) {
    if (input.ruler_active) {
      if (tv->selection_drag_mode == InteractionDragMode::RULER_NEW) {
        if (was_drag) {
          tv->selection_end_time = tv->snap_best_ts;
        }
      } else if (tv->selection_drag_mode == InteractionDragMode::RULER_START) {
        tv->selection_start_time = tv->snap_best_ts;
      } else if (tv->selection_drag_mode == InteractionDragMode::RULER_END) {
        tv->selection_end_time = tv->snap_best_ts;
      }
    } else if (input.tracks_hovered) {
      if (tv->selection_drag_mode == InteractionDragMode::TRACKS_START ||
          tv->selection_drag_mode == InteractionDragMode::TRACKS_END) {
          double ts = (input.is_mouse_clicked) ? mouse_ts : tv->snap_best_ts;
          if (tv->selection_drag_mode == InteractionDragMode::TRACKS_START) {
            tv->selection_start_time = ts;
          } else {
            tv->selection_end_time = ts;
          }
      }
    }
  }

  // 5. Selection (Click to select event, Double-click to zoom)
  if (input.is_mouse_double_clicked) {
    if (tv->hover_matches.size > 0) {
      const HoverMatch& hm = tv->hover_matches[tv->hover_matches.size - 1];
      const Track& t = tv->tracks[hm.track_idx];
      if (t.type == TRACK_TYPE_THREAD && hm.rb.event_idx != (size_t)-1) {
        const TraceEventPersisted& e = td->events[hm.rb.event_idx];
        tv->focused_event_idx = (int64_t)hm.rb.event_idx;
        tv->show_details_panel = true;
        tv->ignore_next_release = true;
        trace_viewer_zoom_to_event(tv, e);
      }
    }
  } else if (!interaction_ignored && input.is_mouse_released && !was_drag) {
    if (tv->hover_matches.size > 0) {
      const HoverMatch& hm = tv->hover_matches[tv->hover_matches.size - 1];
      if (hm.rb.event_idx != (size_t)-1) {
        tv->focused_event_idx = (int64_t)hm.rb.event_idx;
        tv->show_details_panel = true;
      }
    } else if (input.tracks_hovered && mouse_in_tracks_content) {
      if (tv->selection_active && mouse_in_selection && !proximity.near_start && !proximity.near_end) {
        // Click inside selection: clear focused event, keep timeline range.
        tv->focused_event_idx = -1;
      } else if (tv->selection_active && !mouse_in_selection) {
        // Click outside selection: clear timeline range and focused event.
        tv->selection_active = false;
        tv->focused_event_idx = -1;
      } else {
        // No selection active, or click on background: clear focused event.
        tv->focused_event_idx = -1;
      }
    }
  }

  if (input.is_mouse_released) {
    if (interaction_ignored) {
      tv->ignore_next_release = false;
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

  if (tv->search.results_ready.exchange(false)) {
    std::lock_guard<std::mutex> lock(tv->search.mutex);
    LOG_INFO("MAIN THREAD RESULTS READY EXCHANGED! pending_results size = %zu, histogram buckets = %d",
             tv->search.pending_results.size, tv->search.pending_histogram.num_buckets);
    array_list_clear(&tv->selected_event_indices);
    array_list_append(&tv->selected_event_indices, allocator, tv->search.pending_results.data, tv->search.pending_results.size);
    tv->selected_events_dirty = true;
    
    tv->histogram = tv->search.pending_histogram;

    array_list_clear(&tv->filtered_event_indices);
    if (tv->selected_histogram_bucket == -1) {
      for (size_t i = 0; i < tv->selected_event_indices.size; i++) {
        size_t idx = (size_t)tv->selected_event_indices[i];
        if (idx < td->events.size) {
          array_list_push_back(&tv->filtered_event_indices, allocator, (int64_t)idx);
        }
      }
    } else if (tv->selected_histogram_bucket >= 0 && tv->selected_histogram_bucket < tv->histogram.num_buckets) {
      const DurationHistogramBucket& b = tv->histogram.buckets[tv->selected_histogram_bucket];
      for (size_t i = 0; i < tv->selected_event_indices.size; i++) {
        size_t idx = (size_t)tv->selected_event_indices[i];
        if (idx >= td->events.size) continue;
        const TraceEventPersisted& e = td->events[idx];
        int64_t d = e.dur;

        if (d >= b.min_dur && d <= b.max_dur) {
          array_list_push_back(&tv->filtered_event_indices, allocator, (int64_t)idx);
        }
      }
    }

    tv->search_histogram_dirty = false;
  }

  if (tv->search_histogram_dirty) {
    tv->search_histogram_dirty = false;

    array_list_clear(&tv->filtered_event_indices);
    if (tv->selected_histogram_bucket == -1) {
      for (size_t i = 0; i < tv->selected_event_indices.size; i++) {
        size_t idx = (size_t)tv->selected_event_indices[i];
        if (idx < td->events.size) {
          array_list_push_back(&tv->filtered_event_indices, allocator, (int64_t)idx);
        }
      }
    } else if (tv->selected_histogram_bucket >= 0 && tv->selected_histogram_bucket < tv->histogram.num_buckets) {
      const DurationHistogramBucket& b = tv->histogram.buckets[tv->selected_histogram_bucket];
      for (size_t i = 0; i < tv->selected_event_indices.size; i++) {
        size_t idx = (size_t)tv->selected_event_indices[i];
        if (idx >= td->events.size) continue;
        const TraceEventPersisted& e = td->events[idx];
        int64_t d = e.dur;

        if (d >= b.min_dur && d <= b.max_dur) {
          array_list_push_back(&tv->filtered_event_indices, allocator, (int64_t)idx);
        }
      }
    }
  }
}

void trace_viewer_draw(TraceViewer* tv, TraceData* td, Allocator allocator,
                       const Theme* theme_ptr) {
  const Theme& theme = *theme_ptr;

  if (td->events.size > 0) {
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();

    TraceViewerInput input = {
        .canvas_x = canvas_pos.x,
        .canvas_y = canvas_pos.y,
        .canvas_width = canvas_size.x,
        .canvas_height = canvas_size.y,
        .ruler_height = ImGui::GetFrameHeight(),
        .lane_height = ImGui::GetFrameHeight(),
        .mouse_x = ImGui::GetMousePos().x,
        .mouse_y = ImGui::GetMousePos().y,
        .mouse_wheel = ImGui::GetIO().MouseWheel,
        .mouse_wheel_h = ImGui::GetIO().MouseWheelH,
        .click_x = ImGui::GetIO().MouseClickedPos[0].x,
        .click_y = ImGui::GetIO().MouseClickedPos[0].y,
        .is_mouse_down = ImGui::IsMouseDown(0),
        .is_mouse_clicked = ImGui::IsMouseClicked(0),
        .is_mouse_double_clicked = ImGui::IsMouseDoubleClicked(0),
        .is_mouse_released = ImGui::IsMouseReleased(0),
        .mouse_delta_x = ImGui::GetIO().MouseDelta.x,
        .mouse_delta_y = ImGui::GetIO().MouseDelta.y,
        .drag_delta_x = ImGui::GetMouseDragDelta(0).x,
        .drag_delta_y = ImGui::GetMouseDragDelta(0).y,
        .drag_threshold = ImGui::GetIO().MouseDragThreshold,
        .is_ctrl_down = ImGui::IsKeyDown(ImGuiMod_Ctrl),
        .is_shift_down = ImGui::IsKeyDown(ImGuiMod_Shift),
    };

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

      // Top-left of the child window area (excluding scroll)
      ImVec2 window_pos = ImGui::GetWindowPos();
      float padding_x = ImGui::GetStyle().WindowPadding.x;
      float padding_y = ImGui::GetStyle().WindowPadding.y;

      input.canvas_x = window_pos.x + padding_x;
      input.canvas_y = window_pos.y + padding_y - input.ruler_height;
      input.canvas_width = ImGui::GetContentRegionAvail().x;
      input.canvas_height = ImGui::GetContentRegionAvail().y + input.ruler_height;
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
        tv->viewport.start_time, tv->viewport.end_time, input.canvas_width,
        input.canvas_x, input.mouse_x);
    SelectionProximity proximity = trace_viewer_selection_check_proximity(
        tv, current_mouse_ts, (5.0f / input.canvas_width) *
                                  (tv->viewport.end_time - tv->viewport.start_time));

    if (tv->selection_drag_mode != InteractionDragMode::BOX_SELECT &&
        ((tv->selection_drag_mode != InteractionDragMode::NONE &&
          tv->selection_drag_mode != InteractionDragMode::RULER_NEW) ||
         proximity.near_start || proximity.near_end)) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }

    ImGui::SetCursorScreenPos(ImVec2(canvas_pos.x, canvas_pos.y + input.ruler_height));
    if (ImGui::BeginChild("TrackList", ImVec2(0, canvas_size.y - input.ruler_height),
                          ImGuiChildFlags_None, child_flags)) {
      if (tv->target_scroll_y != -1.0f) {
        ImGui::SetScrollY(std::max(0.0f, tv->target_scroll_y));
        tv->target_scroll_y = -1.0f;
      }

      // Handle vertical scroll if dragging tracks
      if (ImGui::IsWindowHovered() &&
          ImGui::IsMouseDragging(ImGuiMouseButton_Left) &&
          tv->selection_drag_mode == InteractionDragMode::NONE) {
        ImGui::SetScrollY(ImGui::GetScrollY() - ImGui::GetIO().MouseDelta.y);
      }

      ImDrawList* track_draw_list = ImGui::GetWindowDrawList();

      ImGui::Dummy(ImVec2(0.0f, tv->total_tracks_height));
      ImGui::SetCursorPos(ImVec2(0, 0));

      ImVec2 tracks_canvas_pos = ImGui::GetCursorScreenPos();
      float inner_width = ImGui::GetContentRegionAvail().x;
      float inner_height = ImGui::GetContentRegionAvail().y;

      // Update last_* fields with current frame's accurate UNSCROLLED values
      tv->last_tracks_x = tracks_canvas_pos.x;
      tv->last_tracks_y = tracks_canvas_pos.y + ImGui::GetScrollY();
      tv->last_inner_width = inner_width;
      tv->last_inner_height = inner_height;

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
              inner_width, tracks_canvas_pos.x, tv->focused_event_idx,
              &tv->track_renderer_state, &tv->render_blocks, allocator);

          for (size_t k = 0; k < tv->render_blocks.size; k++) {
            const TrackRenderBlock& rb = tv->render_blocks[k];
            float y1 = track_pos.y + (float)(rb.depth + 1) * input.lane_height;
            float y2 = y1 + input.lane_height - 1.0f;

            trace_viewer_draw_event(tv, td, track_draw_list, rb.x1, rb.x2, y1,
                                    y2, rb.color, rb.is_selected, rb.is_focused,
                                    rb.name_ref, inner_width,
                                    tracks_canvas_pos.x, theme);
          }
        } else {
          bool mouse_in_sel = input.tracks_hovered && 
                              trace_viewer_selection_is_mouse_inside(
              tv, trace_viewer_px_to_ts(tv->viewport.start_time,
                                        tv->viewport.end_time, inner_width,
                                        tracks_canvas_pos.x, input.mouse_x));
          trace_viewer_draw_counter_track(
              tv, td, track_draw_list, t,
              ImVec2(track_pos.x, track_pos.y + input.lane_height), inner_width,
              vi.height - input.lane_height, tv->viewport.start_time,
              tv->viewport.end_time, theme,
              ImVec2(input.mouse_x, input.mouse_y), mouse_in_sel,
              tv->focused_event_idx, allocator);
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
                                     rb.is_focused, rb.name_ref, inner_width,
                                     tracks_canvas_pos.x, theme);
           }
        }

        // Show tooltip
        trace_viewer_draw_tooltip(tv, td, best_hm, inner_width, tracks_canvas_pos.x);
      }

      if (tv->selection_drag_mode == InteractionDragMode::BOX_SELECT) {
        track_draw_list->AddRectFilled(tv->box_select_start, tv->box_select_end,
                                       theme.box_selection_bg);
        track_draw_list->AddRect(tv->box_select_start, tv->box_select_end,
                                 theme.box_selection_border);
      }

      trace_viewer_draw_selection_overlay(
          tv, track_draw_list, ImVec2(tracks_canvas_pos.x, ImGui::GetWindowPos().y),
          ImVec2(inner_width, ImGui::GetWindowSize().y), theme, true);

      if (tv->snap_has_snap &&
          tv->selection_drag_mode != InteractionDragMode::NONE) {
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
      // Search Section
      trace_viewer_draw_search_section(tv, allocator);
      ImGui::Separator();

      bool has_focus = (tv->focused_event_idx != -1);
      bool has_selection = (tv->selected_event_indices.size > 0);

      if (has_selection) {
        ImGui::TextDisabled("Selection (%zu events)",
                            tv->selected_event_indices.size);
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) {
          array_list_clear(&tv->selected_event_indices);
          array_list_clear(&tv->filtered_event_indices);
          tv->selected_histogram_bucket = -1;
          tv->search_histogram_dirty = true;
          tv->selected_events_dirty = true;
        }

        // Duration Histogram
        const DurationHistogram& h = tv->histogram;
        if (h.num_buckets > 0) {
          ImGui::Spacing();
          ImGui::TextDisabled("Duration Distribution");

          ImVec2 h_canvas_pos = ImGui::GetCursorScreenPos();
          float h_canvas_width = ImGui::GetContentRegionAvail().x;
          float h_canvas_height = 80.0f;

          ImGui::InvisibleButton("##dur_histogram", ImVec2(h_canvas_width, h_canvas_height));
          bool is_hovered = ImGui::IsItemHovered();
          ImVec2 mouse_pos = ImGui::GetIO().MousePos;

          ImDrawList* draw_list = ImGui::GetWindowDrawList();

          // Draw background
          draw_list->AddRectFilled(h_canvas_pos, ImVec2(h_canvas_pos.x + h_canvas_width, h_canvas_pos.y + h_canvas_height),
                                   theme.search_histogram_bg, 4.0f);

          float bar_spacing = 2.0f;
          float total_spacing = bar_spacing * static_cast<float>(h.num_buckets - 1);
          float bar_width = (h_canvas_width - 10.0f - total_spacing) / static_cast<float>(h.num_buckets);

          float baseline_y = h_canvas_pos.y + h_canvas_height - 20.0f;

          int hovered_bucket = -1;

          for (int i = 0; i < h.num_buckets; i++) {
            const DurationHistogramBucket& b = h.buckets[i];
            
            float h_ratio = (h.max_bucket_count > 0) ? static_cast<float>(b.count) / static_cast<float>(h.max_bucket_count) : 0.0f;
            float bar_h = h_ratio * (h_canvas_height - 35.0f);

            float x1 = h_canvas_pos.x + 5.0f + static_cast<float>(i) * (bar_width + bar_spacing);
            float x2 = x1 + bar_width;
            float y1 = baseline_y - bar_h;
            float y2 = baseline_y;

            bool bucket_hovered = is_hovered && mouse_pos.x >= x1 && mouse_pos.x <= x2 &&
                                  mouse_pos.y >= h_canvas_pos.y && mouse_pos.y <= baseline_y;

            if (bucket_hovered) {
              hovered_bucket = i;
            }

            bool is_selected = (tv->selected_histogram_bucket == i);

            ImU32 bar_col;
            if (is_selected) {
              bar_col = theme.search_histogram_bar_selected;
            } else if (bucket_hovered) {
              bar_col = theme.search_histogram_bar_hovered;
            } else {
              bar_col = theme.search_histogram_bar;
            }

            if (b.count > 0) {
              draw_list->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2), bar_col, 2.0f);
            }
          }

          // Draw baseline (X Axis)
          draw_list->AddLine(ImVec2(h_canvas_pos.x + 5.0f, baseline_y),
                             ImVec2(h_canvas_pos.x + h_canvas_width - 5.0f, baseline_y),
                             theme.ruler_border);

          // Draw vertical axis (Y Axis)
          draw_list->AddLine(ImVec2(h_canvas_pos.x + 5.0f, h_canvas_pos.y + 5.0f),
                             ImVec2(h_canvas_pos.x + 5.0f, baseline_y),
                             theme.ruler_border);

          // Label Y-Axis bounds
          char y_max_buf[32];
          snprintf(y_max_buf, sizeof(y_max_buf), "%u", h.max_bucket_count);
          draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.75f,
                             ImVec2(h_canvas_pos.x + 8.0f, h_canvas_pos.y + 4.0f),
                             theme.ruler_text, y_max_buf);
          draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.75f,
                             ImVec2(h_canvas_pos.x + 8.0f, baseline_y - 12.0f),
                             theme.ruler_text, "0");

          // Label X-Axis bounds
          char min_label[32] = "";
          char max_label[32] = "";
          if (h.num_buckets > 0) {
            int first_non_zero_idx = 0;
            if (h.buckets[0].min_dur <= 0 && h.buckets[0].max_dur <= 0 && h.num_buckets > 1) {
              first_non_zero_idx = 1;
            }
            format_duration(min_label, sizeof(min_label), static_cast<double>(h.buckets[first_non_zero_idx].min_dur));
            format_duration(max_label, sizeof(max_label), static_cast<double>(h.buckets[h.num_buckets - 1].max_dur));
          }

          draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.75f,
                             ImVec2(h_canvas_pos.x + 5.0f, baseline_y + 2.0f),
                             theme.ruler_text, min_label);

          float max_text_w = ImGui::CalcTextSize(max_label).x * 0.75f;
          draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.75f,
                             ImVec2(h_canvas_pos.x + h_canvas_width - 5.0f - max_text_w, baseline_y + 2.0f),
                             theme.ruler_text, max_label);

          if (hovered_bucket != -1) {
            const DurationHistogramBucket& b = h.buckets[hovered_bucket];

            ImGui::BeginTooltip();
            
            char min_buf[32], max_buf[32];
            if (b.min_dur <= 0 && b.max_dur <= 0) {
              ImGui::Text("Duration: <= 0 us");
            } else {
              format_duration(min_buf, sizeof(min_buf), (double)b.min_dur);
              format_duration(max_buf, sizeof(max_buf), (double)b.max_dur);
              ImGui::Text("Duration: %s - %s", min_buf, max_buf);
            }

            float percent = h.total_count > 0 ? (static_cast<float>(b.count) / static_cast<float>(h.total_count)) * 100.0f : 0.0f;
            ImGui::Text("Count: %u events (%.1f%%)", b.count, percent);
            if (b.count > 0) {
              ImGui::TextDisabled("Click to filter table below");
            }
            
            ImGui::EndTooltip();

            if (ImGui::IsMouseClicked(0) && b.count > 0) {
              if (tv->selected_histogram_bucket == hovered_bucket) {
                tv->selected_histogram_bucket = -1;
              } else {
                tv->selected_histogram_bucket = hovered_bucket;
              }
              tv->search_histogram_dirty = true;
            }
          }
        }

        if (tv->filtered_event_indices.size > 0) {
          if (ImGui::BeginTable("##multi_select", 4,
                               ImGuiTableFlags_Resizable |
                                   ImGuiTableFlags_ScrollY |
                                   ImGuiTableFlags_RowBg |
                                   ImGuiTableFlags_Borders |
                                   ImGuiTableFlags_SizingFixedFit |
                                   ImGuiTableFlags_Sortable |
                                   ImGuiTableFlags_SortTristate |
                                   ImGuiTableFlags_NoSavedSettings,
                               ImVec2(0, 200.0f))) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Start", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Duration", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs();
            if (specs != nullptr && specs->SpecsDirty) {
              if (specs->SpecsCount > 0) {
                const ImGuiTableColumnSortSpecs& spec = specs->Specs[0];

                std::lock_guard<std::mutex> lock(tv->search.mutex);
                tv->search.sort_column = spec.ColumnIndex;
                tv->search.sort_ascending = (spec.SortDirection == ImGuiSortDirection_Ascending);
                tv->search.sort_none = (spec.SortDirection == ImGuiSortDirection_None);
                tv->search.new_sort_specs_available.store(true);

                platform_submit_job(trace_viewer_search_job, &tv->search);
              }
              specs->SpecsDirty = false;
            }

            ImGuiListClipper clipper;
            clipper.Begin((int)tv->filtered_event_indices.size);
            while (clipper.Step()) {
              for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                size_t event_idx =
                    (size_t)tv->filtered_event_indices[(size_t)i];
                const TraceEventPersisted& e = td->events[event_idx];
                std::string_view name = trace_data_get_string(td, e.name_ref);
                std::string_view cat = trace_data_get_string(td, e.cat_ref);

                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                bool is_focused = (tv->focused_event_idx == (int64_t)event_idx);
                char label[256];
                snprintf(label, sizeof(label), "%.*s##%zu", (int)name.size(),
                         name.data(), event_idx);
                if (ImGui::Selectable(label, is_focused,
                                      ImGuiSelectableFlags_SpanAllColumns |
                                          ImGuiSelectableFlags_AllowOverlap)) {
                  tv->target_focused_event_idx = (int64_t)event_idx;
                }

                ImGui::TableNextColumn();
                ImGui::TextUnformatted(cat.data(), cat.data() + cat.size());

                ImGui::TableNextColumn();
                char ts_buf[32];
                format_duration(ts_buf, sizeof(ts_buf),
                                (double)e.ts - (double)tv->viewport.min_ts);
                ImGui::TextUnformatted(ts_buf);

                ImGui::TableNextColumn();
                if (e.dur > 0) {
                  char dur_buf[32];
                  format_duration(dur_buf, sizeof(dur_buf), (double)e.dur);
                  ImGui::TextUnformatted(dur_buf);
                }
              }
            }
            ImGui::EndTable();
          }

          if (tv->selected_histogram_bucket != -1) {
            ImGui::TextDisabled("Filtered results: %zu / %zu events", tv->filtered_event_indices.size, tv->selected_event_indices.size);
          }
        }
      }

      if (has_focus && has_selection) {
        ImGui::Separator();
      }

      if (has_focus) {
        const TraceEventPersisted& e =
            td->events[(size_t)tv->focused_event_idx];
        std::string_view ph = trace_data_get_string(td, e.ph_ref);
        const Track* target_track = nullptr;

        // Match track containing selected event
        for (size_t t_idx = 0; t_idx < tv->tracks.size; t_idx++) {
          const Track& test_t = tv->tracks[t_idx];
          bool is_counter = (ph.size() == 1 && ph[0] == 'C');

          if (is_counter) {
            if (test_t.type == TRACK_TYPE_COUNTER &&
                test_t.pid == e.pid &&
                test_t.name_ref == e.name_ref &&
                test_t.id_ref == e.id_ref) {
              target_track = &test_t;
              break;
            }
          } else {
            if (test_t.type == TRACK_TYPE_THREAD &&
                test_t.pid == e.pid &&
                test_t.tid == e.tid) {
              target_track = &test_t;
              break;
            }
          }
        }

        ImGui::TextDisabled("Focused Event");
        ImGui::Spacing();

        trace_viewer_draw_event_properties(td, e, (double)tv->viewport.min_ts, true, target_track);
      }

      if (!has_focus && !has_selection) {
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
  tv->selected_events_dirty = true;
}

struct SortKey {
  int64_t event_idx;
  std::string_view text;
  int64_t numeric_val;
};

static void trace_viewer_sort_results(const TraceData* td, ArrayList<int64_t>* results,
                                      int sort_column, bool sort_ascending, bool sort_none,
                                      Allocator allocator) {
  if (results->size <= 1) return;

  if (sort_none) {
    std::sort(results->data, results->data + results->size);
    return;
  }

  ArrayList<SortKey> keys = {};
  array_list_resize(&keys, allocator, results->size);

  for (size_t i = 0; i < results->size; i++) {
    int64_t idx = results->data[i];
    const TraceEventPersisted& e = td->events[(size_t)idx];

    SortKey sk;
    sk.event_idx = idx;

    switch (sort_column) {
      case 0:
        sk.text = trace_data_get_string(td, e.name_ref);
        break;
      case 1:
        sk.text = trace_data_get_string(td, e.cat_ref);
        break;
      case 2:
        sk.numeric_val = e.ts;
        break;
      case 3:
        sk.numeric_val = e.dur;
        break;
      default:
        sk.numeric_val = 0;
        break;
    }
    keys.data[i] = sk;
  }

  std::sort(keys.data, keys.data + keys.size,
            [sort_column, sort_ascending](const SortKey& a, const SortKey& b) {
              int comp = 0;
              switch (sort_column) {
                case 0:
                case 1: {
                  if (a.text.data() == b.text.data()) {
                    comp = 0;
                  } else {
                    comp = a.text.compare(b.text);
                  }
                  break;
                }
                case 2:
                case 3: {
                  if (a.numeric_val < b.numeric_val) comp = -1;
                  else if (a.numeric_val > b.numeric_val) comp = 1;
                  break;
                }
              }

              if (comp == 0) {
                return sort_ascending ? (a.event_idx < b.event_idx) : (a.event_idx > b.event_idx);
              }

              return sort_ascending ? (comp < 0) : (comp > 0);
            });

  for (size_t i = 0; i < results->size; i++) {
    results->data[i] = keys.data[i].event_idx;
  }

  array_list_deinit(&keys, allocator);
}

void trace_viewer_search_job(void* user_data) {
  SearchState* s = (SearchState*)user_data;
  TraceData* td = s->td;
  Allocator allocator = s->allocator;

  if (s->jobs_should_abort.load()) return;

  s->is_searching.store(true);

  ArrayList<char> query = {};
  bool do_search = false;
  bool do_sort = false;
  bool do_box_select = false;
  
  int sort_column = 0;
  bool sort_ascending = true;
  bool sort_none = true;

  {
    std::lock_guard<std::mutex> lock(s->mutex);
    if (s->new_query_available.load()) {
      array_list_append(&query, allocator, s->pending_query.data, s->pending_query.size);
      s->new_query_available.store(false);
      do_search = true;
    }
    
    if (s->new_sort_specs_available.load()) {
      s->new_sort_specs_available.store(false);
      do_sort = true;
    }

    if (s->new_box_selection_available.load()) {
      s->new_box_selection_available.store(false);
      do_box_select = true;
    }

    sort_column = s->sort_column;
    sort_ascending = s->sort_ascending;
    sort_none = s->sort_none;
  }

  if (do_search) {
    ArrayList<int64_t> results = {};
    if (query.size > 0 && query.data[0] != '\0') {
      const char* query_ptr = query.data;
      size_t query_len = strlen(query_ptr);
      size_t n_events = td->events.size;

      auto contains = [](std::string_view text, const char* q, size_t q_len) {
        if (q_len == 0) return true;
        if (text.length() < q_len) return false;
        auto it = std::search(
          text.begin(), text.end(),
          q, q + q_len,
          [](char a, char b) { return std::tolower(a) == std::tolower(b); }
        );
        return it != text.end();
      };

      bool include_threads = s->include_thread_events.load();
      bool include_counters = s->include_counter_events.load();

      for (size_t i = 0; i < n_events; i++) {
        if (s->new_query_available.load() || s->jobs_should_abort.load()) break;

        const TraceEventPersisted& e = td->events[i];
        std::string_view ph = trace_data_get_string(td, e.ph_ref);
        bool is_counter = (ph.size() == 1 && ph[0] == 'C');
        bool is_metadata = (ph.size() == 1 && ph[0] == 'M');

        if (is_counter && !include_counters) continue;
        if (!is_counter && !is_metadata && !include_threads) continue;

        std::string_view name = trace_data_get_string(td, e.name_ref);
        std::string_view cat = trace_data_get_string(td, e.cat_ref);

        bool match = contains(name, query_ptr, query_len) ||
                     contains(cat, query_ptr, query_len);

        if (!match) {
          for (uint32_t k = 0; k < e.args_count; k++) {
            const TraceArgPersisted& arg = td->args[e.args_offset + k];

            if (arg.val_ref != 0) {
              std::string_view arg_val = trace_data_get_string(td, arg.val_ref);
              if (contains(arg_val, query_ptr, query_len)) {
                match = true;
                break;
              }
            }
          }
        }

        if (match) {
          array_list_push_back(&results, allocator, (int64_t)i);
        }
      }

      if (!s->new_query_available.load() && !s->jobs_should_abort.load()) {
        trace_viewer_sort_results(td, &results, sort_column, sort_ascending, sort_none, allocator);

        if (!s->new_query_available.load() && !s->jobs_should_abort.load()) {
          DurationHistogram hist = {};
          trace_viewer_calculate_histogram(results, td, &hist);

          std::lock_guard<std::mutex> lock(s->mutex);
          LOG_INFO("BACKGROUND SEARCH JOB COMPLETED! results size = %zu, histogram buckets = %d",
                   results.size, hist.num_buckets);
          array_list_clear(&s->pending_results);
          array_list_append(&s->pending_results, allocator, results.data, results.size);
          s->pending_histogram = hist;
          s->results_ready.store(true);
        }
      }
      array_list_deinit(&results, allocator);
    } else {
      std::lock_guard<std::mutex> lock(s->mutex);
      array_list_clear(&s->pending_results);
      s->pending_histogram = {};
      s->results_ready.store(true);
    }
  } else if (do_box_select || do_sort) {
    ArrayList<int64_t> results = {};
    {
      std::lock_guard<std::mutex> lock(s->mutex);
      array_list_append(&results, allocator, s->pending_results.data, s->pending_results.size);
    }

    trace_viewer_sort_results(td, &results, sort_column, sort_ascending, sort_none, allocator);

    if (!s->new_query_available.load() && !s->jobs_should_abort.load()) {
      DurationHistogram hist = {};
      trace_viewer_calculate_histogram(results, td, &hist);

      std::lock_guard<std::mutex> lock(s->mutex);
      array_list_clear(&s->pending_results);
      array_list_append(&s->pending_results, allocator, results.data, results.size);
      s->pending_histogram = hist;
      s->results_ready.store(true);
    }
    array_list_deinit(&results, allocator);
  }

  s->is_searching.store(false);
  s->request_update.store(true, std::memory_order_relaxed);
  array_list_deinit(&query, allocator);

  {
    std::lock_guard<std::mutex> lock(s->quit_mutex);
    s->quit_cv.notify_all();
  }
}

struct InputTextCallback_UserData {
  ArrayList<char>* al;
  Allocator allocator;
};

static int trace_viewer_search_input_callback(ImGuiInputTextCallbackData* data) {
  InputTextCallback_UserData* ud = (InputTextCallback_UserData*)data->UserData;
  if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
    ArrayList<char>* al = ud->al;
    array_list_reserve(al, ud->allocator, (size_t)data->BufSize);
    data->Buf = al->data;
  }
  return 0;
}

static void trace_viewer_draw_search_section(TraceViewer* tv, Allocator allocator) {
  ImGui::TextDisabled("Search Events");
  if (tv->focus_search_input) {
    ImGui::SetKeyboardFocusHere();
    tv->focus_search_input = false;
  }

  if (tv->search_query.capacity == 0) {
    array_list_reserve(&tv->search_query, allocator, 128);
    tv->search_query.data[0] = '\0';
    tv->search_query.size = 1;
  }

  InputTextCallback_UserData cb_data = {&tv->search_query, allocator};
  bool search_input_changed = ImGui::InputText("##search", tv->search_query.data,
                                             tv->search_query.capacity,
                                             ImGuiInputTextFlags_CallbackResize,
                                             trace_viewer_search_input_callback, &cb_data);

  bool enter_pressed = ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter);

  ImGui::Spacing();
  bool filter_changed = false;
  filter_changed |= ImGui::Checkbox("Threads", &tv->search_thread_events);
  ImGui::SameLine();
  filter_changed |= ImGui::Checkbox("Counters", &tv->search_counter_events);

  if (search_input_changed || enter_pressed || filter_changed) {
    std::lock_guard<std::mutex> lock(tv->search.mutex);
    const char* last_query = (tv->search.pending_query.size > 0 && tv->search.pending_query.data)
                                 ? tv->search.pending_query.data
                                 : "";

    bool should_trigger = false;
    if (enter_pressed) {
      if (tv->selected_event_indices.size == 0) {
        should_trigger = true;
      }
    } else if (search_input_changed) {
      if (strcmp(last_query, tv->search_query.data) != 0) {
        should_trigger = true;
      }
    } else if (filter_changed) {
      should_trigger = true;
    }

    if (should_trigger) {
      array_list_clear(&tv->search.pending_query);
      array_list_append(&tv->search.pending_query, allocator, tv->search_query.data, strlen(tv->search_query.data) + 1);
      
      tv->search.include_thread_events.store(tv->search_thread_events);
      tv->search.include_counter_events.store(tv->search_counter_events);

      tv->search.new_query_available.store(true);
      tv->search.results_ready.store(false);
      tv->selected_histogram_bucket = -1;
      tv->search_histogram_dirty = true;
      platform_submit_job(trace_viewer_search_job, &tv->search);
    }
  }

  if (tv->search_query.data[0] != '\0') {
    ImGui::Text("%zu events selected", tv->selected_event_indices.size);
    if (tv->search.is_searching.load()) {
       ImGui::SameLine();
       ImGui::TextDisabled("(searching...)");
    }
  }
}
