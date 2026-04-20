#include "src/track_renderer.h"

#include <algorithm>

void track_compute_render_blocks(
    const Track* track, const TraceData* trace_data, double viewport_start,
    double viewport_end, float inner_width, float tracks_canvas_pos_x,
    int64_t selected_event_index, TrackRendererState* state,
    ArrayList<TrackRenderBlock>* out_blocks, Allocator a) {
  array_list_clear(out_blocks);
  if (track->event_indices.size == 0) return;

  double duration = viewport_end - viewport_start;
  if (duration <= 0) return;
  double inv_duration = (double)inner_width / duration;

  size_t start_idx = track_find_visible_start_index(track, trace_data,
                                                    (int64_t)viewport_start);

  array_list_resize(&state->last_x2_per_depth, a, track->max_depth + 1);
  array_list_resize(&state->merge_levels, a, track->max_depth + 1);
  for (size_t d = 0; d < state->last_x2_per_depth.size; d++) {
    state->last_x2_per_depth[d] = -1e30f;
    state->merge_levels[d].active = false;
  }

  for (size_t k = start_idx; k < track->event_indices.size; k++) {
    size_t event_idx = track->event_indices[k];
    const TraceEventPersisted& e = trace_data->events[event_idx];
    uint32_t depth = track->depths[k];

    if (e.ts > (int64_t)viewport_end) break;

    bool is_selected = (selected_event_index == (int64_t)event_idx);

    float x1 = (float)(tracks_canvas_pos_x +
                       ((double)e.ts - viewport_start) * inv_duration);
    float x2 = (float)(x1 + (double)e.dur * inv_duration);

    TrackMergeBlock& m = state->merge_levels[depth];
    uint32_t col = e.color;

    if (!is_selected && m.active && x1 <= m.x2 + TRACK_MIN_EVENT_WIDTH &&
        (x2 - m.x1) <= TRACK_MIN_EVENT_WIDTH) {
      m.x2 = std::max(m.x2, x2);
      m.count++;
      state->last_x2_per_depth[depth] = m.x2;
    } else {
      if (m.active) {
        TrackRenderBlock rb = {m.x1, m.x2, m.col, m.name_ref, depth, m.count,
                               false, m.event_idx};
        array_list_push_back(out_blocks, a, rb);
        m.active = false;
      }

      // Visibility culling: skip tiny events that fall into the same pixel range
      bool is_tiny = (x2 - x1) < 1.0f;
      if (!is_selected && is_tiny &&
          x2 <= state->last_x2_per_depth[depth] + 0.5f) {
        continue;
      }
      state->last_x2_per_depth[depth] = x2;

      if (!is_selected && (x2 - x1) < TRACK_MIN_EVENT_WIDTH) {
        m.x1 = x1;
        m.x2 = x2;
        m.col = col;
        m.name_ref = e.name_ref;
        m.count = 1;
        m.event_idx = event_idx;
        m.active = true;
      } else {
        TrackRenderBlock rb = {x1, x2, col, e.name_ref, depth, 1, is_selected,
                               event_idx};
        array_list_push_back(out_blocks, a, rb);
      }
    }
  }

  // Flush remaining merges
  for (size_t d = 0; d < state->merge_levels.size; d++) {
    TrackMergeBlock& m = state->merge_levels[d];
    if (m.active) {
      TrackRenderBlock rb = {m.x1, m.x2, m.col, m.name_ref, (uint32_t)d, m.count,
                             false, m.event_idx};
      array_list_push_back(out_blocks, a, rb);
      m.active = false;
    }
  }
}

