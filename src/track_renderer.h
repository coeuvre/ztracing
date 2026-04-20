#ifndef ZTRACING_SRC_TRACK_RENDERER_H_
#define ZTRACING_SRC_TRACK_RENDERER_H_

#include <stdint.h>

#include "src/array_list.h"
#include "src/trace_data.h"
#include "src/track.h"

#define TRACK_MIN_EVENT_WIDTH 3.0f

struct TrackRenderBlock {
  float x1;
  float x2;
  uint32_t color;
  StringRef name_ref;
  uint32_t depth;
  uint32_t count;
  bool is_selected;
  size_t event_idx;
};

struct CounterRenderBlock {
  float x1;
  float x2;
  size_t event_idx;
};

struct TrackMergeBlock {
  float x1, x2;
  uint32_t col;
  StringRef name_ref;
  uint32_t count;
  size_t event_idx;
  bool active;
};

struct ThreadBucketState {
  size_t rep_event_idx;
  int64_t max_dur;
  uint32_t count;
  bool blocked;
};

struct TrackRendererState {
  ArrayList<ThreadBucketState> thread_bucket_states;
  ArrayList<double> counter_current_values;
  ArrayList<double> counter_bucket_max_values;
  ArrayList<uint8_t> counter_series_updated;
  ArrayList<double> counter_peaks;
  ArrayList<float> counter_visual_offsets;
};

inline void track_renderer_state_deinit(TrackRendererState* state,
                                        Allocator a) {
  array_list_deinit(&state->thread_bucket_states, a);
  array_list_deinit(&state->counter_current_values, a);
  array_list_deinit(&state->counter_bucket_max_values, a);
  array_list_deinit(&state->counter_series_updated, a);
  array_list_deinit(&state->counter_peaks, a);
  array_list_deinit(&state->counter_visual_offsets, a);
}

inline void track_renderer_state_clear(TrackRendererState* state) {
  array_list_clear(&state->thread_bucket_states);
  array_list_clear(&state->counter_current_values);
  array_list_clear(&state->counter_bucket_max_values);
  array_list_clear(&state->counter_series_updated);
  array_list_clear(&state->counter_peaks);
  array_list_clear(&state->counter_visual_offsets);
}

void track_compute_render_blocks(
    const Track* track, const TraceData* trace_data, double viewport_start,
    double viewport_end, float inner_width, float tracks_canvas_pos_x,
    int64_t selected_event_index, TrackRendererState* state,
    ArrayList<TrackRenderBlock>* out_blocks, Allocator a);

void track_compute_counter_render_blocks(
    const Track* track, const TraceData* trace_data, double viewport_start,
    double viewport_end, float width, float tracks_canvas_pos_x,
    TrackRendererState* state, ArrayList<CounterRenderBlock>* out_blocks,
    Allocator a);

#endif  // ZTRACING_SRC_TRACK_RENDERER_H_
