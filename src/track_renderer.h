#ifndef ZTRACING_SRC_TRACK_RENDERER_H_
#define ZTRACING_SRC_TRACK_RENDERER_H_

#include <stdint.h>
#include "src/array_list.h"
#include "src/track.h"
#include "src/trace_data.h"

struct TrackRenderBlock {
  float x1;
  float x2;
  uint32_t color;
  StringRef name_ref; // 0 if it's a merged block
  uint32_t depth;
  bool is_selected;
};

struct TrackMergeBlock {
  float x1, x2;
  uint32_t col;
  bool active;
};

struct TrackRendererState {
  ArrayList<float> last_x2_per_depth;
  ArrayList<TrackMergeBlock> merge_levels;
};

inline void track_renderer_state_init(TrackRendererState* state, Allocator a) {
  (void)a;
  memset(state, 0, sizeof(TrackRendererState));
}

inline void track_renderer_state_deinit(TrackRendererState* state, Allocator a) {
  array_list_deinit(&state->last_x2_per_depth, a);
  array_list_deinit(&state->merge_levels, a);
}

inline void track_renderer_state_clear(TrackRendererState* state) {
  array_list_clear(&state->last_x2_per_depth);
  array_list_clear(&state->merge_levels);
}

void track_compute_render_blocks(
    const Track* track,
    const TraceData* trace_data,
    double viewport_start,
    double viewport_end,
    float inner_width,
    float tracks_canvas_pos_x,
    int64_t selected_event_index,
    TrackRendererState* state,
    ArrayList<TrackRenderBlock>* out_blocks,
    Allocator a);

#endif  // ZTRACING_SRC_TRACK_RENDERER_H_