void track_compute_counter_render_blocks(
    const Track* track, const TraceData* trace_data, double viewport_start,
    double viewport_end, float width, float tracks_canvas_pos_x,
    ArrayList<double>* out_peaks, ArrayList<CounterRenderBlock>* out_blocks,
    Allocator a) {
  array_list_clear(out_blocks);
  array_list_clear(out_peaks);
  if (track->event_indices.size == 0) return;

  int64_t track_first_ts = trace_data->events[track->event_indices[0]].ts;
  int64_t track_last_ts =
      trace_data->events[track->event_indices[track->event_indices.size - 1]].ts;

  if (viewport_end <= (double)track_first_ts ||
      viewport_start >= (double)track_last_ts) {
    return;
  }

  double duration = viewport_end - viewport_start;
  if (duration <= 0) return;
  double inv_duration = (double)width / duration;

  const float BUCKET_SIZE_PX = 3.0f;
  double bucket_dur = BUCKET_SIZE_PX / inv_duration;

  // Align current_bucket_ts to a multiple of bucket_dur for stability during
  // panning.
  double current_bucket_ts = floor(viewport_start / bucket_dur) * bucket_dur;

  // Fast-forward to the first bucket that could contain data
  if (current_bucket_ts < (double)track_first_ts) {
    current_bucket_ts = floor((double)track_first_ts / bucket_dur) * bucket_dur;
  }

  // Find the initial state (values from the last event before the viewport
  // starts)
  ArrayList<double> current_values = {};
  array_list_resize(&current_values, a, track->counter_series.size);
  for (size_t i = 0; i < current_values.size; i++) current_values[i] = 0.0;

  auto it_start = std::lower_bound(
      track->event_indices.data,
      track->event_indices.data + track->event_indices.size,
      (int64_t)current_bucket_ts,
      [&](size_t idx, int64_t val) { return trace_data->events[idx].ts < val; });

  if (it_start != track->event_indices.data) {
    const TraceEventPersisted& e = trace_data->events[*(it_start - 1)];
    for (uint32_t arg_k = 0; arg_k < e.args_count; arg_k++) {
      const TraceArgPersisted& arg = trace_data->args[e.args_offset + arg_k];
      for (size_t s_idx = 0; s_idx < track->counter_series.size; s_idx++) {
        if (track->counter_series[s_idx] == arg.key_ref) {
          current_values[s_idx] = arg.val_double;
          break;
        }
      }
    }
  }

  const size_t* it = it_start;
  const size_t* search_end =
      track->event_indices.data + track->event_indices.size;

  ArrayList<double> bucket_max_values = {};
  array_list_resize(&bucket_max_values, a, track->counter_series.size);

  while (current_bucket_ts < viewport_end) {
    double next_bucket_ts = current_bucket_ts + bucket_dur;

    // Initialize bucket maximums with the values carried over from the previous
    // bucket.
    for (size_t i = 0; i < current_values.size; i++) {
      bucket_max_values[i] = current_values[i];
    }

    size_t last_event_idx_in_bucket = (size_t)-1;

    // Consume all events in this bucket
    while (it < search_end && trace_data->events[*it].ts < (int64_t)next_bucket_ts) {
      const TraceEventPersisted& e = trace_data->events[*it];
      last_event_idx_in_bucket = *it;

      // In Chrome Trace, a counter event usually updates all its series or sets
      // them. We assume missing series in an event means 0 (or unchanged?
      // current logic uses 0 if not present in the event).
      // Wait, let's stick to the current logic: each event IS the state.
      // If we want to be more robust for events that only update one series:
      for (uint32_t arg_k = 0; arg_k < e.args_count; arg_k++) {
        const TraceArgPersisted& arg = trace_data->args[e.args_offset + arg_k];
        for (size_t s_idx = 0; s_idx < track->counter_series.size; s_idx++) {
          if (track->counter_series[s_idx] == arg.key_ref) {
            current_values[s_idx] = arg.val_double;
            if (current_values[s_idx] > bucket_max_values[s_idx]) {
              bucket_max_values[s_idx] = current_values[s_idx];
            }
            break;
          }
        }
      }
      it++;
    }

    // Determine the end boundary for this bucket (clamped to track end)
    double draw_start_ts = std::max(current_bucket_ts, (double)track_first_ts);
    double draw_end_ts = next_bucket_ts;
    bool hit_track_end = false;
    if (draw_end_ts >= (double)track_last_ts) {
      draw_end_ts = (double)track_last_ts;
      hit_track_end = true;
    }

    float x1 = (float)(tracks_canvas_pos_x +
                       (draw_start_ts - viewport_start) * inv_duration);
    float x2 = (float)(tracks_canvas_pos_x +
                       (draw_end_ts - viewport_start) * inv_duration);

    // Clamp to viewport
    if (x1 < tracks_canvas_pos_x) x1 = tracks_canvas_pos_x;
    if (x2 > tracks_canvas_pos_x + width) x2 = tracks_canvas_pos_x + width;

    if (x2 > x1) {
      bool can_merge = false;
      if (out_blocks->size > 0) {
        const CounterRenderBlock& last_rb = (*out_blocks)[out_blocks->size - 1];
        if (last_rb.event_idx == last_event_idx_in_bucket) {
          // Check if peaks match
          can_merge = true;
          size_t series_count = track->counter_series.size;
          size_t last_peaks_offset = (out_blocks->size - 1) * series_count;
          for (size_t i = 0; i < series_count; i++) {
            if (out_peaks->data[last_peaks_offset + i] !=
                bucket_max_values[i]) {
              can_merge = false;
              break;
            }
          }
        }
      }

      if (can_merge) {
        (*out_blocks)[out_blocks->size - 1].x2 = x2;
      } else {
        CounterRenderBlock rb = {x1, x2, last_event_idx_in_bucket};
        if (last_event_idx_in_bucket == (size_t)-1) {
          if (it != track->event_indices.data) {
            rb.event_idx = *(it - 1);
          } else if (track->event_indices.size > 0 && draw_start_ts >= (double)track_first_ts) {
            rb.event_idx = track->event_indices[0];
          }
        }
        array_list_push_back(out_blocks, a, rb);
        for (size_t i = 0; i < bucket_max_values.size; i++) {
          array_list_push_back(out_peaks, a, bucket_max_values[i]);
        }
      }
    }

    if (hit_track_end) {
      break;
    }

    current_bucket_ts = next_bucket_ts;
  }

  array_list_deinit(&current_values, a);
  array_list_deinit(&bucket_max_values, a);
}
