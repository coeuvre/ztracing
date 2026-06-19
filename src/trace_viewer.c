#include "src/trace_viewer.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/colors.h"
#include "src/format.h"
#include "src/imgui_c.h"
#include "src/logging.h"
#include "src/platform.h"

static void trace_viewer_draw_selection_overlay(trace_viewer_t* tv,
                                                ig_draw_list_t* draw_list,
                                                ig_vec2_t pos, ig_vec2_t size,
                                                const theme_t* theme,
                                                bool draw_duration_text);

static void trace_viewer_draw_search_section(trace_viewer_t* tv,
                                             allocator_t allocator);
void trace_viewer_search_job(void* user_data);
static void trace_viewer_step_vertical_minimap(
    trace_viewer_t* tv, const trace_viewer_input_t* input);
static void trace_viewer_draw_vertical_minimap(const trace_viewer_t* tv,
                                               const trace_data_t* td,
                                               ig_draw_list_t* draw_list,
                                               const theme_t* theme);

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define clamp(val, lo, hi) (min(max(val, lo), hi))
#define swap(t, a, b) \
  do {                \
    t temp = a;       \
    a = b;            \
    b = temp;         \
  } while (0)

static bool trace_viewer_str_contains_case_insensitive(string_t text,
                                                       const char* q,
                                                       size_t q_len) {
  if (q_len == 0) return true;
  if (text.len < q_len) return false;

  for (size_t i = 0; i <= text.len - q_len; i++) {
    bool match = true;
    for (size_t j = 0; j < q_len; j++) {
      char a = text.ptr[i + j];
      char b = q[j];

      if (a >= 'A' && a <= 'Z') a = a - 'A' + 'a';
      if (b >= 'A' && b <= 'Z') b = b - 'A' + 'a';

      if (a != b) {
        match = false;
        break;
      }
    }
    if (match) return true;
  }
  return false;
}

static void trace_viewer_set_clipboard_string(string_t s,
                                              allocator_t allocator) {
  if (s.len < 512) {
    char buf[512];
    memcpy(buf, s.ptr, s.len);
    buf[s.len] = '\0';
    ig_set_clipboard_text(buf);
  } else {
    char* temp = (char*)allocator_alloc(allocator, s.len + 1);
    memcpy(temp, s.ptr, s.len);
    temp[s.len] = '\0';
    ig_set_clipboard_text(temp);
    allocator_free(allocator, temp, s.len + 1);
  }
}

static int trace_viewer_int64_compare(const void* va, const void* vb) {
  int64_t a = *(const int64_t*)va;
  int64_t b = *(const int64_t*)vb;
  if (a < b) return -1;
  if (a > b) return 1;
  return 0;
}

static double trace_viewer_px_to_ts(double start_time, double end_time,
                                    float width, float origin_x, float px) {
  double duration = end_time - start_time;
  if (width <= 0) return start_time;
  return start_time + ((double)(px - origin_x) / (double)width) * duration;
}

static void trace_viewer_snapping_reset(trace_viewer_t* tv, double mouse_ts,
                                        float threshold_px) {
  tv->snap_best_ts = mouse_ts;
  tv->snap_best_dist_px = threshold_px;
  tv->snap_threshold_px = threshold_px;
  tv->snap_has_snap = false;
  tv->snap_px = 0.0f;
  tv->snap_y1 = 0.0f;
  tv->snap_y2 = 0.0f;
}

