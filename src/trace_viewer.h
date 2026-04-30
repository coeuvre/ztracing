#ifndef ZTRACING_SRC_TRACE_VIEWER_H_
#define ZTRACING_SRC_TRACE_VIEWER_H_

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "src/allocator.h"
#include "src/array_list.h"
#include "src/colors.h"
#include "src/trace_data.h"
#include "src/track.h"
#include "src/track_renderer.h"

struct DurationHistogramBucket {
  int64_t min_dur;
  int64_t max_dur;
  uint32_t count;
};

struct DurationHistogram {
  static constexpr int MAX_BINS = 32;
  DurationHistogramBucket buckets[MAX_BINS];
  int num_buckets = 0;
  uint32_t max_bucket_count = 0;
  uint32_t total_count = 0;
  bool has_non_zero_durations = false;
};

struct SearchState {
  std::mutex mutex;
  ArrayList<char> pending_query;
  ArrayList<int64_t> pending_results;
  TraceData* td;
  Allocator allocator;
  std::atomic<bool> new_query_available{false};
  std::atomic<bool> jobs_should_abort{false};
  std::atomic<bool> results_ready{false};
  std::atomic<bool> is_searching{false};
  std::atomic<bool> request_update{false};
  std::mutex quit_mutex;
  std::condition_variable quit_cv;

  int sort_column = 0;
  bool sort_ascending = true;
  bool sort_none = true;
  std::atomic<bool> new_sort_specs_available{false};
  std::atomic<bool> new_box_selection_available{false};

  std::atomic<bool> include_thread_events{true};
  std::atomic<bool> include_counter_events{true};

  DurationHistogram pending_histogram = {};
};

enum class InteractionDragMode {
  NONE,
  RULER_NEW,
  RULER_START,
  RULER_END,
  TRACKS_START,
  TRACKS_END,
  BOX_SELECT
};

struct HoverMatch {
  size_t track_idx;
  size_t block_idx;
  float y1, y2;
  TrackRenderBlock rb;
};

struct TraceViewerInput {
  // Layout info
  float canvas_x, canvas_y;
  float canvas_width, canvas_height;
  float ruler_height;
  float lane_height;
  float tracks_scroll_y;

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

struct TrackViewInfo {
  float y;
  float height;
  bool visible;
  char name[128];
};

struct RulerTick {
  float x;
  double ts_rel;
  char label[32];
};

struct SelectionOverlayLayout {
  bool active;
  float x1, x2;
  char duration_label[64];
};

struct TraceViewer {
  struct Viewport {
    int64_t min_ts;
    int64_t max_ts;
    double start_time;
    double end_time;
  } viewport;

  // Timeline selection state
  bool selection_active;
  double selection_start_time;
  double selection_end_time;
  InteractionDragMode selection_drag_mode;

  // Box selection state
  ImVec2 box_select_start;
  ImVec2 box_select_end;

  // Snapping state
  double snap_best_ts;
  float snap_best_dist_px;
  float snap_threshold_px;
  bool snap_has_snap;
  float snap_px;
  float snap_y1;
  float snap_y2;

  ArrayList<Track> tracks;
  ArrayList<TrackViewInfo> track_infos;
  ArrayList<RulerTick> ruler_ticks;
  SelectionOverlayLayout selection_layout;
  float total_tracks_height;

  TrackRendererState track_renderer_state;
  ArrayList<TrackRenderBlock> render_blocks;
  ArrayList<CounterRenderBlock> counter_render_blocks;
  ArrayList<HoverMatch> hover_matches;
  int64_t focused_event_idx = -1;
  ArrayList<int64_t> selected_event_indices;
  int64_t target_focused_event_idx = -1;
  float target_scroll_y = -1.0f;
  bool request_scroll_to_focused_event;
  bool show_details_panel;
  bool ignore_next_release = false;
  float last_inner_width = 0;
  float last_inner_height = 0;
  float last_tracks_x = 0;
  float last_tracks_y = 0;
  float last_lane_height = 0;
  double last_best_snap_ts = 0;
 
  ArrayList<char> search_query;
  bool focus_search_input;
  bool search_thread_events = true;
  bool search_counter_events = true;
  SearchState search;

  bool search_histogram_dirty = true;
  int selected_histogram_bucket = -1;
  DurationHistogram histogram = {};
  ArrayList<int64_t> filtered_event_indices = {};
};

void trace_viewer_deinit(TraceViewer* tv, Allocator allocator);
void trace_viewer_reset_view(TraceViewer* tv);
void trace_viewer_step(TraceViewer* tv, TraceData* td,
                       const TraceViewerInput& input, Allocator allocator);
void trace_viewer_draw(TraceViewer* tv, TraceData* td, Allocator allocator,
                       const Theme* theme);

void trace_viewer_calculate_histogram(const ArrayList<int64_t>& results, const TraceData* td, DurationHistogram* h);

void trace_viewer_search_job(void* user_data);

#endif  // ZTRACING_SRC_TRACE_VIEWER_H_
