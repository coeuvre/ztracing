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
                               false};
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
        m.active = true;
      } else {
        TrackRenderBlock rb = {x1, x2, col, e.name_ref, depth, 1, is_selected};
        array_list_push_back(out_blocks, a, rb);
      }
    }
  }

  // Flush remaining merges
  for (size_t d = 0; d < state->merge_levels.size; d++) {
    TrackMergeBlock& m = state->merge_levels[d];
    if (m.active) {
      TrackRenderBlock rb = {m.x1, m.x2, m.col, m.name_ref, (uint32_t)d, m.count,
                             false};
      array_list_push_back(out_blocks, a, rb);
      m.active = false;
    }
  }
}

void track_compute_counter_render_blocks(
    const Track* track, const TraceData* trace_data, double viewport_start,
    double viewport_end, float width, float tracks_canvas_pos_x,
    ArrayList<CounterRenderBlock>* out_blocks, Allocator a) {
  array_list_clear(out_blocks);
  if (track->event_indices.size == 0) return;

  int64_t track_last_ts =
      trace_data->events[track->event_indices[track->event_indices.size - 1]].ts;

  if ((int64_t)viewport_start >= track_last_ts) {
    CounterRenderBlock rb = {tracks_canvas_pos_x, tracks_canvas_pos_x + width,
                             (size_t)-1};
    array_list_push_back(out_blocks, a, rb);
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

  const size_t* search_start = track->event_indices.data;
  const size_t* search_end =
      track->event_indices.data + track->event_indices.size;

  size_t last_pushed_event_idx = (size_t)-2;

  while (current_bucket_ts < viewport_end) {
    double next_bucket_ts = current_bucket_ts + bucket_dur;

    // Find the first event that falls into the NEXT bucket.
    auto it = std::lower_bound(
        search_start, search_end, (int64_t)next_bucket_ts,
        [&](size_t idx, int64_t val) { return trace_data->events[idx].ts < val; });

    size_t bucket_event_idx = (size_t)-1;
    if (it != track->event_indices.data) {
      bucket_event_idx = *(it - 1);
    }

    // Determine the end boundary for this bucket (clamped to track end)
    double draw_end_ts = next_bucket_ts;
    bool hit_track_end = false;
    if (draw_end_ts >= (double)track_last_ts) {
      draw_end_ts = (double)track_last_ts;
      hit_track_end = true;
    }

    float x1 = (float)(tracks_canvas_pos_x +
                       (current_bucket_ts - viewport_start) * inv_duration);
    float x2 = (float)(tracks_canvas_pos_x +
                       (draw_end_ts - viewport_start) * inv_duration);

    // Clamp to viewport
    if (x1 < tracks_canvas_pos_x) x1 = tracks_canvas_pos_x;
    if (x2 > tracks_canvas_pos_x + width) x2 = tracks_canvas_pos_x + width;

    if (x2 > x1) {
      if (bucket_event_idx == last_pushed_event_idx && out_blocks->size > 0) {
        (*out_blocks)[out_blocks->size - 1].x2 = x2;
      } else {
        CounterRenderBlock rb = {x1, x2, bucket_event_idx};
        array_list_push_back(out_blocks, a, rb);
        last_pushed_event_idx = bucket_event_idx;
      }
    }

    if (hit_track_end) {
      // Fill the rest with a gap
      if (x2 < tracks_canvas_pos_x + width) {
        if (last_pushed_event_idx == (size_t)-1 && out_blocks->size > 0) {
          (*out_blocks)[out_blocks->size - 1].x2 = tracks_canvas_pos_x + width;
        } else {
          CounterRenderBlock rb = {x2, tracks_canvas_pos_x + width, (size_t)-1};
          array_list_push_back(out_blocks, a, rb);
          last_pushed_event_idx = (size_t)-1;
        }
      }
      break;
    }

    search_start = it;
    current_bucket_ts = next_bucket_ts;
  }
}
