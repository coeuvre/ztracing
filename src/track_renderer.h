#ifndef SRC_TRACK_RENDERER_H
#define SRC_TRACK_RENDERER_H

#include <stdint.h>

#include "core/allocator.h"
#include "core/darray.h"
#include "src/trace_data.h"
#include "src/track.h"

#define TRACK_MIN_EVENT_WIDTH 3.0f

typedef struct track_render_block {
  float x1;
  float x2;
  uint8_t palette_index;
  string_ref_t name_ref;
  uint32_t depth;
  uint32_t count;
  bool is_selected;
  bool is_focused;
  size_t event_idx;
} track_render_block_t;

typedef darray_t(track_render_block_t) darray_track_render_block_t;

typedef struct counter_render_block {
  float x1;
  float x2;
  bool is_selected;
  bool is_focused;
  size_t event_idx;
} counter_render_block_t;

typedef darray_t(counter_render_block_t) darray_counter_render_block_t;

typedef struct track_merge_block {
  float x1, x2;
  uint32_t col;
  string_ref_t name_ref;
  uint32_t count;
  size_t event_idx;
  bool active;
} track_merge_block_t;

typedef struct thread_bucket_state {
  size_t rep_event_idx;
  int64_t max_dur;
  uint32_t count;
  bool blocked;
} thread_bucket_state_t;

typedef struct track_renderer_state {
  darray_t(thread_bucket_state_t) thread_bucket_states;
  darray_int64_t thread_depth_blocked_until;
  darray_double_t counter_current_values;
  darray_double_t counter_bucket_max_values;
  darray_uint8_t counter_series_updated;
  darray_double_t counter_peaks;
  darray_float_t counter_visual_offsets;
  darray_uint8_t selected_events_bitset;
} track_renderer_state_t;

static inline void track_renderer_state_deinit(track_renderer_state_t* state,
                                               allocator_t* a) {
  darray_deinit(&state->thread_bucket_states, a);
  darray_deinit(&state->thread_depth_blocked_until, a);
  darray_deinit(&state->counter_current_values, a);
  darray_deinit(&state->counter_bucket_max_values, a);
  darray_deinit(&state->counter_series_updated, a);
  darray_deinit(&state->counter_peaks, a);
  darray_deinit(&state->counter_visual_offsets, a);
  darray_deinit(&state->selected_events_bitset, a);
}

static inline void track_renderer_state_clear(track_renderer_state_t* state) {
  darray_clear(&state->thread_bucket_states);
  darray_clear(&state->thread_depth_blocked_until);
  darray_clear(&state->counter_current_values);
  darray_clear(&state->counter_bucket_max_values);
  darray_clear(&state->counter_series_updated);
  darray_clear(&state->counter_peaks);
  darray_clear(&state->counter_visual_offsets);
  darray_clear(&state->selected_events_bitset);
}

#ifdef __cplusplus
extern "C" {
#endif

void track_renderer_update_selection_bitset(
    track_renderer_state_t* state, const trace_data_t* trace_data,
    const darray_int64_t* selected_event_indices, allocator_t* a);

void track_compute_render_blocks(
    const track_t* track, const trace_data_t* trace_data, double viewport_start,
    double viewport_end, float inner_width, float tracks_canvas_pos_x,
    int64_t focused_event_idx, track_renderer_state_t* state,
    darray_track_render_block_t* out_blocks, allocator_t* a);

void track_compute_counter_render_blocks(
    const track_t* track, const trace_data_t* trace_data, double viewport_start,
    double viewport_end, float inner_width, float tracks_canvas_pos_x,
    int64_t focused_event_idx, track_renderer_state_t* state,
    darray_counter_render_block_t* out_blocks, allocator_t* a);

#ifdef __cplusplus
}
#endif

#endif  // SRC_TRACK_RENDERER_H
