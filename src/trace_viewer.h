#ifndef ZTRACING_SRC_TRACE_VIEWER_H_
#define ZTRACING_SRC_TRACE_VIEWER_H_

#include <pthread.h>

#include "src/allocator.h"
#include "src/array_list.h"
#include "src/colors.h"
#include "src/imgui_c.h"
#include "src/trace_data.h"
#include "src/track.h"
#include "src/track_renderer.h"
#ifdef __cplusplus
#include <atomic>
#ifndef _Atomic
#define _Atomic(T) std::atomic<T>
#endif
#else
#include <stdatomic.h>
#endif

#define tv_atomic_bool _Atomic(bool)
#define tv_atomic_uint32 _Atomic(uint32_t)

constexpr int DURATION_HISTOGRAM_MAX_BINS = 32;

struct duration_histogram_bucket {
  int64_t min_dur;
  int64_t max_dur;
  uint32_t count;
};
typedef struct duration_histogram_bucket duration_histogram_bucket_t;

struct duration_histogram {
#ifdef __cplusplus
  static constexpr int MAX_BINS = 32;
  duration_histogram_bucket_t buckets[MAX_BINS];
#else
  duration_histogram_bucket_t buckets[DURATION_HISTOGRAM_MAX_BINS];
#endif
  int num_buckets;
  uint32_t max_bucket_count;
  uint32_t total_count;
  bool has_non_zero_durations;
};
typedef struct duration_histogram duration_histogram_t;

struct search_state {
  pthread_mutex_t mutex;
  array_list_t pending_query;
  array_list_t pending_results;
  trace_data_t* td;
  allocator_t allocator;
  tv_atomic_bool new_query_available;
  tv_atomic_bool jobs_should_abort;
  tv_atomic_bool results_ready;
  tv_atomic_bool is_searching;
  tv_atomic_bool request_update;
  pthread_mutex_t quit_mutex;
  pthread_cond_t quit_cv;

  int sort_column;
  bool sort_ascending;
  bool sort_none;
  tv_atomic_bool new_sort_specs_available;
  tv_atomic_bool new_box_selection_available;

  tv_atomic_bool include_thread_events;
  tv_atomic_bool include_counter_events;

  duration_histogram_t pending_histogram;
};
typedef struct search_state search_state_t;

typedef enum {
  INTERACTION_DRAG_MODE_NONE,
  INTERACTION_DRAG_MODE_RULER_NEW,
  INTERACTION_DRAG_MODE_RULER_START,
  INTERACTION_DRAG_MODE_RULER_END,
  INTERACTION_DRAG_MODE_TRACKS_START,
  INTERACTION_DRAG_MODE_TRACKS_END,
  INTERACTION_DRAG_MODE_BOX_SELECT
} ig_interaction_drag_mode_t;

struct hover_match {
  size_t track_idx;
  size_t block_idx;
  float y1, y2;
  track_render_block_t rb;
};
typedef struct hover_match hover_match_t;

struct trace_viewer_input {
  // Layout info
  float canvas_x, canvas_y;
  float canvas_width, canvas_height;
  float ruler_height;
  float lane_height;
  float tracks_scroll_y;

  // Parent viewport boundaries (unaltered by child scroll or padding offsets)
  float viewport_x;
  float viewport_y;
  float viewport_width;
  float viewport_height;

  // Interaction info
  float mouse_x, mouse_y;
  float mouse_wheel, mouse_wheel_h;
  float click_x, click_y;
  bool is_mouse_down;
  bool is_mouse_clicked;
  bool is_mouse_double_clicked;
  bool is_mouse_released;
  float mouse_delta_x, mouse_delta_y;
  float drag_delta_x, drag_delta_y;
  float drag_threshold;
  bool is_ctrl_down, is_shift_down;

  // ImGui item states
  bool ruler_active;
  bool ruler_activated;
  bool ruler_deactivated;
  bool tracks_hovered;

  // Derived interaction info
  double mouse_ts;
};
typedef struct trace_viewer_input trace_viewer_input_t;

struct track_view_info {
  float y;
  float y_rel;
  float height;
  bool visible;
  char name[128];
};
typedef struct track_view_info track_view_info_t;

