#ifndef ZTRACING_SRC_TIMELINE_SELECTION_H_
#define ZTRACING_SRC_TIMELINE_SELECTION_H_

#include <stdint.h>

enum class TimelineDragMode {
  NONE,
  RULER_NEW,
  RULER_START,
  RULER_END,
  TRACKS_START,
  TRACKS_END
};

struct TimelineSelectionState {
  bool active;
  double start_time;
  double end_time;
  TimelineDragMode drag_mode;
};

struct TimelineSelectionProximity {
  bool near_start;
  bool near_end;
};

struct TimelineViewportMapping {
  double start_time;
  double end_time;
  float origin_x;
  float width;
};

struct ViewportState {
  double start_time;
  double end_time;
  int64_t min_ts;
  int64_t max_ts;
};

struct TimelineInteraction {
  enum Area { AREA_RULER, AREA_TRACKS } area;
  double mouse_ts;
  float mouse_px;
  float click_px;
  bool is_mouse_down;
  bool is_mouse_clicked;
  bool is_mouse_released;
  float mouse_wheel;
  float mouse_wheel_h;
  bool is_ctrl_down;
  bool is_shift_down;
  float drag_delta_x;
  float drag_threshold;

  bool ruler_active;
  bool ruler_activated;
  bool ruler_deactivated;
  bool tracks_hovered;
};

struct TimelineSnappingState {
  double best_snap_ts;
  float best_snap_dist_px;
  float threshold_px;

  // Highlighting info
  bool has_snap;
  float snap_px;
  float snap_y1;
  float snap_y2;
};

void timeline_snapping_init(TimelineSnappingState* state, double mouse_ts,
                            float threshold_px);
void timeline_snapping_suggest(TimelineSnappingState* state, double candidate_ts,
                               float candidate_px, float mouse_px, float y1,
                               float y2);

double timeline_mapping_px_to_ts(const TimelineViewportMapping& m, float px);

TimelineSelectionProximity timeline_selection_check_proximity(
    const TimelineSelectionState& state, double mouse_ts, double threshold_ts);

bool timeline_selection_is_mouse_inside(const TimelineSelectionState& state,
                                        double mouse_ts);

void timeline_selection_step(TimelineSelectionState* state,
                             const TimelineInteraction& interaction,
                             const TimelineViewportMapping& mapping,
                             const TimelineSnappingState& snapping);

void viewport_step(ViewportState* viewport,
                   const TimelineInteraction& interaction,
                   const TimelineViewportMapping& mapping,
                   const TimelineSelectionState& selection);

#endif  // ZTRACING_SRC_TIMELINE_SELECTION_H_
