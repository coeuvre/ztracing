#include "src/track_renderer.h"

#include <algorithm>
#include <cmath>

void track_flush_bucket_depth(ArrayList<TrackRenderBlock>* out_blocks,
                              double viewport_start, double inv_duration,
                              float tracks_canvas_pos_x,
                              double current_bucket_ts, double next_bucket_ts,
                              uint32_t depth, ThreadBucketState* s,
                              const TraceData* trace_data, Allocator a) {
  if (s->count == 0) return;

  float x1 = (float)(tracks_canvas_pos_x +
                     (current_bucket_ts - viewport_start) * inv_duration);
  float x2 = (float)(tracks_canvas_pos_x +
                     (next_bucket_ts - viewport_start) * inv_duration);

  const TraceEventPersisted& rep_e = trace_data->events[s->rep_event_idx];
  TrackRenderBlock rb = {x1, x2, rep_e.color, rep_e.name_ref, depth, s->count,
                         false, s->rep_event_idx};
  array_list_push_back(out_blocks, a, rb);
  s->count = 0;
  s->max_dur = -1;
  s->rep_event_idx = (size_t)-1;
}

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

  double bucket_dur = (double)TRACK_MIN_EVENT_WIDTH / inv_duration;
  double current_bucket_ts = floor(viewport_start / bucket_dur) * bucket_dur;

  size_t k = track_find_visible_start_index(track, trace_data,
                                            (int64_t)current_bucket_ts);

  array_list_resize(&state->thread_bucket_states, a, track->max_depth + 1);
  for (size_t d = 0; d < state->thread_bucket_states.size; d++) {
    state->thread_bucket_states[d].count = 0;
    state->thread_bucket_states[d].max_dur = -1;
    state->thread_bucket_states[d].rep_event_idx = (size_t)-1;
    state->thread_bucket_states[d].blocked = false;
  }

  while (current_bucket_ts < viewport_end) {
    double next_bucket_ts = current_bucket_ts + bucket_dur;

    for (size_t d = 0; d < state->thread_bucket_states.size; d++) {
      state->thread_bucket_states[d].blocked = false;
    }

    while (k < track->event_indices.size) {
      size_t event_idx = track->event_indices[k];
      const TraceEventPersisted& e = trace_data->events[event_idx];
      if (e.ts >= (int64_t)next_bucket_ts) break;

      uint32_t depth = track->depths[k];
      bool is_selected = (selected_event_index == (int64_t)event_idx);
      bool is_large = (double)e.dur * inv_duration >= TRACK_MIN_EVENT_WIDTH;

      if (is_selected || is_large) {
        // Flush pending bucket state for this depth
        track_flush_bucket_depth(out_blocks, viewport_start, inv_duration,
                                 tracks_canvas_pos_x, current_bucket_ts,
                                 next_bucket_ts, depth,
                                 &state->thread_bucket_states[depth],
                                 trace_data, a);

        float x1 = (float)(tracks_canvas_pos_x +
                           ((double)e.ts - viewport_start) * inv_duration);
        float x2 = (float)(x1 + (double)e.dur * inv_duration);
        TrackRenderBlock rb = {
            .x1 = x1,
            .x2 = x2,
            .color = e.color,
            .name_ref = e.name_ref,
            .depth = depth,
            .count = 1,
            .is_selected = is_selected,
            .event_idx = event_idx,
        };
        array_list_push_back(out_blocks, a, rb);
        state->thread_bucket_states[depth].blocked = true;
      } else if (!state->thread_bucket_states[depth].blocked) {
        ThreadBucketState& s = state->thread_bucket_states[depth];
        if (e.dur > s.max_dur) {
          s.max_dur = e.dur;
          s.rep_event_idx = event_idx;
        }
        s.count++;
      }
      k++;
    }

    // Flush remaining bucket states
    for (size_t d = 0; d < state->thread_bucket_states.size; d++) {
      track_flush_bucket_depth(out_blocks, viewport_start, inv_duration,
                               tracks_canvas_pos_x, current_bucket_ts,
                               next_bucket_ts, (uint32_t)d,
                               &state->thread_bucket_states[d], trace_data, a);
    }

    current_bucket_ts = next_bucket_ts;
  }

  // Post-processing: merge consecutive blocks that share the same depth and
  // representative event. This happens for events that span multiple buckets.
  if (out_blocks->size > 1) {
    size_t write_idx = 0;
    for (size_t read_idx = 1; read_idx < out_blocks->size; read_idx++) {
      TrackRenderBlock& current = (*out_blocks)[write_idx];
      TrackRenderBlock& next = (*out_blocks)[read_idx];

      if (!current.is_selected && !next.is_selected &&
          current.depth == next.depth &&
          current.event_idx == next.event_idx) {
        current.x2 = next.x2;
        current.count += next.count;
      } else {
        write_idx++;
        (*out_blocks)[write_idx] = next;
      }
    }
    out_blocks->size = write_idx + 1;
  }
}

