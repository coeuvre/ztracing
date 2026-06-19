#include "src/track_renderer.h"

#include <math.h>
#include <string.h>

static size_t binary_search_events(const size_t* event_indices, size_t size,
                                   const trace_event_persisted_t* events,
                                   int64_t target_ts) {
  size_t low = 0;
  size_t high = size;
  while (low < high) {
    size_t mid = low + (high - low) / 2;
    if (events[event_indices[mid]].ts < target_ts) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  return low;
}

void track_flush_bucket_depth(array_list_t* out_blocks,
                              double viewport_start, double inv_duration,
                              float tracks_canvas_pos_x,
                              double current_bucket_ts, double next_bucket_ts,
                              uint32_t depth, thread_bucket_state_t* s,
                              const trace_data_t* trace_data, allocator_t a) {
  if (s->count == 0) return;

  float x1 = (float)(tracks_canvas_pos_x +
                     (current_bucket_ts - viewport_start) * inv_duration);
  float x2 = (float)(tracks_canvas_pos_x +
                     (next_bucket_ts - viewport_start) * inv_duration);

  const trace_event_persisted_t* events = (const trace_event_persisted_t*)trace_data->events.ptr;
  const trace_event_persisted_t* rep_e = &events[s->rep_event_idx];
  
  track_render_block_t rb = {
      .x1 = x1,
      .x2 = x2,
      .color = rep_e->color,
      .name_ref = rep_e->name_ref,
      .depth = depth,
      .count = s->count,
      .is_selected = false,
      .is_focused = false,
      .event_idx = s->rep_event_idx,
  };
  *array_list_push(out_blocks, track_render_block_t, a) = rb;
  s->count = 0;
  s->max_dur = -1;
  s->rep_event_idx = (size_t)-1;
}

void track_renderer_update_selection_bitset(
    track_renderer_state_t* state, const trace_data_t* trace_data,
    const array_list_t* selected_event_indices, allocator_t a) {
  array_list_resize(&state->selected_events_bitset, trace_data->events.len, sizeof(uint8_t), a);
  if (state->selected_events_bitset.len > 0) {
    memset(state->selected_events_bitset.ptr, 0, state->selected_events_bitset.len * sizeof(uint8_t));
    const int64_t* sel_indices = (const int64_t*)selected_event_indices->ptr;
    uint8_t* bitset = (uint8_t*)state->selected_events_bitset.ptr;
    for (size_t i = 0; i < selected_event_indices->len; i++) {
      size_t idx = (size_t)sel_indices[i];
      if (idx < state->selected_events_bitset.len) {
        bitset[idx] = 1;
      }
    }
  }
}

void track_compute_render_blocks(
    const track_t* track, const trace_data_t* trace_data, double viewport_start,
    double viewport_end, float inner_width, float tracks_canvas_pos_x,
    int64_t focused_event_idx,
    track_renderer_state_t* state, array_list_t* out_blocks,
    allocator_t a) {
  array_list_clear(out_blocks);
  if (track->event_indices.len == 0) return;

  double duration = viewport_end - viewport_start;
  if (duration <= 0) return;
  double inv_duration = (double)inner_width / duration;

  double bucket_dur = (double)TRACK_MIN_EVENT_WIDTH / inv_duration;
  // Align current_bucket_ts to a multiple of bucket_dur for stability during panning.
  double current_bucket_ts = floor(viewport_start / bucket_dur) * bucket_dur;

  array_list_resize(&state->thread_depth_blocked_until, track->max_depth + 1, sizeof(int64_t), a);
  int64_t* blocked_until = (int64_t*)state->thread_depth_blocked_until.ptr;
  for (size_t d = 0; d < state->thread_depth_blocked_until.len; d++) {
    blocked_until[d] = -1;
  }

  const trace_event_persisted_t* events = (const trace_event_persisted_t*)trace_data->events.ptr;
  const size_t* event_indices = (const size_t*)track->event_indices.ptr;
  const int64_t* block_max_durs = (const int64_t*)track->block_max_durs.ptr;
  const uint32_t* depths = (const uint32_t*)track->depths.ptr;
  const uint8_t* bitset = (const uint8_t*)state->selected_events_bitset.ptr;

  // Pass 1: Handle spanning events
  size_t num_blocks = track->block_max_durs.len;
  for (size_t b = 0; b < num_blocks; b++) {
    size_t start_idx = b * TRACK_BLOCK_SIZE;
    size_t end_idx = start_idx + TRACK_BLOCK_SIZE;
    if (end_idx > track->event_indices.len) {
      end_idx = track->event_indices.len;
    }
    int64_t block_last_ts = events[event_indices[end_idx - 1]].ts;

    // If the first event in the block starts after the viewport, we can stop looking.
    if (events[event_indices[start_idx]].ts >= (int64_t)current_bucket_ts) {
      break;
    }

    // Skip block if no event in it can reach viewport_start.
    if (block_last_ts + block_max_durs[b] <= (int64_t)viewport_start) {
      continue;
    }

    // Scan block for spanning events.
    for (size_t i = start_idx; i < end_idx; i++) {
      size_t event_idx = event_indices[i];
      const trace_event_persisted_t* e = &events[event_idx];
      if (e->ts >= (int64_t)current_bucket_ts) break;

      if (e->ts + e->dur > (int64_t)viewport_start) {
        uint32_t depth = depths[i];
        bool is_selected = false;
        if (bitset != nullptr && event_idx < state->selected_events_bitset.len) {
          is_selected = (bitset[event_idx] != 0);
        }
        bool is_focused = (event_idx == (size_t)focused_event_idx);

        float x1 = (float)(tracks_canvas_pos_x +
                           ((double)e->ts - viewport_start) * inv_duration);
        float x2 = (float)(x1 + (double)e->dur * inv_duration);
        if (x2 < x1 + TRACK_MIN_EVENT_WIDTH) x2 = x1 + TRACK_MIN_EVENT_WIDTH;
        track_render_block_t rb = {
            .x1 = x1,
            .x2 = x2,
            .color = e->color,
            .name_ref = e->name_ref,
            .depth = depth,
            .count = 1,
            .is_selected = is_selected,
            .is_focused = is_focused,
            .event_idx = event_idx,
        };
        *array_list_push(out_blocks, track_render_block_t, a) = rb;
        // Re-get pointers in case of resize relocation
        blocked_until = (int64_t*)state->thread_depth_blocked_until.ptr;
        if (e->ts + e->dur > blocked_until[depth]) {
          blocked_until[depth] = e->ts + e->dur;
        }
      }
    }
  }

  // Pass 2: Handle events starting within the viewport using bucketing.
  size_t k = binary_search_events(event_indices, track->event_indices.len, events, (int64_t)current_bucket_ts);

  array_list_resize(&state->thread_bucket_states, track->max_depth + 1, sizeof(thread_bucket_state_t), a);
  thread_bucket_state_t* bucket_states = (thread_bucket_state_t*)state->thread_bucket_states.ptr;
  for (size_t d = 0; d < state->thread_bucket_states.len; d++) {
    bucket_states[d].count = 0;
    bucket_states[d].max_dur = -1;
    bucket_states[d].rep_event_idx = (size_t)-1;
    bucket_states[d].blocked = false;
  }

  blocked_until = (int64_t*)state->thread_depth_blocked_until.ptr;
  bitset = (const uint8_t*)state->selected_events_bitset.ptr;

  while (current_bucket_ts < viewport_end) {
    double next_bucket_ts = current_bucket_ts + bucket_dur;

    for (size_t d = 0; d < state->thread_bucket_states.len; d++) {
      bucket_states[d].blocked = (blocked_until[d] >= (int64_t)next_bucket_ts);
    }

    while (k < track->event_indices.len) {
      size_t event_idx = event_indices[k];
      const trace_event_persisted_t* e = &events[event_idx];
      if (e->ts >= (int64_t)next_bucket_ts) break;

      uint32_t depth = depths[k];
      bool is_selected = false;
      if (bitset != nullptr && event_idx < state->selected_events_bitset.len) {
        is_selected = (bitset[event_idx] != 0);
      }
      bool is_focused = (event_idx == (size_t)focused_event_idx);
      bool is_large = (double)e->dur * inv_duration >= TRACK_MIN_EVENT_WIDTH - 0.01f;

      if (is_selected || is_focused || is_large) {
        track_flush_bucket_depth(out_blocks, viewport_start, inv_duration,
                                 tracks_canvas_pos_x, current_bucket_ts,
                                 next_bucket_ts, depth,
                                 &bucket_states[depth],
                                 trace_data, a);

        float x1 = (float)(tracks_canvas_pos_x +
                           ((double)e->ts - viewport_start) * inv_duration);
        float x2 = (float)(x1 + (double)e->dur * inv_duration);
        if (x2 < x1 + TRACK_MIN_EVENT_WIDTH) x2 = x1 + TRACK_MIN_EVENT_WIDTH;
        track_render_block_t rb = {
            .x1 = x1,
            .x2 = x2,
            .color = e->color,
            .name_ref = e->name_ref,
            .depth = depth,
            .count = 1,
            .is_selected = is_selected,
            .is_focused = is_focused,
            .event_idx = event_idx,
        };
        *array_list_push(out_blocks, track_render_block_t, a) = rb;
        bucket_states[depth].blocked = true;
        if (e->ts + e->dur > blocked_until[depth]) {
          blocked_until[depth] = e->ts + e->dur;
        }
      } else if (!bucket_states[depth].blocked) {
        thread_bucket_state_t* s = &bucket_states[depth];
        if (e->dur > s->max_dur) {
          s->max_dur = e->dur;
          s->rep_event_idx = event_idx;
        }
        s->count++;
      }
      k++;
    }

    // Flush remaining bucket states
    for (size_t d = 0; d < state->thread_bucket_states.len; d++) {
      track_flush_bucket_depth(out_blocks, viewport_start, inv_duration,
                               tracks_canvas_pos_x, current_bucket_ts,
                               next_bucket_ts, (uint32_t)d,
                               &bucket_states[d], trace_data, a);
    }

    current_bucket_ts = next_bucket_ts;
  }

  // Post-processing: merge consecutive blocks
  track_render_block_t* out_blocks_data = (track_render_block_t*)out_blocks->ptr;
  if (out_blocks->len > 1) {
    size_t write_idx = 0;
    for (size_t read_idx = 1; read_idx < out_blocks->len; read_idx++) {
      track_render_block_t* current = &out_blocks_data[write_idx];
      track_render_block_t* next = &out_blocks_data[read_idx];

      if (!current->is_selected && !current->is_focused &&
          !next->is_selected && !next->is_focused &&
          current->depth == next->depth &&
          current->event_idx == next->event_idx) {
        current->x2 = next->x2;
        current->count += next->count;
      } else {
        write_idx++;
        out_blocks_data[write_idx] = *next;
      }
    }
    out_blocks->len = write_idx + 1;
  }
}

void track_compute_counter_render_blocks(
    const track_t* track, const trace_data_t* trace_data, double viewport_start,
    double viewport_end, float width, float tracks_canvas_pos_x,
    int64_t focused_event_idx,
    track_renderer_state_t* state, array_list_t* out_blocks,
    allocator_t a) {
  array_list_clear(out_blocks);
  array_list_clear(&state->counter_peaks);
  if (track->event_indices.len == 0) return;

  const trace_event_persisted_t* events = (const trace_event_persisted_t*)trace_data->events.ptr;
  const size_t* event_indices = (const size_t*)track->event_indices.ptr;
  const string_ref_t* counter_series = (const string_ref_t*)track->counter_series.ptr;

  int64_t track_first_ts = events[event_indices[0]].ts;
  int64_t track_last_ts = events[event_indices[track->event_indices.len - 1]].ts;

  if (viewport_end <= (double)track_first_ts ||
      viewport_start >= (double)track_last_ts) {
    return;
  }

  double duration = viewport_end - viewport_start;
  if (duration <= 0) return;
  double inv_duration = (double)width / duration;

  const float BUCKET_SIZE_PX = 3.0f;
  double bucket_dur = BUCKET_SIZE_PX / inv_duration;

  // Align current_bucket_ts to a multiple of bucket_dur for stability during panning.
  double current_bucket_ts = floor(viewport_start / bucket_dur) * bucket_dur;

  // Fast-forward to the first bucket that could contain data
  if (current_bucket_ts < (double)track_first_ts) {
    current_bucket_ts = floor((double)track_first_ts / bucket_dur) * bucket_dur;
  }

  // Find the initial state
  array_list_resize(&state->counter_current_values, track->counter_series.len, sizeof(double), a);
  double* current_values = (double*)state->counter_current_values.ptr;
  for (size_t i = 0; i < state->counter_current_values.len; i++) {
    current_values[i] = 0.0;
  }

  size_t it_start_idx = binary_search_events(event_indices, track->event_indices.len, events, (int64_t)current_bucket_ts);

  const trace_arg_persisted_t* args = (const trace_arg_persisted_t*)trace_data->args.ptr;

  if (it_start_idx != 0) {
    const trace_event_persisted_t* e = &events[event_indices[it_start_idx - 1]];
    for (uint32_t arg_k = 0; arg_k < e->args_count; arg_k++) {
      const trace_arg_persisted_t* arg = &args[e->args_offset + arg_k];
      for (size_t s_idx = 0; s_idx < track->counter_series.len; s_idx++) {
        if (counter_series[s_idx] == arg->key_ref) {
          current_values[s_idx] = arg->val_double;
          break;
        }
      }
    }
  }

  size_t it_idx = it_start_idx;
  size_t search_end_idx = track->event_indices.len;

  array_list_resize(&state->counter_bucket_max_values, track->counter_series.len, sizeof(double), a);
  array_list_resize(&state->counter_series_updated, track->counter_series.len, sizeof(uint8_t), a);

  double* bucket_max_values = (double*)state->counter_bucket_max_values.ptr;
  uint8_t* series_updated = (uint8_t*)state->counter_series_updated.ptr;
  const uint8_t* bitset = (const uint8_t*)state->selected_events_bitset.ptr;

  while (current_bucket_ts < viewport_end) {
    double next_bucket_ts = current_bucket_ts + bucket_dur;

    // Initialize bucket maximums with carried over values
    for (size_t i = 0; i < state->counter_current_values.len; i++) {
      bucket_max_values[i] = current_values[i];
      series_updated[i] = 0;
    }

    size_t last_event_idx_in_bucket = (size_t)-1;
    bool is_selected = false;
    bool is_focused = false;

    // Consume all events in this bucket
    while (it_idx < search_end_idx &&
           events[event_indices[it_idx]].ts < (int64_t)next_bucket_ts) {
      size_t event_idx = event_indices[it_idx];
      const trace_event_persisted_t* e = &events[event_idx];
      last_event_idx_in_bucket = event_idx;

      if (!is_selected && bitset != nullptr && event_idx < state->selected_events_bitset.len) {
        is_selected = (bitset[event_idx] != 0);
      }
      if (!is_focused) {
        is_focused = (event_idx == (size_t)focused_event_idx);
      }

      for (uint32_t arg_k = 0; arg_k < e->args_count; arg_k++) {
        const trace_arg_persisted_t* arg = &args[e->args_offset + arg_k];
        for (size_t s_idx = 0; s_idx < track->counter_series.len; s_idx++) {
          if (counter_series[s_idx] == arg->key_ref) {
            current_values[s_idx] = arg->val_double;
            if (!series_updated[s_idx]) {
              bucket_max_values[s_idx] = arg->val_double;
              series_updated[s_idx] = 1;
            } else {
              if (current_values[s_idx] > bucket_max_values[s_idx]) {
                bucket_max_values[s_idx] = current_values[s_idx];
              }
            }
            break;
          }
        }
      }
      it_idx++;
    }

    // Determine the end boundary
    double draw_start_ts = current_bucket_ts;
    if (draw_start_ts < (double)track_first_ts) {
      draw_start_ts = (double)track_first_ts;
    }
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
      counter_render_block_t* out_blocks_data = (counter_render_block_t*)out_blocks->ptr;
      if (out_blocks->len > 0) {
        const counter_render_block_t* last_rb = &out_blocks_data[out_blocks->len - 1];
        if (last_rb->event_idx == last_event_idx_in_bucket &&
            last_rb->is_selected == is_selected &&
            last_rb->is_focused == is_focused) {
          can_merge = true;
          size_t series_count = track->counter_series.len;
          size_t last_peaks_offset = (out_blocks->len - 1) * series_count;
          double* peaks = (double*)state->counter_peaks.ptr;
          for (size_t i = 0; i < series_count; i++) {
            if (peaks[last_peaks_offset + i] != bucket_max_values[i]) {
              can_merge = false;
              break;
            }
          }
        }
      }

      if (can_merge) {
        out_blocks_data[out_blocks->len - 1].x2 = x2;
      } else {
        counter_render_block_t rb = {
            .x1 = x1,
            .x2 = x2,
            .is_selected = is_selected,
            .is_focused = is_focused,
            .event_idx = last_event_idx_in_bucket,
        };
        if (last_event_idx_in_bucket == (size_t)-1) {
          if (it_idx != 0) {
            rb.event_idx = event_indices[it_idx - 1];
          } else if (track->event_indices.len > 0 &&
                     draw_start_ts >= (double)track_first_ts) {
            rb.event_idx = event_indices[0];
          }
        }
        *array_list_push(out_blocks, counter_render_block_t, a) = rb;
        for (size_t i = 0; i < state->counter_bucket_max_values.len; i++) {
          *array_list_push(&state->counter_peaks, double, a) = bucket_max_values[i];
        }
      }
    }

    if (hit_track_end) {
      break;
    }

    current_bucket_ts = next_bucket_ts;
  }
}
