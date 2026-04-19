#ifndef ZTRACING_SRC_TRACE_VIEWER_H_
#define ZTRACING_SRC_TRACE_VIEWER_H_

#include "src/allocator.h"
#include "src/array_list.h"
#include "src/colors.h"
#include "src/trace_data.h"
#include "src/track.h"
#include "src/track_renderer.h"

struct HoverMatch {
  size_t track_idx;
  size_t block_idx;
  float y1, y2;
  TrackRenderBlock rb;
};

struct TraceViewer {
  struct Viewport {
    int64_t min_ts;
    int64_t max_ts;
    double start_time;
    double end_time;
  } viewport;

  ArrayList<Track> tracks;
  TrackRendererState track_renderer_state;
  ArrayList<TrackRenderBlock> render_blocks;
  ArrayList<CounterRenderBlock> counter_render_blocks;
  ArrayList<HoverMatch> hover_matches;
  int64_t selected_event_index;
  bool show_details_panel;
};

void trace_viewer_init(TraceViewer* tv, Allocator allocator);
void trace_viewer_deinit(TraceViewer* tv, Allocator allocator);
void trace_viewer_draw(TraceViewer* tv, TraceData* td, Allocator allocator,
                       const Theme* theme);

#endif  // ZTRACING_SRC_TRACE_VIEWER_H_