struct ruler_tick {
  float x;
  double ts_rel;
  char label[32];
};
typedef struct ruler_tick ruler_tick_t;

struct selection_overlay_layout {
  bool active;
  float x1, x2;
  char duration_label[64];
};
typedef struct selection_overlay_layout selection_overlay_layout_t;

constexpr float VERTICAL_MINIMAP_WIDTH = 64.0f;
constexpr float VERTICAL_MINIMAP_LANE_HEIGHT = 1.0f;

constexpr int TRACK_HEATMAP_BUCKET_COUNT = 16;

struct track_heatmap {
#ifdef __cplusplus
  static constexpr int BUCKET_COUNT = 16;
  size_t event_indices[BUCKET_COUNT];
#else
  size_t event_indices[TRACK_HEATMAP_BUCKET_COUNT];
#endif
};
typedef struct track_heatmap track_heatmap_t;

struct vertical_minimap_layout {
  bool active;
  float x, y;
  float width, height;
  float minimap_scroll_y;
  float slider_y1;
  float slider_y2;
  bool is_hovered;
};
typedef struct vertical_minimap_layout vertical_minimap_layout_t;

struct vertical_minimap_state {
  bool is_dragging;
  float drag_offset_y;
  array_list_t track_has_selected;
  array_list_t track_heatmap_densities;
  vertical_minimap_layout_t layout;
};
typedef struct vertical_minimap_state vertical_minimap_state_t;

struct trace_viewer {
  struct {
    int64_t min_ts;
    int64_t max_ts;
    double start_time;
    double end_time;
  } viewport;

  // Timeline selection state
  bool selection_active;
  double selection_start_time;
  double selection_end_time;
  ig_interaction_drag_mode_t selection_drag_mode;

  // Box selection state
  ig_vec2_t box_select_start;
  ig_vec2_t box_select_end;

  // Snapping state
  double snap_best_ts;
  float snap_best_dist_px;
  float snap_threshold_px;
  bool snap_has_snap;
  float snap_px;
  float snap_y1;
  float snap_y2;

  array_list_t tracks;
  array_list_t track_infos;
  array_list_t ruler_ticks;
  selection_overlay_layout_t selection_layout;
  float total_tracks_height;

  track_renderer_state_t track_renderer_state;
  array_list_t render_blocks;
  array_list_t counter_render_blocks;
  array_list_t hover_matches;
  int64_t focused_event_idx;
  array_list_t selected_event_indices;
  bool selected_events_dirty;
  int64_t target_focused_event_idx;
  float target_scroll_y;
  bool request_scroll_to_focused_event;
  bool show_details_panel;
  bool ignore_next_release;
  float last_inner_width;
  float last_inner_height;
  float last_tracks_x;
  float last_tracks_y;
  float last_lane_height;
  double last_best_snap_ts;

  array_list_t search_query;
  bool focus_search_input;
  bool search_thread_events;
  bool search_counter_events;
  search_state_t search;

  bool search_histogram_dirty;
  int selected_histogram_bucket;
  duration_histogram_t histogram;
  array_list_t filtered_event_indices;
  vertical_minimap_state_t vertical_minimap;
};
typedef struct trace_viewer trace_viewer_t;

#ifdef __cplusplus
extern "C" {
#endif

void trace_viewer_init(trace_viewer_t* tv);
void trace_viewer_deinit(trace_viewer_t* tv, allocator_t allocator);
void trace_viewer_reset_view(trace_viewer_t* tv);
void trace_viewer_precompute_minimap_heatmap(trace_viewer_t* tv,
                                             const trace_data_t* td,
                                             allocator_t allocator);
void trace_viewer_step(trace_viewer_t* tv, trace_data_t* td,
                       const trace_viewer_input_t* input,
                       allocator_t allocator);
void trace_viewer_draw(trace_viewer_t* tv, trace_data_t* td,
                       allocator_t allocator, const theme_t* theme);

void trace_viewer_calculate_histogram(const array_list_t* results,
                                      const trace_data_t* td,
                                      duration_histogram_t* h);

void trace_viewer_search_job(void* user_data);

#ifdef __cplusplus
}
#endif

#endif  // ZTRACING_SRC_TRACE_VIEWER_H_