static void trace_viewer_snapping_suggest(trace_viewer_t* tv,
                                          double candidate_ts,
                                          float candidate_px, float mouse_px,
                                          float y1, float y2) {
  float dist_px = fabsf(candidate_px - mouse_px);
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
typedef struct SelectionProximity SelectionProximity;

static SelectionProximity trace_viewer_selection_check_proximity(
    const trace_viewer_t* tv, double mouse_ts, double threshold_ts) {
  SelectionProximity result = {false, false};
  if (!tv->selection_active) return result;

  double dist_start = fabs(mouse_ts - tv->selection_start_time);
  double dist_end = fabs(mouse_ts - tv->selection_end_time);

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

static bool trace_viewer_selection_is_mouse_inside(const trace_viewer_t* tv,
                                                   double mouse_ts) {
  if (!tv->selection_active) return true;
  double t1 = tv->selection_start_time;
  double t2 = tv->selection_end_time;
  if (t1 > t2) swap(double, t1, t2);
  return mouse_ts >= t1 && mouse_ts <= t2;
}

const double TRACE_VIEWER_MAX_ZOOM_FACTOR = 1.2;
const double TRACE_VIEWER_MIN_ZOOM_DURATION = 1000.0;  // 1ms = 1000us

void trace_viewer_deinit(trace_viewer_t* tv, allocator_t allocator) {
  track_t* tracks = (track_t*)tv->tracks.ptr;
  for (size_t i = 0; i < tv->tracks.len; i++) {
    track_deinit(&tracks[i], allocator);
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
  array_list_deinit(&tv->vertical_minimap.track_has_selected, allocator);
  array_list_deinit(&tv->vertical_minimap.track_heatmap_densities, allocator);
  array_list_deinit(&tv->search_query, allocator);
  atomic_store(&tv->search.jobs_should_abort, true);
  {
    pthread_mutex_lock(&tv->search.mutex);
    pthread_cond_broadcast(&tv->search.quit_cv);
    pthread_mutex_unlock(&tv->search.mutex);
  }
  if (atomic_load(&tv->search.is_searching)) {
    pthread_mutex_lock(&tv->search.quit_mutex);
    while (atomic_load(&tv->search.is_searching)) {
      pthread_cond_wait(&tv->search.quit_cv, &tv->search.quit_mutex);
    }
    pthread_mutex_unlock(&tv->search.quit_mutex);
  }
  array_list_deinit(&tv->search.pending_query, allocator);
  array_list_deinit(&tv->search.pending_results, allocator);
}

static void trace_viewer_draw_time_ruler(trace_viewer_t* tv,
                                         ig_draw_list_t* draw_list,
                                         ig_vec2_t pos, ig_vec2_t size,
                                         float canvas_x, float canvas_width,
                                         const theme_t* theme) {
  ig_draw_list_add_rect_filled(
      draw_list, (ig_vec2_t){canvas_x, pos.y},
      (ig_vec2_t){canvas_x + canvas_width, pos.y + size.y}, theme->ruler_bg);
  ig_draw_list_add_line(
      draw_list, (ig_vec2_t){canvas_x, pos.y + size.y - 1},
      (ig_vec2_t){canvas_x + canvas_width, pos.y + size.y - 1},
      theme->ruler_border, 1.0f);

  const ruler_tick_t* ticks = (const ruler_tick_t*)tv->ruler_ticks.ptr;
  for (size_t i = 0; i < tv->ruler_ticks.len; i++) {
    const ruler_tick_t* tick = &ticks[i];
    ig_draw_list_add_line(
        draw_list, (ig_vec2_t){tick->x, pos.y + size.y * 0.6f},
        (ig_vec2_t){tick->x, pos.y + size.y - 1}, theme->ruler_tick, 1.0f);
    ig_draw_list_add_text_simple(draw_list,
                                 (ig_vec2_t){tick->x + 3.0f, pos.y + 2.0f},
                                 theme->ruler_text, tick->label, nullptr);
  }

  trace_viewer_draw_selection_overlay(
      tv, draw_list, (ig_vec2_t){canvas_x, pos.y},
      (ig_vec2_t){canvas_width, size.y}, theme, false);
}

static void trace_viewer_draw_selection_overlay(trace_viewer_t* tv,
                                                ig_draw_list_t* draw_list,
                                                ig_vec2_t pos, ig_vec2_t size,
                                                const theme_t* theme,
                                                bool draw_duration_text) {
  const selection_overlay_layout_t* lo = &tv->selection_layout;
  if (!lo->active) return;

  float dim_l_x1 = pos.x;
  float dim_l_x2 = lo->x1;
  if (dim_l_x2 < pos.x) dim_l_x2 = pos.x;
  if (dim_l_x2 > pos.x + size.x) dim_l_x2 = pos.x + size.x;

  float dim_r_x1 = lo->x2;
  if (dim_r_x1 < pos.x) dim_r_x1 = pos.x;
  if (dim_r_x1 > pos.x + size.x) dim_r_x1 = pos.x + size.x;
  float dim_r_x2 = pos.x + size.x;

  if (dim_l_x1 < dim_l_x2) {
    ig_draw_list_add_rect_filled(draw_list, (ig_vec2_t){dim_l_x1, pos.y},
                                 (ig_vec2_t){dim_l_x2, pos.y + size.y},
                                 theme->timeline_selection_bg);
  }
  if (dim_r_x1 < dim_r_x2) {
    ig_draw_list_add_rect_filled(draw_list, (ig_vec2_t){dim_r_x1, pos.y},
                                 (ig_vec2_t){dim_r_x2, pos.y + size.y},
                                 theme->timeline_selection_bg);
  }

  float draw_x1 = lo->x1;
  float draw_x2 = lo->x2 - 1.0f;

  if (lo->x1 >= pos.x && lo->x1 <= pos.x + size.x) {
    ig_draw_list_add_line(draw_list, (ig_vec2_t){draw_x1, pos.y},
                          (ig_vec2_t){draw_x1, pos.y + size.y},
                          theme->timeline_selection_line, 1.0f);
  }
  if (lo->x2 >= pos.x && lo->x2 <= pos.x + size.x) {
    ig_draw_list_add_line(draw_list, (ig_vec2_t){draw_x2, pos.y},
                          (ig_vec2_t){draw_x2, pos.y + size.y},
                          theme->timeline_selection_line, 1.0f);
  }

  if (draw_duration_text) {
    ig_vec2_t text_size = ig_calc_text_size(lo->duration_label);
    float text_x = (lo->x1 + lo->x2) * 0.5f - text_size.x * 0.5f;
    float text_y = pos.y + size.y / 3.0f - text_size.y * 0.5f;

    text_x =
        max(pos.x + 5.0f, min(text_x, pos.x + size.x - text_size.x - 5.0f));

    float padding_x = 4.0f;
    float padding_y = 2.0f;
    ig_vec2_t bg_min = {text_x - padding_x, text_y - padding_y};
    ig_vec2_t bg_max = {text_x + text_size.x + padding_x,
                        text_y + text_size.y + padding_y};

    ig_draw_list_add_rect_filled(draw_list, bg_min, bg_max,
                                 theme->timeline_selection_text_bg);
    ig_draw_list_add_rect(draw_list, bg_min, bg_max,
                          theme->timeline_selection_line, 4.0f, 0, 1.0f);

    ig_draw_list_add_text_simple(draw_list, (ig_vec2_t){text_x, text_y},
                                 theme->timeline_selection_text,
                                 lo->duration_label, nullptr);

    float line_y = text_y + text_size.y * 0.5f;
    float arrow_size = 5.0f;
    uint32_t line_col = theme->timeline_selection_line;

    float left_line_end_x = text_x - 5.0f;
    if (left_line_end_x > draw_x1) {
      ig_draw_list_add_line(draw_list, (ig_vec2_t){draw_x1, line_y},
                            (ig_vec2_t){left_line_end_x, line_y}, line_col,
                            1.0f);
      ig_draw_list_add_line(
          draw_list, (ig_vec2_t){draw_x1, line_y},
          (ig_vec2_t){draw_x1 + arrow_size, line_y - arrow_size}, line_col,
          1.0f);
      ig_draw_list_add_line(
          draw_list, (ig_vec2_t){draw_x1, line_y},
          (ig_vec2_t){draw_x1 + arrow_size, line_y + arrow_size}, line_col,
          1.0f);
    }

    float right_line_start_x = text_x + text_size.x + 5.0f;
    if (right_line_start_x < draw_x2) {
      ig_draw_list_add_line(draw_list, (ig_vec2_t){right_line_start_x, line_y},
                            (ig_vec2_t){draw_x2, line_y}, line_col, 1.0f);
      ig_draw_list_add_line(
          draw_list, (ig_vec2_t){draw_x2, line_y},
          (ig_vec2_t){draw_x2 - arrow_size, line_y - arrow_size}, line_col,
          1.0f);
      ig_draw_list_add_line(
          draw_list, (ig_vec2_t){draw_x2, line_y},
          (ig_vec2_t){draw_x2 - arrow_size, line_y + arrow_size}, line_col,
          1.0f);
    }
  }
}

static void trace_viewer_draw_event(trace_viewer_t* tv, trace_data_t* td,
                                    ig_draw_list_t* draw_list, float x1,
                                    float x2, float y1, float y2, uint32_t col,
                                    bool is_selected, bool is_focused,
                                    string_ref_t name_ref, float inner_width,
                                    float tracks_canvas_pos_x,
                                    const theme_t* theme) {
  (void)tv;
  float lane_height = y2 - y1 + 1.0f;

  float border_thickness = 1.0f;
  float event_width = x2 - x1;
  bool draw_border = (event_width > TRACK_MIN_EVENT_WIDTH + 0.01f);

  ig_draw_list_add_rect_filled(draw_list, (ig_vec2_t){x1, y1},
                               (ig_vec2_t){x2, y2}, col);

  if (is_focused || is_selected || draw_border) {
    uint32_t border_col = theme->event_border;
    if (is_focused) {
      border_col = theme->event_border_focused;
      border_thickness = 3.0f;
    } else if (is_selected) {
      border_col = theme->event_border_selected;
    }

    ig_draw_list_add_rect(draw_list, (ig_vec2_t){x1, y1}, (ig_vec2_t){x2, y2},
                          border_col, 0.0f, 0, border_thickness);
  }

  if (name_ref != 0) {
    float padding_h = 6.0f;

    if (event_width > padding_h * 2.0f + 10.0f) {
      float visible_x1 = max(x1, tracks_canvas_pos_x);
      float visible_x2 = min(x2, tracks_canvas_pos_x + inner_width);

      if (visible_x2 > visible_x1) {
        string_t name = trace_data_get_string(td, name_ref);
        if (name.len > 0) {
          uint32_t text_col = theme->event_text;
          if (is_focused) {
            text_col = theme->event_text_focused;
          } else if (is_selected) {
            text_col = theme->event_text_selected;
          }
          float event_font_size = ig_get_font_size();
          float text_y = y1 + (lane_height - event_font_size) * 0.5f;

          float text_width =
              ig_font_calc_text_size_a(ig_get_font(), event_font_size, FLT_MAX,
                                       0.0f, name.ptr, name.ptr + name.len)
                  .x;

          float text_x =
              max(x1 + padding_h, x1 + (event_width - text_width) * 0.5f);

          ig_vec4_t fine_clip_rect = {visible_x1 + padding_h, y1,
                                      visible_x2 - padding_h, y2};

          ig_draw_list_add_text(draw_list, ig_get_font(), event_font_size,
                                (ig_vec2_t){text_x, text_y}, text_col, name.ptr,
                                name.ptr + name.len, 0.0f, &fine_clip_rect);
        }
      }
    }
  }
}

static void trace_viewer_details_add_row(const char* field_label,
                                         string_t value_str,
                                         const char* btn_id_suffix,
                                         bool show_copy_buttons, uint32_t color,
                                         bool has_color,
                                         allocator_t allocator) {
  ig_table_next_row();

  // Column 0: Label
  ig_table_next_column();
  if (has_color) {
    ig_vec2_t p_col = ig_get_cursor_screen_pos();
    float sz_col = ig_get_text_line_height() * 0.7f;
    float offset_col = (ig_get_text_line_height() - sz_col) * 0.5f;

    ig_draw_list_t* dl = ig_get_window_draw_list();
    ig_vec2_t p_min = {p_col.x, p_col.y + offset_col};
    ig_vec2_t p_max = {p_col.x + sz_col, p_col.y + offset_col + sz_col};
    ig_draw_list_add_rect_filled(dl, p_min, p_max, color);

    ig_set_cursor_pos_x(ig_get_cursor_pos_x() + sz_col + 4.0f);
  }
  ig_text_disabled("%s", field_label);

  // Column 1: Value
  ig_table_next_column();
  if (show_copy_buttons) {
    ig_text_wrapped("%.*s", (int)value_str.len, value_str.ptr);
  } else {
    ig_text_unformatted(value_str.ptr, value_str.ptr + value_str.len);
  }

  // Column 2: Action
  ig_table_next_column();
  if (show_copy_buttons && btn_id_suffix) {
    char btn_label[64];
    snprintf(btn_label, sizeof(btn_label), "Copy##%s", btn_id_suffix);
    if (ig_small_button(btn_label)) {
      trace_viewer_set_clipboard_string(value_str, allocator);
    }
  }
}

static void trace_viewer_draw_event_properties(const trace_data_t* td,
                                               const trace_event_persisted_t* e,
                                               double viewport_min_ts,
                                               bool show_copy_buttons,
                                               const track_t* t,
                                               allocator_t allocator) {
  string_t name = trace_data_get_string(td, e->name_ref);
  string_t cat = trace_data_get_string(td, e->cat_ref);
  string_t ph = trace_data_get_string(td, e->ph_ref);

  ig_spacing();

  ig_table_flags_t table_flags = IG_TABLE_FLAGS_NO_SAVED_SETTINGS;
  if (!show_copy_buttons) {
    table_flags |= IG_TABLE_FLAGS_SIZING_FIXED_FIT;
  }

  if (ig_begin_table("##focused_event_table_unified", 3, table_flags,
                     (ig_vec2_t){0.0f, 0.0f}, 0.0f)) {
    ig_table_setup_column("Label", IG_TABLE_COLUMN_FLAGS_WIDTH_FIXED, 0.0f, 0);
    if (show_copy_buttons) {
      ig_table_setup_column("Value", IG_TABLE_COLUMN_FLAGS_WIDTH_STRETCH, 0.0f,
                            0);
    } else {
      ig_table_setup_column("Value", IG_TABLE_COLUMN_FLAGS_WIDTH_FIXED, 0.0f,
                            0);
    }
    ig_table_setup_column("Action", IG_TABLE_COLUMN_FLAGS_WIDTH_FIXED, 0.0f, 0);

    // 1. Header Row / Event Name
    if (t != nullptr && t->type == TRACK_TYPE_COUNTER) {
      string_t t_name = trace_data_get_string(td, t->name_ref);
      trace_viewer_details_add_row("Name", t_name, "Name", show_copy_buttons, 0,
                                   false, allocator);
    } else {
      bool is_counter = (ph.len == 1 && ph.ptr[0] == 'C');
      if (!is_counter) {
        trace_viewer_details_add_row("Name", name, "Name", show_copy_buttons, 0,
                                     false, allocator);
      } else {
        trace_viewer_details_add_row("Name", (string_t){"(Counter Event)", 15},
                                     "Name", show_copy_buttons, 0, false,
                                     allocator);
      }
    }

    if (cat.len > 0) {
      trace_viewer_details_add_row("Category", cat, "Category",
                                   show_copy_buttons, 0, false, allocator);
    }

    char ts_buf[32];
    format_duration(ts_buf, sizeof(ts_buf), (double)e->ts - viewport_min_ts,
                    0.0);
    trace_viewer_details_add_row("Start", (string_t){ts_buf, strlen(ts_buf)},
                                 nullptr, show_copy_buttons, 0, false,
                                 allocator);

    if (e->dur > 0) {
      char dur_buf[32];
      format_duration(dur_buf, sizeof(dur_buf), (double)e->dur, 0.0);
      trace_viewer_details_add_row(
          "Duration", (string_t){dur_buf, strlen(dur_buf)}, nullptr,
          show_copy_buttons, 0, false, allocator);
    }

    if (t == nullptr || t->type == TRACK_TYPE_THREAD) {
      char pid_tid_buf[64];
      snprintf(pid_tid_buf, sizeof(pid_tid_buf), "%d / %d", e->pid, e->tid);
      trace_viewer_details_add_row(
          "PID / TID", (string_t){pid_tid_buf, strlen(pid_tid_buf)}, nullptr,
          show_copy_buttons, 0, false, allocator);
    }

    size_t skip_count = 0;
    const string_ref_t* skip_keys = nullptr;

    if (t != nullptr && t->type == TRACK_TYPE_COUNTER) {
      bool single_series_redundant = false;
      string_t t_name = trace_data_get_string(td, t->name_ref);

      if (t->counter_series.len == 1) {
        string_t s_name = trace_data_get_string(
            td, ((const string_ref_t*)t->counter_series.ptr)[0]);
        bool match = false;
        if (s_name.len == t_name.len &&
            memcmp(s_name.ptr, t_name.ptr, s_name.len) == 0)
          match = true;
        else if (s_name.len == 0)
          match = true;
        else if (s_name.len == 5 && memcmp(s_name.ptr, "value", 5) == 0)
          match = true;
        else if (s_name.len == 5 && memcmp(s_name.ptr, "Value", 5) == 0)
          match = true;

        if (match) {
          single_series_redundant = true;
        }
      }

      if (single_series_redundant) {
        double val = 0.0;
        string_ref_t val_s_ref = 0;
        string_ref_t target_key =
            ((const string_ref_t*)t->counter_series.ptr)[0];
        for (uint32_t arg_k = 0; arg_k < e->args_count; arg_k++) {
          const trace_arg_persisted_t* arg = array_list_get(
              &td->args, const trace_arg_persisted_t, e->args_offset + arg_k);
          if (arg->key_ref == target_key) {
            val = arg->val_double;
            val_s_ref = arg->val_ref;
            break;
          }
        }

        ig_table_next_row();
        ig_table_next_column();
        ig_text_disabled("Value");

        ig_table_next_column();
        if (val_s_ref != 0) {
          string_t val_s = trace_data_get_string(td, val_s_ref);
          if (show_copy_buttons) {
            ig_text_wrapped("%.*s", (int)val_s.len, val_s.ptr);
          } else {
            ig_text_unformatted(val_s.ptr, val_s.ptr + val_s.len);
          }
        } else {
          ig_text("%.2f", val);
        }

        ig_table_next_column();
      } else {
        double total = 0.0;
        bool has_total_series = false;

        const string_ref_t* series_ptr =
            (const string_ref_t*)t->counter_series.ptr;
        for (size_t s_i = 0; s_i < t->counter_series.len; s_i++) {
          size_t s_idx = t->counter_series.len - 1 - s_i;
          string_ref_t key_ref = series_ptr[s_idx];

          string_t s_name = trace_data_get_string(td, key_ref);
          if ((s_name.len == 5 && memcmp(s_name.ptr, "total", 5) == 0) ||
              (s_name.len == 5 && memcmp(s_name.ptr, "Total", 5) == 0)) {
            has_total_series = true;
          }

          double val = 0.0;
          string_ref_t val_s_ref = 0;
          for (uint32_t arg_k = 0; arg_k < e->args_count; arg_k++) {
            const trace_arg_persisted_t* arg = array_list_get(
                &td->args, const trace_arg_persisted_t, e->args_offset + arg_k);
            if (arg->key_ref == key_ref) {
              val = arg->val_double;
              val_s_ref = arg->val_ref;
              break;
            }
          }

          char series_val_buf[64];
          if (val_s_ref != 0) {
            string_t val_s = trace_data_get_string(td, val_s_ref);
            snprintf(series_val_buf, sizeof(series_val_buf), "%.*s",
                     (int)val_s.len, val_s.ptr);
          } else {
            snprintf(series_val_buf, sizeof(series_val_buf), "%.2f", val);
          }

          char name_buf[128];
          size_t name_len = s_name.len < 127 ? s_name.len : 127;
          memcpy(name_buf, s_name.ptr, name_len);
          name_buf[name_len] = '\0';

          trace_viewer_details_add_row(
              name_buf, (string_t){series_val_buf, strlen(series_val_buf)},
              nullptr, show_copy_buttons,
              ((const uint32_t*)t->counter_colors.ptr)[s_idx], true, allocator);

          total += val;
        }

        if (t->counter_series.len > 1 && !has_total_series) {
          char total_buf[64];
          snprintf(total_buf, sizeof(total_buf), "%.2f", total);
          trace_viewer_details_add_row(
              "Total", (string_t){total_buf, strlen(total_buf)}, nullptr,
              show_copy_buttons, 0, false, allocator);
        }
      }

      skip_keys = (const string_ref_t*)t->counter_series.ptr;
      skip_count = t->counter_series.len;
    }

    // Event arguments
    for (uint32_t k = 0; k < e->args_count; k++) {
      const trace_arg_persisted_t* arg = array_list_get(
          &td->args, const trace_arg_persisted_t, e->args_offset + k);
      bool skip = false;
      for (size_t i = 0; i < skip_count; i++) {
        if (arg->key_ref == skip_keys[i]) {
          skip = true;
          break;
        }
      }
      if (skip) continue;

      string_t key = trace_data_get_string(td, arg->key_ref);
      char key_buf[128];
      size_t key_len = key.len < 127 ? key.len : 127;
      memcpy(key_buf, key.ptr, key_len);
      key_buf[key_len] = '\0';

      char val_buf[256];
      bool is_str = (arg->val_ref != 0);

      if (is_str) {
        string_t val = trace_data_get_string(td, arg->val_ref);
        trace_viewer_details_add_row(key_buf, val, key_buf, show_copy_buttons,
                                     0, false, allocator);
      } else {
        snprintf(val_buf, sizeof(val_buf), "%.2f", arg->val_double);
        trace_viewer_details_add_row(
            key_buf, (string_t){val_buf, strlen(val_buf)}, nullptr,
            show_copy_buttons, 0, false, allocator);
      }
    }

    ig_end_table();
  }
}

static void trace_viewer_draw_tooltip(trace_viewer_t* tv, trace_data_t* td,
                                      const hover_match_t* best_hm,
                                      float inner_width,
                                      float tracks_canvas_pos_x,
                                      allocator_t allocator) {
  const track_render_block_t* rb = &best_hm->rb;
  if (rb->count == 1) {
    const track_t* t =
        array_list_get(&tv->tracks, const track_t, best_hm->track_idx);
    const trace_event_persisted_t* e = array_list_get(
        &td->events, const trace_event_persisted_t, rb->event_idx);

    ig_push_style_var(IG_STYLE_VAR_WINDOW_PADDING, (ig_vec2_t){10.0f, 10.0f});
    ig_begin_tooltip();

    if (t->type == TRACK_TYPE_COUNTER) {
      trace_viewer_draw_event_properties(td, e, (double)tv->viewport.min_ts,
                                         false, t, allocator);
    } else {
      trace_viewer_draw_event_properties(td, e, (double)tv->viewport.min_ts,
                                         false, t, allocator);
    }
    ig_end_tooltip();
    ig_pop_style_var(1);
  } else if (rb->count > 1) {
    ig_push_style_var(IG_STYLE_VAR_WINDOW_PADDING, (ig_vec2_t){10.0f, 10.0f});
    ig_begin_tooltip();
    ig_text("%u merged events", rb->count);

    double ts1 =
        trace_viewer_px_to_ts(tv->viewport.start_time, tv->viewport.end_time,
                              inner_width, tracks_canvas_pos_x, rb->x1);
    double ts2 =
        trace_viewer_px_to_ts(tv->viewport.start_time, tv->viewport.end_time,
                              inner_width, tracks_canvas_pos_x, rb->x2);
    char dur_buf[32];
    format_duration(dur_buf, sizeof(dur_buf), ts2 - ts1, 0.0);
    ig_text("Duration: %s", dur_buf);

    ig_end_tooltip();
    ig_pop_style_var(1);
  }
}

static void trace_viewer_draw_counter_track(
    trace_viewer_t* tv, trace_data_t* td, ig_draw_list_t* draw_list,
    const track_t* t, ig_vec2_t pos, float width, float height,
    double viewport_start, double viewport_end, const theme_t* theme,
    ig_vec2_t mouse_pos, bool track_list_hovered, int64_t focused_event_idx,
    allocator_t allocator) {
  if (t->event_indices.len == 0) return;

  double duration = viewport_end - viewport_start;
  if (duration <= 0) return;

  double max_total = t->counter_max_total;
  if (max_total <= 0) max_total = 1.0;

  track_renderer_state_t* state = &tv->track_renderer_state;
  array_list_resize(&state->counter_current_values, t->counter_series.len,
                    sizeof(double), allocator);

  track_compute_counter_render_blocks(t, td, viewport_start, viewport_end,
                                      width, pos.x, focused_event_idx, state,
                                      &tv->counter_render_blocks, allocator);

  if (tv->counter_render_blocks.len == 0) return;

  size_t n_blocks = tv->counter_render_blocks.len;
  size_t n_series = t->counter_series.len;
  array_list_resize(&state->counter_visual_offsets, n_blocks * (n_series + 1),
                    sizeof(float), allocator);

  float* visual_offsets = (float*)state->counter_visual_offsets.ptr;
  double* counter_peaks = (double*)state->counter_peaks.ptr;

  float min_h = 1.0f;
  for (size_t i = 0; i < n_blocks; i++) {
    float current_y_offset_px = 0.0f;
    visual_offsets[i * (n_series + 1)] = 0.0f;
    for (size_t s_idx = 0; s_idx < n_series; s_idx++) {
      double val = counter_peaks[i * n_series + s_idx];
      double visual_val = max(val, (double)min_h / height * max_total);
      current_y_offset_px += (float)(visual_val / max_total * height);
      visual_offsets[i * (n_series + 1) + s_idx + 1] = current_y_offset_px;
    }
  }

  // Pass 1: Filled areas and hover highlight
  const counter_render_block_t* blocks =
      (const counter_render_block_t*)tv->counter_render_blocks.ptr;
  for (size_t i = 0; i < n_blocks; i++) {
    const counter_render_block_t* rb = &blocks[i];

    bool hovered = track_list_hovered && mouse_pos.x >= rb->x1 &&
                   mouse_pos.x < rb->x2 && mouse_pos.y >= pos.y &&
                   mouse_pos.y < pos.y + height;

    // Draw stack
    for (size_t s_idx = 0; s_idx < n_series; s_idx++) {
      float off_bottom = visual_offsets[i * (n_series + 1) + s_idx];
      float off_top = visual_offsets[i * (n_series + 1) + s_idx + 1];

      float y_top = pos.y + height - off_top;
      float y_bottom = pos.y + height - off_bottom;

      ig_draw_list_add_rect_filled(
          draw_list, (ig_vec2_t){rb->x1, y_top}, (ig_vec2_t){rb->x2, y_bottom},
          ((const uint32_t*)t->counter_colors.ptr)[s_idx]);
    }

    if (hovered) {
      ig_draw_list_add_rect_filled(draw_list, (ig_vec2_t){rb->x1, pos.y},
                                   (ig_vec2_t){rb->x2, pos.y + height},
                                   IG_COL32(255, 255, 255, 30));
    }

    if (rb->is_focused) {
      ig_draw_list_add_rect_filled(draw_list, (ig_vec2_t){rb->x1, pos.y},
                                   (ig_vec2_t){rb->x2, pos.y + height},
                                   theme->event_focused_bg);
    }
  }

  // Pass 2: Step lines (no anti-aliasing for sharp lines)
  ig_draw_list_flags_t old_flags = ig_draw_list_get_flags(draw_list);
  ig_draw_list_set_flags(draw_list,
                         old_flags & ~IG_DRAW_LIST_FLAGS_ANTI_ALIASED_LINES);

  for (size_t s_idx = 0; s_idx < n_series; s_idx++) {
    float prev_y_top = -1.0f;
    for (size_t i = 0; i < n_blocks; i++) {
      const counter_render_block_t* rb = &blocks[i];

      float off_top = visual_offsets[i * (n_series + 1) + s_idx + 1];
      float y_top = pos.y + height - off_top;

      uint32_t line_col = theme->event_border;
      float thickness = 1.0f;
      if (rb->is_focused) {
        line_col = theme->event_border_focused;
        thickness = 3.0f;
      } else if (rb->is_selected) {
        line_col = theme->event_border_selected;
      }

      // Horizontal segment
      ig_draw_list_add_line(draw_list, (ig_vec2_t){rb->x1, y_top},
                            (ig_vec2_t){rb->x2, y_top}, line_col, thickness);

      // Vertical segment (connect to previous bucket)
      if (prev_y_top != -1.0f && y_top != prev_y_top) {
        ig_draw_list_add_line(draw_list, (ig_vec2_t){rb->x1, prev_y_top},
                              (ig_vec2_t){rb->x1, y_top}, theme->event_border,
                              1.0f);
      }
      prev_y_top = y_top;
    }
  }

  ig_draw_list_set_flags(draw_list, old_flags);
}

static void trace_viewer_box_select_update(trace_viewer_t* tv, trace_data_t* td,
                                           allocator_t allocator) {
  float x1 = tv->box_select_start.x;
  float x2 = tv->box_select_end.x;
  float y1 = tv->box_select_start.y;
  float y2 = tv->box_select_end.y;
  if (x1 > x2) swap(float, x1, x2);
  if (y1 > y2) swap(float, y1, y2);

  array_list_clear(&tv->selected_event_indices);

  double ts1 =
      trace_viewer_px_to_ts(tv->viewport.start_time, tv->viewport.end_time,
                            tv->last_inner_width, tv->last_tracks_x, x1);
  double ts2 =
      trace_viewer_px_to_ts(tv->viewport.start_time, tv->viewport.end_time,
                            tv->last_inner_width, tv->last_tracks_x, x2);

  const track_t* tracks = (const track_t*)tv->tracks.ptr;
  const track_view_info_t* track_infos =
      (const track_view_info_t*)tv->track_infos.ptr;

  for (size_t i = 0; i < tv->tracks.len; i++) {
    const track_t* t = &tracks[i];
    const track_view_info_t* vi = &track_infos[i];
    if (!vi->visible) continue;

    // Check track Y overlap
    if (vi->y + vi->height < y1 || vi->y > y2) continue;

    if (t->type == TRACK_TYPE_THREAD) {
      const size_t* event_indices_ptr = (const size_t*)t->event_indices.ptr;
      const size_t* it_start =
          event_indices_ptr +
          trace_data_events_lower_bound(
              event_indices_ptr, t->event_indices.len,
              (const trace_event_persisted_t*)td->events.ptr,
              (int64_t)ts1 - t->max_dur);
      const uint32_t* depths_ptr = (const uint32_t*)t->depths.ptr;
      const trace_event_persisted_t* events_ptr =
          (const trace_event_persisted_t*)td->events.ptr;

      for (const size_t* it = it_start;
           it < event_indices_ptr + t->event_indices.len; ++it) {
        const trace_event_persisted_t* e = &events_ptr[*it];
        if (e->ts > (int64_t)ts2) break;

        // Explicitly check for time overlap since it_start is conservative
        if (e->ts + e->dur < (int64_t)ts1) continue;

        size_t event_idx_in_track = (size_t)(it - event_indices_ptr);
        uint32_t depth = depths_ptr[event_idx_in_track];
        float event_y1 = vi->y + (float)(depth + 1) * tv->last_lane_height;
        float event_y2 = event_y1 + tv->last_lane_height;

        if (event_y2 < y1 || event_y1 > y2) continue;

        *array_list_push(&tv->selected_event_indices, int64_t, allocator) =
            (int64_t)*it;
      }
    } else {
      // Counter track
      if (t->event_indices.len > 0) {
        float chart_y1 = vi->y + tv->last_lane_height;
        float chart_y2 = vi->y + vi->height;
        if (!(chart_y2 < y1 || chart_y1 > y2)) {
          const size_t* event_indices_ptr = (const size_t*)t->event_indices.ptr;
          const size_t* it_start =
              event_indices_ptr +
              trace_data_events_lower_bound(
                  event_indices_ptr, t->event_indices.len,
                  (const trace_event_persisted_t*)td->events.ptr, (int64_t)ts1);
          const trace_event_persisted_t* events_ptr =
              (const trace_event_persisted_t*)td->events.ptr;

          for (const size_t* it = it_start;
               it < event_indices_ptr + t->event_indices.len; ++it) {
            if (events_ptr[*it].ts > (int64_t)ts2) break;
            *array_list_push(&tv->selected_event_indices, int64_t, allocator) =
                (int64_t)*it;
          }
        }
      }
    }
  }

  // Sort for binary search
  if (tv->selected_event_indices.len > 0) {
    qsort(tv->selected_event_indices.ptr, tv->selected_event_indices.len,
          sizeof(int64_t), trace_viewer_int64_compare);
  }

  LOG_INFO(
      "SYNC BOX SELECT COMPLETED! track size = %zu, found %zu selected event "
      "indices",
      tv->tracks.len, tv->selected_event_indices.len);

  tv->selected_histogram_bucket = -1;

  array_list_clear(&tv->filtered_event_indices);
  if (tv->selected_event_indices.len > 0) {
    int64_t* dest_filtered =
        array_list_append(&tv->filtered_event_indices,
                          tv->selected_event_indices.len, int64_t, allocator);
    memcpy(dest_filtered, tv->selected_event_indices.ptr,
           tv->selected_event_indices.len * sizeof(int64_t));
  }

  {
    pthread_mutex_lock(&tv->search.mutex);
    array_list_clear(&tv->search.pending_results);
    if (tv->selected_event_indices.len > 0) {
      int64_t* dest_pending = array_list_append(&tv->search.pending_results,
                                                tv->selected_event_indices.len,
                                                int64_t, tv->search.allocator);
      memcpy(dest_pending, tv->selected_event_indices.ptr,
             tv->selected_event_indices.len * sizeof(int64_t));
    }
    atomic_store(&tv->search.new_box_selection_available, true);
    atomic_store(&tv->search.results_ready, false);
    pthread_mutex_unlock(&tv->search.mutex);
  }

  tv->selected_events_dirty = true;
  platform_submit_job(trace_viewer_search_job, &tv->search);
}

static void trace_viewer_zoom_to_event(trace_viewer_t* tv,
                                       const trace_event_persisted_t* e) {
  double event_start = (double)e->ts;
  double event_end = (double)(e->ts + e->dur);

  if (e->dur > 0) {
    double event_dur = event_end - event_start;
    double padding = event_dur * 0.05;
    double target_dur = event_dur + padding * 2.0;
    if (target_dur < TRACE_VIEWER_MIN_ZOOM_DURATION) {
      target_dur = TRACE_VIEWER_MIN_ZOOM_DURATION;
      padding = (target_dur - event_dur) * 0.5;
    }

    tv->viewport.start_time = event_start - padding;
    tv->viewport.end_time = event_start - padding + target_dur;

    tv->selection_active = true;
    tv->selection_start_time = event_start;
    tv->selection_end_time = event_end;
  } else {
    double current_dur = tv->viewport.end_time - tv->viewport.start_time;
    tv->viewport.start_time = event_start - current_dur * 0.5;
    tv->viewport.end_time = tv->viewport.start_time + current_dur;
    tv->selection_active = false;
  }

  tv->selection_drag_mode = INTERACTION_DRAG_MODE_NONE;
  tv->request_scroll_to_focused_event = true;
}

void trace_viewer_calculate_histogram(const array_list_t* results,
                                      const trace_data_t* td,
                                      duration_histogram_t* h) {
  h->num_buckets = 0;
  h->max_bucket_count = 0;
  h->total_count = (uint32_t)results->len;
  h->has_non_zero_durations = false;

  const int64_t* results_ptr = (const int64_t*)results->ptr;
  const trace_event_persisted_t* events_ptr =
      (const trace_event_persisted_t*)td->events.ptr;

  if (results->len > 0) {
    int64_t min_dur = -1;
    int64_t max_dur = -1;
    uint32_t zero_count = 0;

    for (size_t i = 0; i < results->len; i++) {
      size_t idx = (size_t)results_ptr[i];
      if (idx >= td->events.len) continue;
      const trace_event_persisted_t* e = &events_ptr[idx];
      int64_t d = e->dur;

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
      if (min_dur > 0 && max_dur > 0 &&
          (double)max_dur / (double)min_dur > 100.0) {
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

        if (j > 0 && L[j] <= L[j - 1]) {
          L[j] = L[j - 1] + 1;
        }
      }

      for (int j = 0; j < k_bins; j++) {
        duration_histogram_bucket_t* b = &h->buckets[start_bin_idx + j];
        b->min_dur = L[j];
        b->max_dur = L[j + 1] - 1;
        b->count = 0;
      }

      for (size_t i = 0; i < results->len; i++) {
        size_t idx = (size_t)results_ptr[i];
        if (idx >= td->events.len) continue;
        const trace_event_persisted_t* e = &events_ptr[idx];
        int64_t d = e->dur;

        if (d <= 0) continue;

        for (int b_idx = start_bin_idx; b_idx < h->num_buckets; b_idx++) {
          duration_histogram_bucket_t* b = &h->buckets[b_idx];
          if (d >= b->min_dur && d <= b->max_dur) {
            b->count++;
            if (b->count > h->max_bucket_count) {
              h->max_bucket_count = b->count;
            }
            break;
          }
        }
      }
    }
  }
}

void trace_viewer_step(trace_viewer_t* tv, trace_data_t* td,
                       const trace_viewer_input_t* input,
                       allocator_t allocator) {
  // 0. Handle focus requests
  if (tv->target_focused_event_idx != -1) {
    size_t event_idx = (size_t)tv->target_focused_event_idx;
    if (event_idx < td->events.len) {
      const trace_event_persisted_t* events =
          (const trace_event_persisted_t*)td->events.ptr;
      const trace_event_persisted_t* e = &events[event_idx];
      tv->focused_event_idx = (int64_t)event_idx;
      tv->show_details_panel = true;
      trace_viewer_zoom_to_event(tv, e);
    }
    tv->target_focused_event_idx = -1;
  }

  double current_duration = tv->viewport.end_time - tv->viewport.start_time;
  if (current_duration <= 0) current_duration = 1.0;

  float tracks_origin_x = input->canvas_x;
  float tracks_origin_y = input->canvas_y + input->ruler_height;
  float tracks_inner_width = input->canvas_width;
  float tracks_inner_height = input->canvas_height - input->ruler_height;
  if (tracks_inner_width <= 0) tracks_inner_width = 1.0f;
  tv->last_tracks_x = tracks_origin_x;
  tv->last_tracks_y = tracks_origin_y;
  tv->last_inner_width = tracks_inner_width;
  tv->last_inner_height = tracks_inner_height;
  tv->last_lane_height = input->lane_height;

  double mouse_ts = trace_viewer_px_to_ts(
      tv->viewport.start_time, tv->viewport.end_time, tracks_inner_width,
      tracks_origin_x, input->mouse_x);

  bool mouse_in_tracks_content =
      input->mouse_x >= tracks_origin_x &&
      input->mouse_x < tracks_origin_x + tracks_inner_width &&
      input->mouse_y >= tracks_origin_y &&
      input->mouse_y < tracks_origin_y + tracks_inner_height;

  // 1. Snapping Initialization
  float snap_threshold_px = 5.0f;
  trace_viewer_snapping_reset(tv, mouse_ts, snap_threshold_px);

  // 2. Interaction Logic - Pre-pass
  double threshold_ts =
      ((double)tv->snap_threshold_px / (double)tracks_inner_width) *
      current_duration;

  SelectionProximity proximity =
      trace_viewer_selection_check_proximity(tv, mouse_ts, threshold_ts);

  bool mouse_in_selection =
      trace_viewer_selection_is_mouse_inside(tv, mouse_ts);
  bool interaction_ignored = tv->ignore_next_release;

  // Ruler Interaction
  if (input->ruler_active) {
    if (!interaction_ignored && input->ruler_activated) {
      if (proximity.near_start) {
        tv->selection_drag_mode = INTERACTION_DRAG_MODE_RULER_START;
      } else if (proximity.near_end) {
        tv->selection_drag_mode = INTERACTION_DRAG_MODE_RULER_END;
      } else {
        tv->selection_drag_mode = INTERACTION_DRAG_MODE_RULER_NEW;
      }
    }

    if (tv->selection_drag_mode == INTERACTION_DRAG_MODE_RULER_NEW) {
      if (fabsf(input->drag_delta_x) >= input->drag_threshold) {
        tv->selection_active = true;
        tv->selection_start_time = trace_viewer_px_to_ts(
            tv->viewport.start_time, tv->viewport.end_time, tracks_inner_width,
            tracks_origin_x, input->click_x);
      }
    }
  } else {
    if (!interaction_ignored && input->ruler_deactivated) {
      if (fabsf(input->drag_delta_x) < input->drag_threshold) {
        if (tv->selection_drag_mode == INTERACTION_DRAG_MODE_RULER_NEW) {
          tv->selection_active = false;
        }
      }
    }

    if (tv->selection_drag_mode == INTERACTION_DRAG_MODE_RULER_NEW ||
        tv->selection_drag_mode == INTERACTION_DRAG_MODE_RULER_START ||
        tv->selection_drag_mode == INTERACTION_DRAG_MODE_RULER_END) {
      tv->selection_drag_mode = INTERACTION_DRAG_MODE_NONE;
    }
  }

  // Tracks Interaction
  if (input->tracks_hovered && !input->ruler_active) {
    if (!interaction_ignored &&
        tv->selection_drag_mode == INTERACTION_DRAG_MODE_NONE) {
      if (input->is_shift_down && input->is_mouse_clicked) {
        tv->selection_drag_mode = INTERACTION_DRAG_MODE_BOX_SELECT;
        tv->box_select_start = (ig_vec2_t){input->mouse_x, input->mouse_y};
        tv->box_select_end = tv->box_select_start;
      } else if (tv->selection_active && input->is_mouse_clicked &&
                 (proximity.near_start || proximity.near_end)) {
        tv->selection_drag_mode = proximity.near_start
                                      ? INTERACTION_DRAG_MODE_TRACKS_START
                                      : INTERACTION_DRAG_MODE_TRACKS_END;
      }
    }

    if (tv->selection_drag_mode == INTERACTION_DRAG_MODE_TRACKS_START ||
        tv->selection_drag_mode == INTERACTION_DRAG_MODE_TRACKS_END) {
      if (!input->is_mouse_down) {
        tv->selection_drag_mode = INTERACTION_DRAG_MODE_NONE;
      }
    }

    if (tv->selection_drag_mode == INTERACTION_DRAG_MODE_BOX_SELECT) {
      tv->box_select_end = (ig_vec2_t){input->mouse_x, input->mouse_y};
      if (!input->is_mouse_down) {
        trace_viewer_box_select_update(tv, td, allocator);
        tv->selection_drag_mode = INTERACTION_DRAG_MODE_NONE;
        if (tv->selected_event_indices.len > 0) {
          tv->show_details_panel = true;
        }
      }
    }

    // Zooming
    if (input->mouse_wheel != 0.0f && input->is_ctrl_down) {
      double mouse_x_rel = (double)(input->mouse_x - tracks_origin_x) /
                           (double)tracks_inner_width;
      double zoom_factor =
          (input->mouse_wheel > 0.0f) ? 0.8 : TRACE_VIEWER_MAX_ZOOM_FACTOR;
      double new_duration = current_duration * zoom_factor;

      double trace_duration =
          (double)(tv->viewport.max_ts - tv->viewport.min_ts);
      double max_duration = trace_duration * TRACE_VIEWER_MAX_ZOOM_FACTOR;
      double min_duration = TRACE_VIEWER_MIN_ZOOM_DURATION;

      if (tv->selection_active) {
        double t1 = tv->selection_start_time;
        double t2 = tv->selection_end_time;
        if (t1 > t2) swap(double, t1, t2);
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
        if (t1 > t2) swap(double, t1, t2);
        if (tv->viewport.start_time > t1) tv->viewport.start_time = t1;
        if (tv->viewport.start_time + new_duration < t2)
          tv->viewport.start_time = t2 - new_duration;
      }
      tv->viewport.end_time = tv->viewport.start_time + new_duration;
      current_duration = new_duration;
    }

    // Panning
    if (input->is_mouse_down && !input->is_mouse_clicked &&
        tv->selection_drag_mode == INTERACTION_DRAG_MODE_NONE) {
      double dx = (double)input->mouse_delta_x;
      double dt = (dx / (double)tracks_inner_width) * current_duration;
      tv->viewport.start_time -= dt;

      if (tv->selection_active) {
        double t1 = tv->selection_start_time;
        double t2 = tv->selection_end_time;
        if (t1 > t2) swap(double, t1, t2);
        if (tv->viewport.start_time > t1) tv->viewport.start_time = t1;
        if (tv->viewport.start_time + current_duration < t2)
          tv->viewport.start_time = t2 - current_duration;
      }
      tv->viewport.end_time = tv->viewport.start_time + current_duration;
    }
  }

  // 3. Track Layout and Pass 1: Culling, Naming, Snapping, Hit-testing
  array_list_clear(&tv->hover_matches);

  array_list_resize(&tv->track_infos, tv->tracks.len, sizeof(track_view_info_t),
                    allocator);
  tv->total_tracks_height = 0.0f;

  float counter_track_height = 3.0f * input->lane_height;
  bool track_list_hovered =
      input->tracks_hovered && mouse_in_tracks_content &&
      (mouse_in_selection || input->is_mouse_double_clicked) &&
      tv->selection_drag_mode == INTERACTION_DRAG_MODE_NONE &&
      !proximity.near_start && !proximity.near_end;

  bool is_dragging = (tv->selection_drag_mode != INTERACTION_DRAG_MODE_NONE);
  bool was_drag = (fabsf(input->drag_delta_x) >= input->drag_threshold ||
                   fabsf(input->drag_delta_y) >= input->drag_threshold);
  bool should_snap =
      is_dragging && was_drag &&
      tv->selection_drag_mode != INTERACTION_DRAG_MODE_BOX_SELECT;

  if (tv->selected_events_dirty) {
    track_renderer_update_selection_bitset(
        &tv->track_renderer_state, td, &tv->selected_event_indices, allocator);

    array_list_resize(&tv->vertical_minimap.track_has_selected, tv->tracks.len,
                      sizeof(bool), allocator);
    bool* track_has_selected =
        (bool*)tv->vertical_minimap.track_has_selected.ptr;
    const track_t* tracks = (const track_t*)tv->tracks.ptr;

    for (size_t i = 0; i < tv->tracks.len; i++) {
      const track_t* t = &tracks[i];
      bool has_sel = false;
      const size_t* event_indices_ptr = (const size_t*)t->event_indices.ptr;
      for (size_t j = 0; j < t->event_indices.len; j++) {
        size_t event_idx = event_indices_ptr[j];
        if (event_idx < tv->track_renderer_state.selected_events_bitset.len &&
            ((const uint8_t*)tv->track_renderer_state.selected_events_bitset
                 .ptr)[event_idx] != 0) {
          has_sel = true;
          break;
        }
      }
      track_has_selected[i] = has_sel;
    }

    tv->selected_events_dirty = false;
  }

  track_t* tracks = (track_t*)tv->tracks.ptr;
  track_view_info_t* track_infos = (track_view_info_t*)tv->track_infos.ptr;

  for (size_t i = 0; i < tv->tracks.len; i++) {
    track_t* t = &tracks[i];
    track_view_info_t* vi = &track_infos[i];

    vi->height = (t->type == TRACK_TYPE_COUNTER)
                     ? counter_track_height
                     : (float)(t->max_depth + 2) * input->lane_height;
    vi->y_rel = tv->total_tracks_height;
    vi->y = input->canvas_y + input->ruler_height + tv->total_tracks_height -
            input->tracks_scroll_y;

    const size_t* event_indices_ptr = (const size_t*)t->event_indices.ptr;

    if (tv->request_scroll_to_focused_event) {
      for (size_t j = 0; j < t->event_indices.len; j++) {
        if (event_indices_ptr[j] == (size_t)tv->focused_event_idx) {
          float track_top = tv->total_tracks_height;
          float viewport_height = input->canvas_height - input->ruler_height;
          tv->target_scroll_y =
              track_top - (viewport_height - vi->height) * 0.5f;
          tv->request_scroll_to_focused_event = false;
          break;
        }
      }
    }

    tv->total_tracks_height += vi->height;

    // Frustum culling
    vi->visible =
        (vi->y + vi->height >= input->canvas_y + input->ruler_height &&
         vi->y <= input->canvas_y + input->canvas_height);

    // Format header name
    string_t name_str = trace_data_get_string(td, t->name_ref);
    string_t id_str = trace_data_get_string(td, t->id_ref);

    if (name_str.len == 0) {
      if (t->type == TRACK_TYPE_THREAD) {
        snprintf(vi->name, sizeof(vi->name), "Thread %d", t->tid);
      } else {
        snprintf(vi->name, sizeof(vi->name), "Counter");
      }
    } else if (id_str.len > 0) {
      snprintf(vi->name, sizeof(vi->name), "%.*s (%.*s)", (int)name_str.len,
               name_str.ptr, (int)id_str.len, id_str.ptr);
    } else {
      snprintf(vi->name, sizeof(vi->name), "%.*s", (int)name_str.len,
               name_str.ptr);
    }

    if (vi->visible) {
      if (t->type == TRACK_TYPE_THREAD) {
        track_compute_render_blocks(
            t, td, tv->viewport.start_time, tv->viewport.end_time,
            tracks_inner_width, tracks_origin_x, tv->focused_event_idx,
            &tv->track_renderer_state, &tv->render_blocks, allocator);

        const track_render_block_t* rblocks =
            (const track_render_block_t*)tv->render_blocks.ptr;
        for (size_t k = 0; k < tv->render_blocks.len; k++) {
          const track_render_block_t* rb = &rblocks[k];
          float y1 = vi->y + (float)(rb->depth + 1) * input->lane_height;
          float y2 = y1 + input->lane_height - 1.0f;

          // Snapping
          if (should_snap) {
            double ts1 = trace_viewer_px_to_ts(
                tv->viewport.start_time, tv->viewport.end_time,
                tracks_inner_width, tracks_origin_x, rb->x1);
            trace_viewer_snapping_suggest(tv, ts1, rb->x1, input->mouse_x, y1,
                                          y2);
            double ts2 = trace_viewer_px_to_ts(
                tv->viewport.start_time, tv->viewport.end_time,
                tracks_inner_width, tracks_origin_x, rb->x2);
            trace_viewer_snapping_suggest(tv, ts2, rb->x2, input->mouse_x, y1,
                                          y2);
          }

          // Hit-testing
          if (track_list_hovered && input->mouse_y >= y1 &&
              input->mouse_y < y2 && input->mouse_x >= rb->x1 &&
              input->mouse_x < rb->x2) {
            hover_match_t hm = {i, k, y1, y2, *rb};
            *array_list_push(&tv->hover_matches, hover_match_t, allocator) = hm;
          }
        }
      } else {
        // Counter hit-testing
        track_compute_counter_render_blocks(
            t, td, tv->viewport.start_time, tv->viewport.end_time,
            tracks_inner_width, tracks_origin_x, tv->focused_event_idx,
            &tv->track_renderer_state, &tv->counter_render_blocks, allocator);

        float track_content_y = vi->y + input->lane_height;
        float track_content_h = vi->height - input->lane_height;

        if (track_list_hovered && input->mouse_y >= track_content_y &&
            input->mouse_y < track_content_y + track_content_h) {
          const counter_render_block_t* crblocks =
              (const counter_render_block_t*)tv->counter_render_blocks.ptr;
          for (size_t k = 0; k < tv->counter_render_blocks.len; k++) {
            const counter_render_block_t* rb = &crblocks[k];
            if (input->mouse_x >= rb->x1 && input->mouse_x < rb->x2) {
              hover_match_t hm = {i,
                                  k,
                                  track_content_y,
                                  track_content_y + track_content_h,
                                  {0}};
              hm.rb.event_idx = rb->event_idx;
              hm.rb.count = (rb->event_idx != (size_t)-1) ? 1 : 0;
              *array_list_push(&tv->hover_matches, hover_match_t, allocator) =
                  hm;
              break;
            }
          }
        }
      }
    }
  }

  // 4. Boundary Updates (using pre-calculated snap_best_ts from THIS frame)
  if (!interaction_ignored) {
    if (input->ruler_active) {
      if (tv->selection_drag_mode == INTERACTION_DRAG_MODE_RULER_NEW) {
        if (was_drag) {
          tv->selection_end_time = tv->snap_best_ts;
        }
      } else if (tv->selection_drag_mode == INTERACTION_DRAG_MODE_RULER_START) {
        tv->selection_start_time = tv->snap_best_ts;
      } else if (tv->selection_drag_mode == INTERACTION_DRAG_MODE_RULER_END) {
        tv->selection_end_time = tv->snap_best_ts;
      }
    } else if (input->tracks_hovered) {
      if (tv->selection_drag_mode == INTERACTION_DRAG_MODE_TRACKS_START ||
          tv->selection_drag_mode == INTERACTION_DRAG_MODE_TRACKS_END) {
        double ts = (input->is_mouse_clicked) ? mouse_ts : tv->snap_best_ts;
        if (tv->selection_drag_mode == INTERACTION_DRAG_MODE_TRACKS_START) {
          tv->selection_start_time = ts;
        } else {
          tv->selection_end_time = ts;
        }
      }
    }
  }

  // 5. Selection (Click to select event, Double-click to zoom)
  const hover_match_t* hover_matches_ptr =
      (const hover_match_t*)tv->hover_matches.ptr;
  if (input->is_mouse_double_clicked) {
    if (tv->hover_matches.len > 0) {
      const hover_match_t* hm = &hover_matches_ptr[tv->hover_matches.len - 1];
      const track_t* t = &tracks[hm->track_idx];
      if (t->type == TRACK_TYPE_THREAD && hm->rb.event_idx != (size_t)-1) {
        const trace_event_persisted_t* events =
            (const trace_event_persisted_t*)td->events.ptr;
        const trace_event_persisted_t* e = &events[hm->rb.event_idx];
        tv->focused_event_idx = (int64_t)hm->rb.event_idx;
        tv->show_details_panel = true;
        tv->ignore_next_release = true;
        trace_viewer_zoom_to_event(tv, e);
      }
    }
  } else if (!interaction_ignored && input->is_mouse_released && !was_drag) {
    if (tv->hover_matches.len > 0) {
      const hover_match_t* hm = &hover_matches_ptr[tv->hover_matches.len - 1];
      if (hm->rb.event_idx != (size_t)-1) {
        tv->focused_event_idx = (int64_t)hm->rb.event_idx;
        tv->show_details_panel = true;
      }
    } else if (input->tracks_hovered && mouse_in_tracks_content) {
      if (tv->selection_active && mouse_in_selection && !proximity.near_start &&
          !proximity.near_end) {
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

  if (input->is_mouse_released) {
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
    if (t1 > t2) swap(double, t1, t2);

    tv->selection_layout.x1 =
        (float)(tracks_origin_x + (t1 - tv->viewport.start_time) /
                                      current_duration * tracks_inner_width);
    tv->selection_layout.x2 =
        (float)(tracks_origin_x + (t2 - tv->viewport.start_time) /
                                      current_duration * tracks_inner_width);

    format_duration(tv->selection_layout.duration_label,
                    sizeof(tv->selection_layout.duration_label), t2 - t1,
                    t2 - t1);
  }

  // Ruler Ticks
  array_list_clear(&tv->ruler_ticks);
  double tick_interval =
      calculate_tick_interval(current_duration, tracks_inner_width, 100.0);
  double display_start = tv->viewport.start_time - (double)tv->viewport.min_ts;
  double display_end = tv->viewport.end_time - (double)tv->viewport.min_ts;
  double first_tick_rel = ceil(display_start / tick_interval) * tick_interval;
  for (double t_rel = first_tick_rel; t_rel <= display_end;
       t_rel += tick_interval) {
    double t = t_rel + (double)tv->viewport.min_ts;
    float x =
        (float)(tracks_origin_x + (t - tv->viewport.start_time) /
                                      current_duration * tracks_inner_width);
    if (x < tracks_origin_x || x > tracks_origin_x + tracks_inner_width)
      continue;

    ruler_tick_t tick;
    tick.x = x;
    tick.ts_rel = t_rel;
    format_duration(tick.label, sizeof(tick.label), t_rel, tick_interval);
    *array_list_push(&tv->ruler_ticks, ruler_tick_t, allocator) = tick;
  }

  tv->last_best_snap_ts = tv->snap_best_ts;

  if (atomic_exchange(&tv->search.results_ready, false)) {
    pthread_mutex_lock(&tv->search.mutex);
    LOG_INFO(
        "MAIN THREAD RESULTS READY EXCHANGED! pending_results size = %zu, "
        "histogram buckets = %d",
        tv->search.pending_results.len,
        tv->search.pending_histogram.num_buckets);
    array_list_clear(&tv->selected_event_indices);
    if (tv->search.pending_results.len > 0) {
      int64_t* dest_selections =
          array_list_append(&tv->selected_event_indices,
                            tv->search.pending_results.len, int64_t, allocator);
      memcpy(dest_selections, tv->search.pending_results.ptr,
             tv->search.pending_results.len * sizeof(int64_t));
    }
    tv->selected_events_dirty = true;

    tv->histogram = tv->search.pending_histogram;

    array_list_clear(&tv->filtered_event_indices);
    const int64_t* selected_ptr =
        (const int64_t*)tv->selected_event_indices.ptr;
    if (tv->selected_histogram_bucket == -1) {
      for (size_t i = 0; i < tv->selected_event_indices.len; i++) {
        size_t idx = (size_t)selected_ptr[i];
        if (idx < td->events.len) {
          *array_list_push(&tv->filtered_event_indices, int64_t, allocator) =
              (int64_t)idx;
        }
      }
    } else if (tv->selected_histogram_bucket >= 0 &&
               tv->selected_histogram_bucket < tv->histogram.num_buckets) {
      const duration_histogram_bucket_t* b =
          &tv->histogram.buckets[tv->selected_histogram_bucket];
      const trace_event_persisted_t* events =
          (const trace_event_persisted_t*)td->events.ptr;
      for (size_t i = 0; i < tv->selected_event_indices.len; i++) {
        size_t idx = (size_t)selected_ptr[i];
        if (idx >= td->events.len) continue;
        const trace_event_persisted_t* e = &events[idx];
        int64_t d = e->dur;

        if (d >= b->min_dur && d <= b->max_dur) {
          *array_list_push(&tv->filtered_event_indices, int64_t, allocator) =
              (int64_t)idx;
        }
      }
    }
    pthread_mutex_unlock(&tv->search.mutex);

    tv->search_histogram_dirty = false;
  }

  if (tv->search_histogram_dirty) {
    tv->search_histogram_dirty = false;

    array_list_clear(&tv->filtered_event_indices);
    const int64_t* selected_ptr =
        (const int64_t*)tv->selected_event_indices.ptr;
    if (tv->selected_histogram_bucket == -1) {
      for (size_t i = 0; i < tv->selected_event_indices.len; i++) {
        size_t idx = (size_t)selected_ptr[i];
        if (idx < td->events.len) {
          *array_list_push(&tv->filtered_event_indices, int64_t, allocator) =
              (int64_t)idx;
        }
      }
    } else if (tv->selected_histogram_bucket >= 0 &&
               tv->selected_histogram_bucket < tv->histogram.num_buckets) {
      const duration_histogram_bucket_t* b =
          &tv->histogram.buckets[tv->selected_histogram_bucket];
      const trace_event_persisted_t* events =
          (const trace_event_persisted_t*)td->events.ptr;
      for (size_t i = 0; i < tv->selected_event_indices.len; i++) {
        size_t idx = (size_t)selected_ptr[i];
        if (idx >= td->events.len) continue;
        const trace_event_persisted_t* e = &events[idx];
        int64_t d = e->dur;

        if (d >= b->min_dur && d <= b->max_dur) {
          *array_list_push(&tv->filtered_event_indices, int64_t, allocator) =
              (int64_t)idx;
        }
      }
    }
  }

  // 7. Precompute Vertical Minimap Layout & Interaction
  trace_viewer_step_vertical_minimap(tv, input);
}

static void trace_viewer_step_vertical_minimap(
    trace_viewer_t* tv, const trace_viewer_input_t* input) {
  float minimap_x =
      input->viewport_x + input->viewport_width - VERTICAL_MINIMAP_WIDTH;
  float minimap_y = input->viewport_y + input->ruler_height;
  float visible_h = input->viewport_height - input->ruler_height;
  float total_h = tv->total_tracks_height;

  float lane_height = input->lane_height;
  float scale =
      VERTICAL_MINIMAP_LANE_HEIGHT / (lane_height > 0.0f ? lane_height : 1.0f);

  float minimap_content_h = total_h * scale;
  float minimap_draw_h = min(minimap_content_h, visible_h);

  float scroll_y = input->tracks_scroll_y;

  // Calculate current minimap scroll based on current main scroll
  float minimap_scroll_y = 0.0f;
  if (minimap_content_h > visible_h && total_h > visible_h) {
    float pct = scroll_y / (total_h - visible_h);
    minimap_scroll_y = pct * (minimap_content_h - visible_h);
  }

  // Interaction Logic (Mouse Hit-testing & Drag state)
  bool is_hovered = total_h > 0 && input->mouse_x >= minimap_x &&
                    input->mouse_x <= minimap_x + VERTICAL_MINIMAP_WIDTH &&
                    input->mouse_y >= minimap_y &&
                    input->mouse_y <= minimap_y + minimap_draw_h;

  bool inside_slider = false;
  if (tv->vertical_minimap.layout.active) {
    inside_slider = input->mouse_y >= tv->vertical_minimap.layout.slider_y1 &&
                    input->mouse_y <= tv->vertical_minimap.layout.slider_y2;
  }

  bool jump_clicked = false;

  if (is_hovered && input->is_mouse_clicked) {
    tv->vertical_minimap.is_dragging = true;

    if (inside_slider) {
      // Clicked on slider: start dragging directly, capture offset immediately
      tv->vertical_minimap.drag_offset_y =
          input->mouse_y - tv->vertical_minimap.layout.slider_y1;
    } else {
      // Clicked outside slider: jump to track
      jump_clicked = true;
      if (total_h > visible_h) {
        float mouse_y_rel = input->mouse_y - minimap_y;
        float y_content = mouse_y_rel + minimap_scroll_y;
        float y_content_main = y_content / scale;

        size_t clicked_track_idx = (size_t)-1;
        const track_view_info_t* track_infos =
            (const track_view_info_t*)tv->track_infos.ptr;
        for (size_t i = 0; i < tv->tracks.len; i++) {
          const track_view_info_t* vi = &track_infos[i];
          if (y_content_main >= vi->y_rel &&
              y_content_main < vi->y_rel + vi->height) {
            clicked_track_idx = i;
            break;
          }
        }

        if (clicked_track_idx != (size_t)-1) {
          const track_view_info_t* vi = &track_infos[clicked_track_idx];
          float target_scroll = vi->y_rel - (visible_h - vi->height) * 0.5f;
          tv->target_scroll_y = clamp(target_scroll, 0.0f, total_h - visible_h);
        }
      }
      // Set sentinel to calculate offset in next frame after scroll applies
      tv->vertical_minimap.drag_offset_y = -1.0f;
    }
  }

  if (!input->is_mouse_down) {
    tv->vertical_minimap.is_dragging = false;
  }

  // Calculate slider height (fixed ratio of viewport to total height, scaled)
  float slider_h = visible_h * scale;
  slider_h = clamp(slider_h, 10.0f, minimap_draw_h);

  if (tv->vertical_minimap.is_dragging && !jump_clicked &&
      total_h > visible_h) {
    float current_slider_y1 = minimap_y + scroll_y * scale - minimap_scroll_y;

    if (tv->vertical_minimap.drag_offset_y == -1.0f) {
      // Capture offset in the first frame after a jump-click
      tv->vertical_minimap.drag_offset_y = input->mouse_y - current_slider_y1;
    }

    float mouse_y_rel = clamp(input->mouse_y - minimap_y, 0.0f, minimap_draw_h);
    float slider_y1_target = mouse_y_rel - tv->vertical_minimap.drag_offset_y;
    float max_slider_y = minimap_draw_h - slider_h;

    float pct = 0.0f;
    if (max_slider_y > 0.0f) {
      pct = clamp(slider_y1_target / max_slider_y, 0.0f, 1.0f);
    }
    tv->target_scroll_y = pct * (total_h - visible_h);
  }

  // Layout Projections
  vertical_minimap_layout_t* layout = &tv->vertical_minimap.layout;
  layout->active = total_h > 0;
  layout->x = minimap_x;
  layout->y = minimap_y;
  layout->width = VERTICAL_MINIMAP_WIDTH;
  layout->height = minimap_draw_h;
  layout->is_hovered = is_hovered;
  layout->minimap_scroll_y = minimap_scroll_y;

  if (total_h > 0) {
    float slider_y1 = minimap_y + scroll_y * scale - minimap_scroll_y;
    // Clamp slider bounds to maintain fixed height
    slider_y1 =
        clamp(slider_y1, minimap_y, minimap_y + minimap_draw_h - slider_h);
    float slider_y2 = slider_y1 + slider_h;

    layout->slider_y1 = slider_y1;
    layout->slider_y2 = slider_y2;
  } else {
    layout->slider_y1 = minimap_y;
    layout->slider_y2 = minimap_y + minimap_draw_h;
  }
}

static void trace_viewer_draw_vertical_minimap(const trace_viewer_t* tv,
                                               const trace_data_t* td,
                                               ig_draw_list_t* draw_list,
                                               const theme_t* theme) {
  const vertical_minimap_layout_t* layout = &tv->vertical_minimap.layout;
  if (!layout->active) return;

  ig_vec2_t minimap_pos = {layout->x, layout->y};
  ig_vec2_t minimap_size = {layout->width, layout->height};

  // Draw minimap background with premium glassmorphic styling
  ig_draw_list_add_rect_filled(draw_list, minimap_pos,
                               (ig_vec2_t){minimap_pos.x + minimap_size.x,
                                           minimap_pos.y + minimap_size.y},
                               theme->vertical_minimap_bg);
  ig_draw_list_add_line(
      draw_list, minimap_pos,
      (ig_vec2_t){minimap_pos.x, minimap_pos.y + minimap_size.y},
      theme->track_separator, 1.0f);

  float scale = VERTICAL_MINIMAP_LANE_HEIGHT /
                (tv->last_lane_height > 0.0f ? tv->last_lane_height : 1.0f);

  // 1. Draw 2D micro track heat blocks
  float cell_w = (minimap_size.x - 2.0f) / TRACK_HEATMAP_BUCKET_COUNT;
  float minimap_scroll_y = layout->minimap_scroll_y;

  const track_view_info_t* track_infos =
      (const track_view_info_t*)tv->track_infos.ptr;
  const track_heatmap_t* heatmaps =
      (const track_heatmap_t*)tv->vertical_minimap.track_heatmap_densities.ptr;
  const trace_event_persisted_t* events =
      (const trace_event_persisted_t*)td->events.ptr;

  for (size_t i = 0; i < tv->tracks.len; i++) {
    const track_view_info_t* vi = &track_infos[i];
    // Offset by header height in minimap (1 lane =
    // VERTICAL_MINIMAP_LANE_HEIGHT)
    float draw_y1 = minimap_pos.y + vi->y_rel * scale - minimap_scroll_y +
                    VERTICAL_MINIMAP_LANE_HEIGHT;
    float draw_y2 =
        minimap_pos.y + (vi->y_rel + vi->height) * scale - minimap_scroll_y;

    // Culling (use full track bounds for culling)
    float full_draw_y1 = minimap_pos.y + vi->y_rel * scale - minimap_scroll_y;
    if (draw_y2 <= minimap_pos.y ||
        full_draw_y1 >= minimap_pos.y + minimap_size.y) {
      continue;
    }

    if (draw_y1 < draw_y2) {
      const track_heatmap_t* h = &heatmaps[i];

      // Render the 16 horizontal time slices with consecutive bucket coalescing
      int start_b = -1;
      uint32_t active_col = 0;

      for (int b = 0; b < TRACK_HEATMAP_BUCKET_COUNT; b++) {
        size_t event_idx = h->event_indices[b];
        uint32_t cell_col =
            (event_idx != (size_t)-1) ? events[event_idx].color : 0;

        if (cell_col == active_col) {
          continue;
        }

        // Flush previous coalesced block
        if (active_col != 0 && start_b != -1) {
          float cell_x1 = minimap_pos.x + 1.0f + (float)start_b * cell_w;
          float cell_x2 = minimap_pos.x + 1.0f + (float)b * cell_w;
          ig_draw_list_add_rect_filled(draw_list, (ig_vec2_t){cell_x1, draw_y1},
                                       (ig_vec2_t){cell_x2, draw_y2},
                                       active_col);
        }

        // Start new block
        active_col = cell_col;
        start_b = (cell_col != 0) ? b : -1;
      }

      // Final flush
      if (active_col != 0 && start_b != -1) {
        float cell_x1 = minimap_pos.x + 1.0f + (float)start_b * cell_w;
        float cell_x2 =
            minimap_pos.x + 1.0f + (float)TRACK_HEATMAP_BUCKET_COUNT * cell_w;
        ig_draw_list_add_rect_filled(draw_list, (ig_vec2_t){cell_x1, draw_y1},
                                     (ig_vec2_t){cell_x2, draw_y2}, active_col);
      }
    }
  }

  // 2. Draw search/selection hotspot ticks
  const bool* track_has_selected =
      (const bool*)tv->vertical_minimap.track_has_selected.ptr;
  for (size_t i = 0; i < tv->tracks.len; i++) {
    if (i < tv->vertical_minimap.track_has_selected.len &&
        track_has_selected[i]) {
      const track_view_info_t* vi = &track_infos[i];
      // Center on content area (excluding header)
      float draw_y =
          minimap_pos.y +
          (vi->y_rel + (vi->height + tv->last_lane_height) * 0.5f) * scale -
          minimap_scroll_y;
      if (draw_y >= minimap_pos.y && draw_y <= minimap_pos.y + minimap_size.y) {
        ig_draw_list_add_line(
            draw_list, (ig_vec2_t){minimap_pos.x + 1.0f, draw_y},
            (ig_vec2_t){minimap_pos.x + minimap_size.x, draw_y},
            theme->timeline_selection_line, 1.5f);
      }
    }
  }

  // 3. Draw viewport bracket/slider
  uint32_t slider_col = theme->vertical_minimap_slider_bg;
  if (tv->vertical_minimap.is_dragging) {
    slider_col = theme->vertical_minimap_slider_bg_active;
  } else if (layout->is_hovered) {
    slider_col = theme->vertical_minimap_slider_bg_hovered;
  }

  ig_draw_list_add_rect_filled(
      draw_list, (ig_vec2_t){minimap_pos.x + 2.0f, layout->slider_y1 + 1.0f},
      (ig_vec2_t){minimap_pos.x + minimap_size.x - 2.0f,
                  layout->slider_y2 - 1.0f},
      slider_col);
}

void trace_viewer_draw(trace_viewer_t* tv, trace_data_t* td,
                       allocator_t allocator, const theme_t* theme_ptr) {
  const theme_t* theme = theme_ptr;

  if (td->events.len > 0) {
    ig_vec2_t canvas_pos = ig_get_cursor_screen_pos();
    ig_vec2_t canvas_size = ig_get_content_region_avail();

    trace_viewer_input_t input = {
        .canvas_x = canvas_pos.x,
        .canvas_y = canvas_pos.y,
        .canvas_width = canvas_size.x,
        .canvas_height = canvas_size.y,
        .ruler_height = ig_get_frame_height(),
        .lane_height = ig_get_frame_height(),
        .viewport_x = canvas_pos.x,
        .viewport_y = canvas_pos.y,
        .viewport_width = canvas_size.x,
        .viewport_height = canvas_size.y,
        .mouse_x = ig_get_io_mouse_pos().x,
        .mouse_y = ig_get_io_mouse_pos().y,
        .mouse_wheel = ig_get_io_mouse_wheel(),
        .mouse_wheel_h = ig_get_io_mouse_wheel_h(),
        .click_x = ig_get_io_mouse_clicked_pos(0).x,
        .click_y = ig_get_io_mouse_clicked_pos(0).y,
        .is_mouse_down = ig_is_mouse_down(0),
        .is_mouse_clicked = ig_is_mouse_clicked(0, false),
        .is_mouse_double_clicked = ig_is_mouse_double_clicked(0),
        .is_mouse_released = ig_is_mouse_released(0),
        .mouse_delta_x = ig_get_io_mouse_delta().x,
        .mouse_delta_y = ig_get_io_mouse_delta().y,
        .drag_delta_x = ig_get_mouse_drag_delta(0, -1.0f).x,
        .drag_delta_y = ig_get_mouse_drag_delta(0, -1.0f).y,
        .drag_threshold = ig_get_io_mouse_drag_threshold(),
        .is_ctrl_down = platform_is_mac() ? ig_is_key_down(IG_MOD_SUPER)
                                          : ig_is_key_down(IG_MOD_CTRL),
        .is_shift_down = ig_is_key_down(IG_MOD_SHIFT),
    };

    float tracks_origin_x =
        tv->last_tracks_x > 0 ? tv->last_tracks_x : canvas_pos.x;
    float tracks_inner_width = tv->last_inner_width > 0
                                   ? tv->last_inner_width
                                   : (canvas_size.x - VERTICAL_MINIMAP_WIDTH);
    if (tracks_inner_width <= 0) tracks_inner_width = 1.0f;

    // Pre-pass to capture interaction state
    ig_set_cursor_screen_pos((ig_vec2_t){tracks_origin_x, canvas_pos.y});
    ig_invisible_button("##Ruler",
                        (ig_vec2_t){tracks_inner_width, input.ruler_height});
    input.ruler_active = ig_is_item_active();
    input.ruler_activated = ig_is_item_activated();
    input.ruler_deactivated = ig_is_item_deactivated();

    ig_window_flags_t child_flags =
        IG_WINDOW_FLAGS_NO_MOVE | IG_WINDOW_FLAGS_NO_SCROLLBAR;
    if (input.is_ctrl_down) child_flags |= IG_WINDOW_FLAGS_NO_SCROLL_WITH_MOUSE;

    ig_set_cursor_screen_pos(
        (ig_vec2_t){canvas_pos.x, canvas_pos.y + input.ruler_height});
    if (ig_begin_child("TrackList",
                       (ig_vec2_t){canvas_size.x - VERTICAL_MINIMAP_WIDTH,
                                   canvas_size.y - input.ruler_height},
                       false, child_flags)) {
      input.tracks_hovered =
          ig_is_window_hovered(IG_HOVERED_FLAGS_CHILD_WINDOWS);
      input.tracks_scroll_y = ig_get_scroll_y();

      // Top-left of the child window area (excluding scroll)
      ig_vec2_t window_pos = ig_get_window_pos();
      float padding_x = ig_get_style_window_padding().x;
      float padding_y = ig_get_style_window_padding().y;

      input.canvas_x = window_pos.x + padding_x;
      input.canvas_y = window_pos.y + padding_y - input.ruler_height;
      input.canvas_width = ig_get_content_region_avail().x;
      input.canvas_height =
          ig_get_content_region_avail().y + input.ruler_height;
    }
    ig_end_child();

    trace_viewer_step(tv, td, &input, allocator);

    // --- Drawing Phase ---
    ig_draw_list_t* draw_list = ig_get_window_draw_list();
    ig_draw_list_add_rect_filled(
        draw_list, canvas_pos,
        (ig_vec2_t){canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y},
        theme->viewport_bg);

    double current_mouse_ts = trace_viewer_px_to_ts(
        tv->viewport.start_time, tv->viewport.end_time, input.canvas_width,
        input.canvas_x, input.mouse_x);
    SelectionProximity proximity = trace_viewer_selection_check_proximity(
        tv, current_mouse_ts,
        (5.0f / input.canvas_width) *
            (tv->viewport.end_time - tv->viewport.start_time));

    if (tv->selection_drag_mode != INTERACTION_DRAG_MODE_BOX_SELECT &&
        ((tv->selection_drag_mode != INTERACTION_DRAG_MODE_NONE &&
          tv->selection_drag_mode != INTERACTION_DRAG_MODE_RULER_NEW) ||
         proximity.near_start || proximity.near_end)) {
      ig_set_mouse_cursor(IG_MOUSE_CURSOR_RESIZE_EW);
    }

    ig_set_cursor_screen_pos(
        (ig_vec2_t){canvas_pos.x, canvas_pos.y + input.ruler_height});
    if (ig_begin_child("TrackList",
                       (ig_vec2_t){canvas_size.x - VERTICAL_MINIMAP_WIDTH,
                                   canvas_size.y - input.ruler_height},
                       false, child_flags)) {
      if (tv->target_scroll_y != -1.0f) {
        ig_set_scroll_y(max(0.0f, tv->target_scroll_y));
        tv->target_scroll_y = -1.0f;
      }

      // Handle vertical scroll if dragging tracks
      if (ig_is_window_hovered(0) && ig_is_mouse_dragging(0, -1.0f) &&
          tv->selection_drag_mode == INTERACTION_DRAG_MODE_NONE) {
        ig_set_scroll_y(ig_get_scroll_y() - ig_get_io_mouse_delta().y);
      }

      ig_draw_list_t* track_draw_list = ig_get_window_draw_list();

      ig_dummy((ig_vec2_t){0.0f, tv->total_tracks_height});
      ig_set_cursor_pos((ig_vec2_t){0, 0});

      ig_vec2_t tracks_canvas_pos = ig_get_cursor_screen_pos();
      float inner_width = ig_get_content_region_avail().x;
      float inner_height = ig_get_content_region_avail().y;

      // Update last_* fields with current frame's accurate UNSCROLLED values
      tv->last_tracks_x = tracks_canvas_pos.x;
      tv->last_tracks_y = tracks_canvas_pos.y + ig_get_scroll_y();
      tv->last_inner_width = inner_width;
      tv->last_inner_height = inner_height;

      const track_t* tracks = (const track_t*)tv->tracks.ptr;
      const track_view_info_t* track_infos =
          (const track_view_info_t*)tv->track_infos.ptr;

      for (size_t i = 0; i < tv->tracks.len; i++) {
        const track_t* t = &tracks[i];
        const track_view_info_t* vi = &track_infos[i];

        if (!vi->visible) continue;

        ig_vec2_t track_pos = {tracks_canvas_pos.x, vi->y};

        ig_draw_list_add_rect_filled(
            track_draw_list, track_pos,
            (ig_vec2_t){track_pos.x + inner_width, track_pos.y + vi->height},
            theme->track_bg);

        // Render track header
        ig_draw_list_add_rect_filled(
            track_draw_list, track_pos,
            (ig_vec2_t){track_pos.x + inner_width,
                        track_pos.y + input.lane_height},
            theme->track_header_bg);
        ig_draw_list_add_line(
            track_draw_list,
            (ig_vec2_t){track_pos.x, track_pos.y + input.lane_height - 1},
            (ig_vec2_t){track_pos.x + inner_width,
                        track_pos.y + input.lane_height - 1},
            theme->track_separator, 1.0f);

        // Sticky header text
        float sticky_x = max(track_pos.x, tracks_canvas_pos.x);
        float font_size = ig_get_font_size();
        ig_vec2_t text_pos = {
            sticky_x + 5, track_pos.y + (input.lane_height - font_size) * 0.5f};

        size_t display_name_len = strlen(vi->name);
        ig_draw_list_add_text(track_draw_list, ig_get_font(), font_size,
                              text_pos, theme->track_text, vi->name,
                              vi->name + display_name_len, 0.0f, nullptr);

        ig_vec2_t text_size =
            ig_font_calc_text_size_a(ig_get_font(), font_size, FLT_MAX, 0.0f,
                                     vi->name, vi->name + display_name_len);

        // Tooltip for header
        if (trace_viewer_selection_is_mouse_inside(
                tv, trace_viewer_px_to_ts(
                        tv->viewport.start_time, tv->viewport.end_time,
                        inner_width, tracks_canvas_pos.x, input.mouse_x)) &&
            ig_is_mouse_hovering_rect(
                text_pos,
                (ig_vec2_t){text_pos.x + text_size.x, text_pos.y + text_size.y},
                true)) {
          ig_push_style_var(IG_STYLE_VAR_WINDOW_PADDING,
                            (ig_vec2_t){10.0f, 10.0f});
          ig_begin_tooltip();
          ig_text("PID: %d", t->pid);
          if (t->type == TRACK_TYPE_THREAD) {
            ig_text("TID: %d", t->tid);
          }
          ig_end_tooltip();
          ig_pop_style_var(1);
        }

        if (t->type == TRACK_TYPE_THREAD) {
          track_compute_render_blocks(
              t, td, tv->viewport.start_time, tv->viewport.end_time,
              inner_width, tracks_canvas_pos.x, tv->focused_event_idx,
              &tv->track_renderer_state, &tv->render_blocks, allocator);

          const track_render_block_t* rblocks =
              (const track_render_block_t*)tv->render_blocks.ptr;
          for (size_t k = 0; k < tv->render_blocks.len; k++) {
            const track_render_block_t* rb = &rblocks[k];
            float y1 = track_pos.y + (float)(rb->depth + 1) * input.lane_height;
            float y2 = y1 + input.lane_height - 1.0f;

            trace_viewer_draw_event(tv, td, track_draw_list, rb->x1, rb->x2, y1,
                                    y2, rb->color, rb->is_selected,
                                    rb->is_focused, rb->name_ref, inner_width,
                                    tracks_canvas_pos.x, theme);
          }
        } else {
          bool mouse_in_sel =
              input.tracks_hovered &&
              trace_viewer_selection_is_mouse_inside(
                  tv, trace_viewer_px_to_ts(
                          tv->viewport.start_time, tv->viewport.end_time,
                          inner_width, tracks_canvas_pos.x, input.mouse_x));
          trace_viewer_draw_counter_track(
              tv, td, track_draw_list, t,
              (ig_vec2_t){track_pos.x, track_pos.y + input.lane_height},
              inner_width, vi->height - input.lane_height,
              tv->viewport.start_time, tv->viewport.end_time, theme,
              (ig_vec2_t){input.mouse_x, input.mouse_y}, mouse_in_sel,
              tv->focused_event_idx, allocator);
        }
      }

      // Handle hover highlighting and tooltip
      if (tv->hover_matches.len > 0) {
        const hover_match_t* hover_matches =
            (const hover_match_t*)tv->hover_matches.ptr;
        const hover_match_t* best_hm =
            &hover_matches[tv->hover_matches.len - 1];
        const track_render_block_t* rb = &best_hm->rb;

        // Re-draw hovered block with highlight (only for threads, counters do
        // it themselves)
        const track_t* best_track = &tracks[best_hm->track_idx];
        if (best_track->type == TRACK_TYPE_THREAD) {
          uint32_t col = rb->color;
          if (!rb->is_selected) {
            ig_vec4_t col_v = ig_color_convert_u32_to_float4(col);
            col_v.x = min(col_v.x + 0.15f, 1.0f);
            col_v.y = min(col_v.y + 0.15f, 1.0f);
            col_v.z = min(col_v.z + 0.15f, 1.0f);
            col = ig_color_convert_float4_to_u32(col_v);

            trace_viewer_draw_event(tv, td, track_draw_list, rb->x1, rb->x2,
                                    best_hm->y1, best_hm->y2, col, false,
                                    rb->is_focused, rb->name_ref, inner_width,
                                    tracks_canvas_pos.x, theme);
          }
        }

        // Show tooltip
        trace_viewer_draw_tooltip(tv, td, best_hm, inner_width,
                                  tracks_canvas_pos.x, allocator);
      }

      if (tv->selection_drag_mode == INTERACTION_DRAG_MODE_BOX_SELECT) {
        ig_draw_list_add_rect_filled(track_draw_list, tv->box_select_start,
                                     tv->box_select_end,
                                     theme->box_selection_bg);
        ig_draw_list_add_rect(track_draw_list, tv->box_select_start,
                              tv->box_select_end, theme->box_selection_border,
                              0.0f, 0, 1.0f);
      }

      trace_viewer_draw_selection_overlay(
          tv, track_draw_list,
          (ig_vec2_t){tracks_canvas_pos.x, ig_get_window_pos().y},
          (ig_vec2_t){inner_width, ig_get_window_size().y}, theme, true);

      if (tv->snap_has_snap &&
          tv->selection_drag_mode != INTERACTION_DRAG_MODE_NONE) {
        ig_draw_list_add_line(track_draw_list,
                              (ig_vec2_t){tv->snap_px, tv->snap_y1},
                              (ig_vec2_t){tv->snap_px, tv->snap_y2},
                              IG_COL32(255, 0, 0, 255), 3.0f);
      }
    }
    ig_end_child();

    // --- Vertical Minimap Render (Dumb Phase) ---
    trace_viewer_draw_vertical_minimap(tv, td, draw_list, theme);

    trace_viewer_draw_time_ruler(
        tv, draw_list, (ig_vec2_t){tracks_origin_x, canvas_pos.y},
        (ig_vec2_t){tracks_inner_width, input.ruler_height}, canvas_pos.x,
        canvas_size.x, theme);
  }

  // Details Panel
  if (tv->show_details_panel) {
    ig_push_style_var(IG_STYLE_VAR_WINDOW_PADDING, (ig_vec2_t){10.0f, 10.0f});
    if (ig_begin("Details", &tv->show_details_panel,
                 IG_WINDOW_FLAGS_NO_FOCUS_ON_APPEARING)) {
      // Search Section
      trace_viewer_draw_search_section(tv, allocator);
      ig_separator();

      bool has_focus = (tv->focused_event_idx != -1);
      bool has_selection = (tv->selected_event_indices.len > 0);

      if (has_selection) {
        ig_text_disabled("Selection (%zu events)",
                         tv->selected_event_indices.len);
        ig_same_line(0.0f, -1.0f);
        if (ig_small_button("Clear")) {
          array_list_clear(&tv->selected_event_indices);
          array_list_clear(&tv->filtered_event_indices);
          tv->selected_histogram_bucket = -1;
          tv->search_histogram_dirty = true;
          tv->selected_events_dirty = true;
        }

        // Duration Histogram
        const duration_histogram_t* h = &tv->histogram;
        if (h->num_buckets > 0) {
          ig_spacing();
          ig_text_disabled("Duration Distribution");

          ig_vec2_t h_canvas_pos = ig_get_cursor_screen_pos();
          float h_canvas_width = ig_get_content_region_avail().x;
          float h_canvas_height = 80.0f;

          ig_invisible_button("##dur_histogram",
                              (ig_vec2_t){h_canvas_width, h_canvas_height});
          bool is_hovered = ig_is_item_hovered();
          ig_vec2_t mouse_pos = ig_get_io_mouse_pos();

          ig_draw_list_t* draw_list = ig_get_window_draw_list();

          // Draw background
          ig_draw_list_add_rect_filled(
              draw_list, h_canvas_pos,
              (ig_vec2_t){h_canvas_pos.x + h_canvas_width,
                          h_canvas_pos.y + h_canvas_height},
              theme->search_histogram_bg);

          float bar_spacing = 2.0f;
          float total_spacing = bar_spacing * (float)(h->num_buckets - 1);
          float bar_width =
              (h_canvas_width - 10.0f - total_spacing) / (float)h->num_buckets;

          float baseline_y = h_canvas_pos.y + h_canvas_height - 20.0f;

          int hovered_bucket = -1;

          for (int i = 0; i < h->num_buckets; i++) {
            const duration_histogram_bucket_t* b = &h->buckets[i];

            float h_ratio = (h->max_bucket_count > 0)
                                ? (float)b->count / (float)h->max_bucket_count
                                : 0.0f;
            float bar_h = h_ratio * (h_canvas_height - 35.0f);

            float x1 =
                h_canvas_pos.x + 5.0f + (float)i * (bar_width + bar_spacing);
            float x2 = x1 + bar_width;
            float y1 = baseline_y - bar_h;
            float y2 = baseline_y;

            bool bucket_hovered =
                is_hovered && mouse_pos.x >= x1 && mouse_pos.x <= x2 &&
                mouse_pos.y >= h_canvas_pos.y && mouse_pos.y <= baseline_y;

            if (bucket_hovered) {
              hovered_bucket = i;
            }

            bool is_selected = (tv->selected_histogram_bucket == i);

            uint32_t bar_col;
            if (is_selected) {
              bar_col = theme->search_histogram_bar_selected;
            } else if (bucket_hovered) {
              bar_col = theme->search_histogram_bar_hovered;
            } else {
              bar_col = theme->search_histogram_bar;
            }

            if (b->count > 0) {
              ig_draw_list_add_rect_filled(draw_list, (ig_vec2_t){x1, y1},
                                           (ig_vec2_t){x2, y2}, bar_col);
            }
          }

          // Draw baseline (X Axis)
          ig_draw_list_add_line(
              draw_list, (ig_vec2_t){h_canvas_pos.x + 5.0f, baseline_y},
              (ig_vec2_t){h_canvas_pos.x + h_canvas_width - 5.0f, baseline_y},
              theme->ruler_border, 1.0f);

          // Draw vertical axis (Y Axis)
          ig_draw_list_add_line(
              draw_list,
              (ig_vec2_t){h_canvas_pos.x + 5.0f, h_canvas_pos.y + 5.0f},
              (ig_vec2_t){h_canvas_pos.x + 5.0f, baseline_y},
              theme->ruler_border, 1.0f);

          // Label Y-Axis bounds
          char y_max_buf[32];
          snprintf(y_max_buf, sizeof(y_max_buf), "%u", h->max_bucket_count);
          ig_draw_list_add_text(
              draw_list, ig_get_font(), ig_get_font_size() * 0.75f,
              (ig_vec2_t){h_canvas_pos.x + 8.0f, h_canvas_pos.y + 4.0f},
              theme->ruler_text, y_max_buf, nullptr, 0.0f, nullptr);
          ig_draw_list_add_text(
              draw_list, ig_get_font(), ig_get_font_size() * 0.75f,
              (ig_vec2_t){h_canvas_pos.x + 8.0f, baseline_y - 12.0f},
              theme->ruler_text, "0", nullptr, 0.0f, nullptr);

          // Label X-Axis bounds
          char min_label[32] = "";
          char max_label[32] = "";
          if (h->num_buckets > 0) {
            int first_non_zero_idx = 0;
            if (h->buckets[0].min_dur <= 0 && h->buckets[0].max_dur <= 0 &&
                h->num_buckets > 1) {
              first_non_zero_idx = 1;
            }
            format_duration(min_label, sizeof(min_label),
                            (double)h->buckets[first_non_zero_idx].min_dur,
                            0.0);
            format_duration(max_label, sizeof(max_label),
                            (double)h->buckets[h->num_buckets - 1].max_dur,
                            0.0);
          }

          ig_draw_list_add_text(
              draw_list, ig_get_font(), ig_get_font_size() * 0.75f,
              (ig_vec2_t){h_canvas_pos.x + 5.0f, baseline_y + 2.0f},
              theme->ruler_text, min_label, nullptr, 0.0f, nullptr);

          float max_text_w = ig_calc_text_size(max_label).x * 0.75f;
          ig_draw_list_add_text(
              draw_list, ig_get_font(), ig_get_font_size() * 0.75f,
              (ig_vec2_t){h_canvas_pos.x + h_canvas_width - 5.0f - max_text_w,
                          baseline_y + 2.0f},
              theme->ruler_text, max_label, nullptr, 0.0f, nullptr);

          if (hovered_bucket != -1) {
            const duration_histogram_bucket_t* b = &h->buckets[hovered_bucket];

            ig_begin_tooltip();

            char min_buf[32], max_buf[32];
            if (b->min_dur <= 0 && b->max_dur <= 0) {
              ig_text("Duration: <= 0 us");
            } else {
              format_duration(min_buf, sizeof(min_buf), (double)b->min_dur,
                              0.0);
              format_duration(max_buf, sizeof(max_buf), (double)b->max_dur,
                              0.0);
              ig_text("Duration: %s - %s", min_buf, max_buf);
            }

            float percent =
                h->total_count > 0
                    ? ((float)b->count / (float)h->total_count) * 100.0f
                    : 0.0f;
            ig_text("Count: %u events (%.1f%%)", b->count, percent);
            if (b->count > 0) {
              ig_text_disabled("Click to filter table below");
            }

            ig_end_tooltip();

            if (ig_is_mouse_clicked(0, false) && b->count > 0) {
              if (tv->selected_histogram_bucket == hovered_bucket) {
                tv->selected_histogram_bucket = -1;
              } else {
                tv->selected_histogram_bucket = hovered_bucket;
              }
              tv->search_histogram_dirty = true;
            }
          }
        }

        if (tv->filtered_event_indices.len > 0) {
          if (ig_begin_table(
                  "##multi_select", 4,
                  IG_TABLE_FLAGS_RESIZABLE | IG_TABLE_FLAGS_SCROLL_Y |
                      IG_TABLE_FLAGS_ROW_BG | IG_TABLE_FLAGS_BORDERS |
                      IG_TABLE_FLAGS_SIZING_FIXED_FIT |
                      IG_TABLE_FLAGS_SORTABLE | IG_TABLE_FLAGS_SORT_TRISTATE |
                      IG_TABLE_FLAGS_NO_SAVED_SETTINGS,
                  (ig_vec2_t){0, 200.0f}, 0.0f)) {
            ig_table_setup_column("Name", IG_TABLE_COLUMN_FLAGS_WIDTH_STRETCH,
                                  0.0f, 0);
            ig_table_setup_column("Category", IG_TABLE_COLUMN_FLAGS_WIDTH_FIXED,
                                  0.0f, 0);
            ig_table_setup_column("Start", IG_TABLE_COLUMN_FLAGS_WIDTH_FIXED,
                                  0.0f, 0);
            ig_table_setup_column("Duration", IG_TABLE_COLUMN_FLAGS_WIDTH_FIXED,
                                  0.0f, 0);
            ig_table_setup_scroll_freeze(0, 1);
            ig_table_headers_row();

            ig_table_sort_specs_t* specs = ig_table_get_sort_specs();
            if (specs != nullptr && ig_table_sort_specs_get_dirty(specs)) {
              if (ig_table_sort_specs_get_count(specs) > 0) {
                int column_index =
                    ig_table_sort_specs_get_column_index(specs, 0);
                ig_sort_direction_t direction =
                    ig_table_sort_specs_get_sort_direction(specs, 0);

                pthread_mutex_lock(&tv->search.mutex);
                tv->search.sort_column = column_index;
                tv->search.sort_ascending =
                    (direction == IG_SORT_DIRECTION_ASCENDING);
                tv->search.sort_none = (direction == IG_SORT_DIRECTION_NONE);
                atomic_store(&tv->search.new_sort_specs_available, true);
                pthread_mutex_unlock(&tv->search.mutex);

                platform_submit_job(trace_viewer_search_job, &tv->search);
              }
              ig_table_sort_specs_clear_dirty(specs);
            }

            ig_list_clipper_t* clipper = ig_list_clipper_create();
            ig_list_clipper_begin(clipper, (int)tv->filtered_event_indices.len,
                                  -1.0f);
            while (ig_list_clipper_step(clipper)) {
              int display_start = ig_list_clipper_get_display_start(clipper);
              int display_end = ig_list_clipper_get_display_end(clipper);
              const int64_t* filtered_ptr =
                  (const int64_t*)tv->filtered_event_indices.ptr;
              const trace_event_persisted_t* events =
                  (const trace_event_persisted_t*)td->events.ptr;

              for (int i = display_start; i < display_end; i++) {
                size_t event_idx = (size_t)filtered_ptr[i];
                const trace_event_persisted_t* e = &events[event_idx];
                string_t name = trace_data_get_string(td, e->name_ref);
                string_t cat = trace_data_get_string(td, e->cat_ref);

                ig_table_next_row();
                ig_table_next_column();

                bool is_focused = (tv->focused_event_idx == (int64_t)event_idx);
                char label[256];
                snprintf(label, sizeof(label), "%.*s##%zu", (int)name.len,
                         name.ptr, event_idx);
                if (ig_selectable(label, is_focused,
                                  IG_SELECTABLE_FLAGS_SPAN_ALL_COLUMNS |
                                      IG_SELECTABLE_FLAGS_ALLOW_OVERLAP,
                                  (ig_vec2_t){0.0f, 0.0f})) {
                  tv->target_focused_event_idx = (int64_t)event_idx;
                }

                ig_table_next_column();
                ig_text_unformatted(cat.ptr, cat.ptr + cat.len);

                ig_table_next_column();
                char ts_buf[32];
                format_duration(ts_buf, sizeof(ts_buf),
                                (double)e->ts - (double)tv->viewport.min_ts,
                                0.0);
                ig_text_unformatted(ts_buf, nullptr);

                ig_table_next_column();
                if (e->dur > 0) {
                  char dur_buf[32];
                  format_duration(dur_buf, sizeof(dur_buf), (double)e->dur,
                                  0.0);
                  ig_text_unformatted(dur_buf, nullptr);
                }
              }
            }
            ig_list_clipper_destroy(clipper);
            ig_end_table();
          }

          if (tv->selected_histogram_bucket != -1) {
            ig_text_disabled("Filtered results: %zu / %zu events",
                             tv->filtered_event_indices.len,
                             tv->selected_event_indices.len);
          }
        }
      }

      if (has_focus && has_selection) {
        ig_separator();
      }

      if (has_focus) {
        const trace_event_persisted_t* events =
            (const trace_event_persisted_t*)td->events.ptr;
        const trace_event_persisted_t* e =
            &events[(size_t)tv->focused_event_idx];
        string_t ph = trace_data_get_string(td, e->ph_ref);
        const track_t* target_track = nullptr;

        const track_t* tracks = (const track_t*)tv->tracks.ptr;
        // Match track containing selected event
        for (size_t t_idx = 0; t_idx < tv->tracks.len; t_idx++) {
          const track_t* test_t = &tracks[t_idx];
          bool is_counter = (ph.len == 1 && ph.ptr[0] == 'C');

          if (is_counter) {
            if (test_t->type == TRACK_TYPE_COUNTER && test_t->pid == e->pid &&
                test_t->name_ref == e->name_ref &&
                test_t->id_ref == e->id_ref) {
              target_track = test_t;
              break;
            }
          } else {
            if (test_t->type == TRACK_TYPE_THREAD && test_t->pid == e->pid &&
                test_t->tid == e->tid) {
              target_track = test_t;
              break;
            }
          }
        }

        ig_text_disabled("Focused Event");
        ig_spacing();

        trace_viewer_draw_event_properties(td, e, (double)tv->viewport.min_ts,
                                           true, target_track, allocator);
      }

      if (!has_focus && !has_selection) {
        ig_vec4_t gray = {0.5f, 0.5f, 0.5f, 1.0f};
        ig_text_colored(gray, "Select an event to see details.");
      }
    }
    ig_end();
    ig_pop_style_var(1);
  }
}

void trace_viewer_init(trace_viewer_t* tv) {
  tv->focused_event_idx = -1;
  tv->target_focused_event_idx = -1;
  tv->target_scroll_y = -1.0f;
  tv->selected_histogram_bucket = -1;
  tv->search_thread_events = true;
  tv->search_counter_events = true;

  // Initialize SearchState sort fields
  tv->search.sort_column = 0;
  tv->search.sort_ascending = true;
  tv->search.sort_none = true;

  atomic_store(&tv->search.include_thread_events, true);
  atomic_store(&tv->search.include_counter_events, true);
}

void trace_viewer_reset_view(trace_viewer_t* tv) {
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

void trace_viewer_precompute_minimap_heatmap(trace_viewer_t* tv,
                                             const trace_data_t* td,
                                             allocator_t a) {
  array_list_resize(&tv->vertical_minimap.track_heatmap_densities,
                    tv->tracks.len, sizeof(track_heatmap_t), a);

  track_heatmap_t* heatmaps =
      (track_heatmap_t*)tv->vertical_minimap.track_heatmap_densities.ptr;
  const track_t* tracks = (const track_t*)tv->tracks.ptr;
  const trace_event_persisted_t* events =
      (const trace_event_persisted_t*)td->events.ptr;

  // Initialize all buckets to (size_t)-1 (idle) to prevent out-of-bounds access
  // on zero duration or empty traces
  for (size_t i = 0; i < tv->tracks.len; i++) {
    track_heatmap_t* h = &heatmaps[i];
    for (int b = 0; b < TRACK_HEATMAP_BUCKET_COUNT; b++) {
      h->event_indices[b] = (size_t)-1;
    }
  }

  double total_dur = (double)(tv->viewport.max_ts - tv->viewport.min_ts);
  if (total_dur > 0) {
    double bucket_dur = total_dur / TRACK_HEATMAP_BUCKET_COUNT;

    // Duration cache for finding the dominant event color
    int64_t max_dur[TRACK_HEATMAP_BUCKET_COUNT];

    for (size_t i = 0; i < tv->tracks.len; i++) {
      const track_t* t = &tracks[i];
      track_heatmap_t* h = &heatmaps[i];

      // Initialize buckets to (size_t)-1 (idle)
      for (int b = 0; b < TRACK_HEATMAP_BUCKET_COUNT; b++) {
        h->event_indices[b] = (size_t)-1;
        max_dur[b] = -1;
      }

      if (t->event_indices.len == 0) continue;

      const size_t* t_event_indices = (const size_t*)t->event_indices.ptr;
      const int* t_depths = (const int*)t->depths.ptr;

      // Identify dominant event index in each bucket
      for (size_t k = 0; k < t->event_indices.len; k++) {
        if (t->type == TRACK_TYPE_THREAD && t_depths[k] != 0) {
          continue;
        }

        size_t event_idx = t_event_indices[k];
        const trace_event_persisted_t* e = &events[event_idx];
        double rel_ts = (double)(e->ts - tv->viewport.min_ts);
        int b_idx = (int)(rel_ts / bucket_dur);
        if (b_idx < 0) b_idx = 0;
        if (b_idx >= TRACK_HEATMAP_BUCKET_COUNT)
          b_idx = TRACK_HEATMAP_BUCKET_COUNT - 1;

        // Pick dominant event index based on longest duration
        if (e->dur > max_dur[b_idx]) {
          max_dur[b_idx] = e->dur;
          h->event_indices[b_idx] = event_idx;
        }
      }
    }
  }
}

struct sort_key {
  int64_t event_idx;
  string_t text;
  int64_t numeric_val;
};
typedef struct sort_key sort_key_t;

static _Thread_local int g_sort_column = 0;
static _Thread_local bool g_sort_ascending = true;

static int trace_viewer_sort_key_compare(const void* va, const void* vb) {
  const sort_key_t* a = (const sort_key_t*)va;
  const sort_key_t* b = (const sort_key_t*)vb;

  int comp = 0;
  switch (g_sort_column) {
    case 0:
    case 1: {
      size_t min_len = a->text.len < b->text.len ? a->text.len : b->text.len;
      comp = memcmp(a->text.ptr, b->text.ptr, min_len);
      if (comp == 0) {
        if (a->text.len < b->text.len)
          comp = -1;
        else if (a->text.len > b->text.len)
          comp = 1;
      }
      break;
    }
    case 2:
    case 3: {
      if (a->numeric_val < b->numeric_val)
        comp = -1;
      else if (a->numeric_val > b->numeric_val)
        comp = 1;
      break;
    }
  }

  if (comp == 0) {
    if (a->event_idx < b->event_idx)
      comp = -1;
    else if (a->event_idx > b->event_idx)
      comp = 1;
  }

  return g_sort_ascending ? comp : -comp;
}

static void trace_viewer_sort_results(const trace_data_t* td,
                                      array_list_t* results, int sort_column,
                                      bool sort_ascending, bool sort_none,
                                      allocator_t allocator) {
  if (results->len > 1) {
    if (sort_none) {
      qsort(results->ptr, results->len, sizeof(int64_t),
            trace_viewer_int64_compare);
    } else {
      sort_key_t stack_keys[128];
      sort_key_t* keys_ptr = stack_keys;
      bool use_heap = results->len > 128;
      if (use_heap) {
        keys_ptr = (sort_key_t*)allocator_alloc(
            allocator, results->len * sizeof(sort_key_t));
      }

      int64_t* results_ptr = (int64_t*)results->ptr;

      for (size_t i = 0; i < results->len; i++) {
        int64_t idx = results_ptr[i];
        const trace_event_persisted_t* e = array_list_get(
            &td->events, const trace_event_persisted_t, (size_t)idx);

        sort_key_t sk;
        sk.event_idx = idx;

        switch (sort_column) {
          case 0:
            sk.text = trace_data_get_string(td, e->name_ref);
            break;
          case 1:
            sk.text = trace_data_get_string(td, e->cat_ref);
            break;
          case 2:
            sk.numeric_val = e->ts;
            break;
          case 3:
            sk.numeric_val = e->dur;
            break;
          default:
            sk.numeric_val = 0;
            break;
        }
        keys_ptr[i] = sk;
      }

      g_sort_column = sort_column;
      g_sort_ascending = sort_ascending;
      qsort(keys_ptr, results->len, sizeof(sort_key_t),
            trace_viewer_sort_key_compare);

      for (size_t i = 0; i < results->len; i++) {
        results_ptr[i] = keys_ptr[i].event_idx;
      }

      if (use_heap) {
        allocator_free(allocator, keys_ptr, results->len * sizeof(sort_key_t));
      }
    }
  }
}

void trace_viewer_search_job(void* user_data) {
  search_state_t* s = (search_state_t*)user_data;
  trace_data_t* td = s->td;
  allocator_t allocator = s->allocator;

  if (atomic_load(&s->jobs_should_abort)) return;

  atomic_store(&s->is_searching, true);

  array_list_t query = {};
  bool do_search = false;
  bool do_sort = false;
  bool do_box_select = false;

  int sort_column = 0;
  bool sort_ascending = true;
  bool sort_none = true;

  {
    pthread_mutex_lock(&s->mutex);
    if (atomic_load(&s->new_query_available)) {
      if (s->pending_query.len > 0) {
        char* dest_query =
            array_list_append(&query, s->pending_query.len, char, allocator);
        memcpy(dest_query, s->pending_query.ptr,
               s->pending_query.len * sizeof(char));
      }
      atomic_store(&s->new_query_available, false);
      do_search = true;
    }

    if (atomic_load(&s->new_sort_specs_available)) {
      atomic_store(&s->new_sort_specs_available, false);
      do_sort = true;
    }

    if (atomic_load(&s->new_box_selection_available)) {
      atomic_store(&s->new_box_selection_available, false);
      do_box_select = true;
    }

    sort_column = s->sort_column;
    sort_ascending = s->sort_ascending;
    sort_none = s->sort_none;
    pthread_mutex_unlock(&s->mutex);
  }

  if (do_search) {
    array_list_t results = {};
    if (query.len > 0 && ((char*)query.ptr)[0] != '\0') {
      const char* query_ptr = (const char*)query.ptr;
      size_t query_len = strlen(query_ptr);
      size_t n_events = td->events.len;

      bool include_threads = atomic_load(&s->include_thread_events);
      bool include_counters = atomic_load(&s->include_counter_events);

      for (size_t i = 0; i < n_events; i++) {
        if (atomic_load(&s->new_query_available) ||
            atomic_load(&s->jobs_should_abort))
          break;

        const trace_event_persisted_t* e =
            array_list_get(&td->events, const trace_event_persisted_t, i);
        string_t ph = trace_data_get_string(td, e->ph_ref);
        bool is_counter = (ph.len == 1 && ph.ptr[0] == 'C');
        bool is_metadata = (ph.len == 1 && ph.ptr[0] == 'M');

        if (is_counter && !include_counters) continue;
        if (!is_counter && !is_metadata && !include_threads) continue;

        string_t name = trace_data_get_string(td, e->name_ref);
        string_t cat = trace_data_get_string(td, e->cat_ref);

        bool match = trace_viewer_str_contains_case_insensitive(name, query_ptr,
                                                                query_len) ||
                     trace_viewer_str_contains_case_insensitive(cat, query_ptr,
                                                                query_len);

        if (!match) {
          for (uint32_t k = 0; k < e->args_count; k++) {
            const trace_arg_persisted_t* arg = &(
                (const trace_arg_persisted_t*)td->args.ptr)[e->args_offset + k];

            if (arg->val_ref != 0) {
              string_t arg_val = trace_data_get_string(td, arg->val_ref);
              if (trace_viewer_str_contains_case_insensitive(arg_val, query_ptr,
                                                             query_len)) {
                match = true;
                break;
              }
            }
          }
        }

        if (match) {
          *array_list_push(&results, int64_t, allocator) = (int64_t)i;
        }
      }

      if (!atomic_load(&s->new_query_available) &&
          !atomic_load(&s->jobs_should_abort)) {
        trace_viewer_sort_results(td, &results, sort_column, sort_ascending,
                                  sort_none, allocator);

        if (!atomic_load(&s->new_query_available) &&
            !atomic_load(&s->jobs_should_abort)) {
          duration_histogram_t hist = {};
          trace_viewer_calculate_histogram(&results, td, &hist);

          pthread_mutex_lock(&s->mutex);
          LOG_INFO(
              "BACKGROUND SEARCH JOB COMPLETED! results size = %zu, histogram "
              "buckets = %d",
              results.len, hist.num_buckets);
          array_list_clear(&s->pending_results);
          if (results.len > 0) {
            int64_t* dest_results = array_list_append(
                &s->pending_results, results.len, int64_t, allocator);
            memcpy(dest_results, results.ptr, results.len * sizeof(int64_t));
          }
          s->pending_histogram = hist;
          atomic_store(&s->results_ready, true);
          pthread_mutex_unlock(&s->mutex);
        }
      }
      array_list_deinit(&results, allocator);
    } else {
      pthread_mutex_lock(&s->mutex);
      array_list_clear(&s->pending_results);
      duration_histogram_t empty_hist = {};
      s->pending_histogram = empty_hist;
      atomic_store(&s->results_ready, true);
      pthread_mutex_unlock(&s->mutex);
    }
  } else if (do_box_select || do_sort) {
    array_list_t results = {};
    {
      pthread_mutex_lock(&s->mutex);
      if (s->pending_results.len > 0) {
        int64_t* dest_copy = array_list_append(&results, s->pending_results.len,
                                               int64_t, allocator);
        memcpy(dest_copy, s->pending_results.ptr,
               s->pending_results.len * sizeof(int64_t));
      }
      pthread_mutex_unlock(&s->mutex);
    }

    trace_viewer_sort_results(td, &results, sort_column, sort_ascending,
                              sort_none, allocator);

    if (!atomic_load(&s->new_query_available) &&
        !atomic_load(&s->jobs_should_abort)) {
      duration_histogram_t hist = {};
      trace_viewer_calculate_histogram(&results, td, &hist);

      pthread_mutex_lock(&s->mutex);
      array_list_clear(&s->pending_results);
      if (results.len > 0) {
        int64_t* dest_res = array_list_append(&s->pending_results, results.len,
                                              int64_t, allocator);
        memcpy(dest_res, results.ptr, results.len * sizeof(int64_t));
      }
      s->pending_histogram = hist;
      atomic_store(&s->results_ready, true);
      pthread_mutex_unlock(&s->mutex);
    }
    array_list_deinit(&results, allocator);
  }

  atomic_store(&s->is_searching, false);
  atomic_store_explicit(&s->request_update, true, memory_order_relaxed);
  array_list_deinit(&query, allocator);

  {
    pthread_mutex_lock(&s->quit_mutex);
    pthread_cond_broadcast(&s->quit_cv);
    pthread_mutex_unlock(&s->quit_mutex);
  }
}

struct InputTextCallback_UserData {
  array_list_t* al;
  allocator_t allocator;
};
typedef struct InputTextCallback_UserData InputTextCallback_UserData;

static int trace_viewer_search_input_callback(
    ig_input_text_callback_data_t* data) {
  InputTextCallback_UserData* ud =
      (InputTextCallback_UserData*)ig_input_text_callback_data_get_user_data(
          data);
  if (ig_input_text_callback_data_get_event_flag(data) ==
      IG_INPUT_TEXT_FLAGS_CALLBACK_RESIZE) {
    array_list_t* al = ud->al;
    size_t buf_size = (size_t)ig_input_text_callback_data_get_buf_size(data);
    array_list_reserve(al, buf_size, sizeof(char), ud->allocator);
    ig_input_text_callback_data_set_buf(data, (char*)al->ptr);
  }
  return 0;
}

static void trace_viewer_draw_search_section(trace_viewer_t* tv,
                                             allocator_t allocator) {
  ig_text_disabled("Search Events");
  if (tv->focus_search_input) {
    ig_set_keyboard_focus_here(0);
    tv->focus_search_input = false;
  }

  if (tv->search_query.cap == 0) {
    array_list_reserve(&tv->search_query, 128, sizeof(char), allocator);
    ((char*)tv->search_query.ptr)[0] = '\0';
    tv->search_query.len = 1;
  }

  InputTextCallback_UserData cb_data = {&tv->search_query, allocator};
  bool search_input_changed =
      ig_input_text("##search", (char*)tv->search_query.ptr,
                    tv->search_query.cap, IG_INPUT_TEXT_FLAGS_CALLBACK_RESIZE,
                    trace_viewer_search_input_callback, &cb_data);

  bool enter_pressed =
      ig_is_item_focused() && ig_is_key_pressed(IG_KEY_ENTER, false);

  ig_spacing();
  bool filter_changed = false;
  filter_changed |= ig_checkbox("Threads", &tv->search_thread_events);
  ig_same_line(0.0f, -1.0f);
  filter_changed |= ig_checkbox("Counters", &tv->search_counter_events);

  if (search_input_changed || enter_pressed || filter_changed) {
    pthread_mutex_lock(&tv->search.mutex);
    const char* last_query =
        (tv->search.pending_query.len > 0 && tv->search.pending_query.ptr)
            ? (const char*)tv->search.pending_query.ptr
            : "";

    bool should_trigger = false;
    if (enter_pressed) {
      if (tv->selected_event_indices.len == 0) {
        should_trigger = true;
      }
    } else if (search_input_changed) {
      if (strcmp(last_query, (const char*)tv->search_query.ptr) != 0) {
        should_trigger = true;
      }
    } else if (filter_changed) {
      should_trigger = true;
    }

    if (should_trigger) {
      array_list_clear(&tv->search.pending_query);
      size_t query_len = strlen((const char*)tv->search_query.ptr) + 1;
      char* dest_pq = array_list_append(&tv->search.pending_query, query_len,
                                        char, allocator);
      memcpy(dest_pq, tv->search_query.ptr, query_len * sizeof(char));

      atomic_store(&tv->search.include_thread_events, tv->search_thread_events);
      atomic_store(&tv->search.include_counter_events,
                   tv->search_counter_events);

      atomic_store(&tv->search.new_query_available, true);
      atomic_store(&tv->search.results_ready, false);
      tv->selected_histogram_bucket = -1;
      tv->search_histogram_dirty = true;
      platform_submit_job(trace_viewer_search_job, &tv->search);
    }
    pthread_mutex_unlock(&tv->search.mutex);
  }

  if (((const char*)tv->search_query.ptr)[0] != '\0') {
    ig_text("%zu events selected", tv->selected_event_indices.len);
    if (atomic_load(&tv->search.is_searching)) {
      ig_same_line(0.0f, -1.0f);
      ig_text_disabled("(searching...)");
    }
  }
}