void track_compute_counter_render_blocks(
    const Track* track, const TraceData* trace_data, double viewport_start,
    double viewport_end, float width, float tracks_canvas_pos_x,
    TrackRendererState* state, ArrayList<CounterRenderBlock>* out_blocks,
    Allocator a) {
  array_list_clear(out_blocks);
  array_list_clear(&state->counter_peaks);
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
  array_list_resize(&state->counter_current_values, a, track->counter_series.size);
  for (size_t i = 0; i < state->counter_current_values.size; i++)
    state->counter_current_values[i] = 0.0;

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
          state->counter_current_values[s_idx] = arg.val_double;
          break;
        }
      }
    }
  }

  const size_t* it = it_start;
  const size_t* search_end =
      track->event_indices.data + track->event_indices.size;

  array_list_resize(&state->counter_bucket_max_values, a, track->counter_series.size);
  array_list_resize(&state->counter_series_updated, a, track->counter_series.size);

  while (current_bucket_ts < viewport_end) {
    double next_bucket_ts = current_bucket_ts + bucket_dur;

    // Initialize bucket maximums with the values carried over from the previous
    // bucket.
    for (size_t i = 0; i < state->counter_current_values.size; i++) {
      state->counter_bucket_max_values[i] = state->counter_current_values[i];
      state->counter_series_updated[i] = 0;
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
            state->counter_current_values[s_idx] = arg.val_double;
            if (!state->counter_series_updated[s_idx]) {
              state->counter_bucket_max_values[s_idx] = arg.val_double;
              state->counter_series_updated[s_idx] = 1;
            } else {
              if (state->counter_current_values[s_idx] >
                  state->counter_bucket_max_values[s_idx]) {
                state->counter_bucket_max_values[s_idx] =
                    state->counter_current_values[s_idx];
              }
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
            if (state->counter_peaks.data[last_peaks_offset + i] !=
                state->counter_bucket_max_values[i]) {
              can_merge = false;
              break;
            }
          }
        }
      }

      if (can_merge) {
        (*out_blocks)[out_blocks->size - 1].x2 = x2;
      } else {
        CounterRenderBlock rb = {
            .x1 = x1,
            .x2 = x2,
            .event_idx = last_event_idx_in_bucket,
        };
        if (last_event_idx_in_bucket == (size_t)-1) {
          if (it != track->event_indices.data) {
            rb.event_idx = *(it - 1);
          } else if (track->event_indices.size > 0 &&
                     draw_start_ts >= (double)track_first_ts) {
            rb.event_idx = track->event_indices[0];
          }
        }
        array_list_push_back(out_blocks, a, rb);
        for (size_t i = 0; i < state->counter_bucket_max_values.size; i++) {
          array_list_push_back(&state->counter_peaks, a,
                               state->counter_bucket_max_values[i]);
        }
      }
    }

    if (hit_track_end) {
      break;
    }

    current_bucket_ts = next_bucket_ts;
  }
}
