#ifndef ZTRACING_SRC_TIMELINE_SELECTION_H_
#define ZTRACING_SRC_TIMELINE_SELECTION_H_

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

TimelineSelectionProximity timeline_selection_check_proximity(
    const TimelineSelectionState& state, double mouse_ts, double threshold_ts);

void timeline_selection_handle_ruler_interaction(
    TimelineSelectionState* state, double mouse_ts, bool is_item_active,
    bool is_item_activated, bool is_item_deactivated, float mouse_drag_delta_x,
    float mouse_drag_threshold, const TimelineSelectionProximity& proximity);

void timeline_selection_handle_tracks_interaction(
    TimelineSelectionState* state, double mouse_ts, bool is_mouse_clicked,
    bool is_mouse_down, const TimelineSelectionProximity& proximity);

#endif  // ZTRACING_SRC_TIMELINE_SELECTION_H_
