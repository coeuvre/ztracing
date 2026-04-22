#ifndef ZTRACING_SRC_TRACE_VIEWER_H_
#define ZTRACING_SRC_TRACE_VIEWER_H_

#include "src/allocator.h"
#include "src/array_list.h"
#include "src/colors.h"
#include "src/timeline_selection.h"
#include "src/trace_data.h"
#include "src/track.h"
#include "src/track_renderer.h"

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
};

struct TraceViewer {
  struct Viewport {
    int64_t min_ts;
    int64_t max_ts;
    double start_time;
    double end_time;
  } viewport;

  TimelineSelectionState timeline_selection;

  ArrayList<Track> tracks;
  TrackRendererState track_renderer_state;
  ArrayList<TrackRenderBlock> render_blocks;
  ArrayList<CounterRenderBlock> counter_render_blocks;
  ArrayList<HoverMatch> hover_matches;
  TimelineSnappingState snapping;
  int64_t selected_event_index = -1;
  bool show_details_panel;
  float last_inner_width = 0;
  float last_tracks_x = 0;
  double last_best_snap_ts = 0;
};

void trace_viewer_deinit(TraceViewer* tv, Allocator allocator);
void trace_viewer_reset_view(TraceViewer* tv);
void trace_viewer_step(TraceViewer* tv, TraceData* td,
                       const TraceViewerInput& input, Allocator allocator);
void trace_viewer_draw(TraceViewer* tv, TraceData* td, Allocator allocator,
                       const Theme* theme);

#endif  // ZTRACING_SRC_TRACE_VIEWER_H_
