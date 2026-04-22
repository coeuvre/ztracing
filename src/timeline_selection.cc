#include "src/timeline_selection.h"

#include <math.h>

#include <algorithm>

TimelineSelectionProximity timeline_selection_check_proximity(
    const TimelineSelectionState& state, double mouse_ts, double threshold_ts) {
  TimelineSelectionProximity result = {false, false};
  if (!state.active) return result;

  double dist_start = std::abs(mouse_ts - state.start_time);
  double dist_end = std::abs(mouse_ts - state.end_time);

  if (dist_start < threshold_ts) result.near_start = true;
  if (dist_end < threshold_ts) result.near_end = true;

  if (result.near_start && result.near_end) {
    if (dist_start < dist_end)
      result.near_end = false;
    else
      result.near_start = false;
  }
  return result;
}

void timeline_selection_handle_ruler_interaction(
    TimelineSelectionState* state, double mouse_ts, bool is_item_active,
    bool is_item_activated, bool is_item_deactivated, float mouse_drag_delta_x,
    float mouse_drag_threshold, const TimelineSelectionProximity& proximity) {
  if (is_item_active) {
    if (is_item_activated) {
      if (proximity.near_start) {
        state->drag_mode = TimelineDragMode::RULER_START;
      } else if (proximity.near_end) {
        state->drag_mode = TimelineDragMode::RULER_END;
      } else {
        state->drag_mode = TimelineDragMode::RULER_NEW;
        state->active = true;
        state->start_time = mouse_ts;
        state->end_time = mouse_ts;
      }
    }

    if (state->drag_mode == TimelineDragMode::RULER_START) {
      state->start_time = mouse_ts;
    } else if (state->drag_mode == TimelineDragMode::RULER_END ||
               state->drag_mode == TimelineDragMode::RULER_NEW) {
      state->end_time = mouse_ts;
    }
  } else {
    if (is_item_deactivated) {
      if (std::abs(mouse_drag_delta_x) < mouse_drag_threshold) {
        if (state->drag_mode == TimelineDragMode::RULER_NEW) {
          state->active = false;
        }
      }
    }

    if (state->drag_mode == TimelineDragMode::RULER_NEW ||
        state->drag_mode == TimelineDragMode::RULER_START ||
        state->drag_mode == TimelineDragMode::RULER_END) {
      state->drag_mode = TimelineDragMode::NONE;
    }
  }
}

void timeline_selection_handle_tracks_interaction(
    TimelineSelectionState* state, double mouse_ts, bool is_mouse_clicked,
    bool is_mouse_down, const TimelineSelectionProximity& proximity) {
  if (state->drag_mode == TimelineDragMode::NONE) {
    if (state->active && is_mouse_clicked &&
        (proximity.near_start || proximity.near_end)) {
      state->drag_mode = proximity.near_start ? TimelineDragMode::TRACKS_START
                                              : TimelineDragMode::TRACKS_END;
    }
  }

  if (state->drag_mode == TimelineDragMode::TRACKS_START ||
      state->drag_mode == TimelineDragMode::TRACKS_END) {
    if (is_mouse_down) {
      if (state->drag_mode == TimelineDragMode::TRACKS_START) {
        state->start_time = mouse_ts;
      } else {
        state->end_time = mouse_ts;
      }
    } else {
      state->drag_mode = TimelineDragMode::NONE;
    }
  }
}
