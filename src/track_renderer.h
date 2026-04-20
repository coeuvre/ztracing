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

struct TrackRendererState {
  ArrayList<float> last_x2_per_depth;
  ArrayList<TrackMergeBlock> merge_levels;
  ArrayList<double> counter_values;
  ArrayList<double> counter_peaks;
  ArrayList<float> counter_visual_offsets;
};

inline void track_renderer_state_init(TrackRendererState* state, Allocator a) {
  (void)a;
  memset(state, 0, sizeof(TrackRendererState));
}

inline void track_renderer_state_deinit(TrackRendererState* state,
                                        Allocator a) {
  array_list_deinit(&state->last_x2_per_depth, a);
  array_list_deinit(&state->merge_levels, a);
  array_list_deinit(&state->counter_values, a);
  array_list_deinit(&state->counter_peaks, a);
  array_list_deinit(&state->counter_visual_offsets, a);
}

inline void track_renderer_state_clear(TrackRendererState* state) {
  array_list_clear(&state->last_x2_per_depth);
  array_list_clear(&state->merge_levels);
  array_list_clear(&state->counter_values);
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
    ArrayList<double>* out_peaks, ArrayList<CounterRenderBlock>* out_blocks,
    Allocator a);

#endif  // ZTRACING_SRC_TRACK_RENDERER_H_
