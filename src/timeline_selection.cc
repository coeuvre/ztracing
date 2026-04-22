#include "src/timeline_selection.h"

#include <math.h>

#include <algorithm>

void timeline_snapping_init(TimelineSnappingState* state, double mouse_ts,
                            float threshold_px) {
  state->best_snap_ts = mouse_ts;
  state->best_snap_dist_px = threshold_px;
  state->threshold_px = threshold_px;
  state->has_snap = false;
  state->snap_px = 0.0f;
  state->snap_y1 = 0.0f;
  state->snap_y2 = 0.0f;
}

void timeline_snapping_suggest(TimelineSnappingState* state, double candidate_ts,
                               float candidate_px, float mouse_px, float y1,
                               float y2) {
  float dist_px = std::abs(candidate_px - mouse_px);
  if (dist_px < state->best_snap_dist_px) {
    state->best_snap_dist_px = dist_px;
    state->best_snap_ts = candidate_ts;
    state->has_snap = true;
    state->snap_px = candidate_px;
    state->snap_y1 = y1;
    state->snap_y2 = y2;
  }
}

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

double timeline_mapping_px_to_ts(const TimelineViewportMapping& m,
                                        float px) {
  double duration = m.end_time - m.start_time;
  if (m.width <= 0) return m.start_time;
  return m.start_time + ((double)(px - m.origin_x) / (double)m.width) * duration;
}

bool timeline_selection_is_mouse_inside(const TimelineSelectionState& state,
                                        double mouse_ts) {
  if (!state.active) return true;
  double t1 = state.start_time;
  double t2 = state.end_time;
  if (t1 > t2) std::swap(t1, t2);
  return mouse_ts >= t1 && mouse_ts <= t2;
}

void timeline_selection_step(TimelineSelectionState* state,
                             const TimelineInteraction& interaction,
                             const TimelineViewportMapping& mapping,
                             const TimelineSnappingState& snapping) {
  double mouse_ts = timeline_mapping_px_to_ts(mapping, interaction.mouse_px);
  double threshold_ts =
      ((double)snapping.threshold_px / (double)mapping.width) *
      (mapping.end_time - mapping.start_time);

  TimelineSelectionProximity proximity =
      timeline_selection_check_proximity(*state, mouse_ts, threshold_ts);

  // Ruler Interaction Logic
  if (interaction.ruler_active) {
    if (interaction.ruler_activated) {
      if (proximity.near_start) {
        state->drag_mode = TimelineDragMode::RULER_START;
      } else if (proximity.near_end) {
        state->drag_mode = TimelineDragMode::RULER_END;
      } else {
        state->drag_mode = TimelineDragMode::RULER_NEW;
      }
    }

    if (state->drag_mode == TimelineDragMode::RULER_NEW) {
      if (std::abs(interaction.drag_delta_x) >= interaction.drag_threshold) {
        state->active = true;
        state->start_time = timeline_mapping_px_to_ts(mapping, interaction.click_px);
        state->end_time = snapping.best_snap_ts;
      }
    } else if (state->drag_mode == TimelineDragMode::RULER_START) {
      state->start_time = snapping.best_snap_ts;
    } else if (state->drag_mode == TimelineDragMode::RULER_END) {
      state->end_time = snapping.best_snap_ts;
    }
  } else {
    if (interaction.ruler_deactivated) {
      if (std::abs(interaction.drag_delta_x) < interaction.drag_threshold) {
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

  // Tracks Interaction Logic
  if (interaction.tracks_hovered && !interaction.ruler_active) {
    if (state->drag_mode == TimelineDragMode::NONE) {
      if (state->active && interaction.is_mouse_clicked &&
          (proximity.near_start || proximity.near_end)) {
        state->drag_mode = proximity.near_start ? TimelineDragMode::TRACKS_START
                                                : TimelineDragMode::TRACKS_END;
      }
    }

    if (state->drag_mode == TimelineDragMode::TRACKS_START ||
        state->drag_mode == TimelineDragMode::TRACKS_END) {
      if (interaction.is_mouse_down) {
        // Initial click in tracks area is also precise
        double ts = interaction.is_mouse_clicked ? mouse_ts
                                                 : snapping.best_snap_ts;
        if (state->drag_mode == TimelineDragMode::TRACKS_START) {
          state->start_time = ts;
        } else {
          state->end_time = ts;
        }
      } else {
        state->drag_mode = TimelineDragMode::NONE;
      }
    }
  }
}

#define TRACE_VIEWER_MAX_ZOOM_FACTOR 1.2
#define TRACE_VIEWER_MIN_ZOOM_DURATION 1000.0

void viewport_step(ViewportState* viewport,
                   const TimelineInteraction& interaction,
                   const TimelineViewportMapping& mapping,
                   const TimelineSelectionState& selection) {
  if (!interaction.tracks_hovered || interaction.ruler_active) return;

  double current_duration = viewport->end_time - viewport->start_time;

  // Panning handled externally in trace_viewer.cc for now because it uses MouseDelta
  // which is not in TimelineInteraction yet.
  
  // Zooming
  if (interaction.mouse_wheel != 0.0f && interaction.is_ctrl_down) {
    double mouse_x_rel = (double)(interaction.mouse_px - mapping.origin_x) / (double)mapping.width;
    double mouse_ts = viewport->start_time + mouse_x_rel * current_duration;
    double zoom_factor = (interaction.mouse_wheel > 0.0f) ? 0.8 : TRACE_VIEWER_MAX_ZOOM_FACTOR;
    double new_duration = current_duration * zoom_factor;

    double trace_duration = (double)(viewport->max_ts - viewport->min_ts);
    double max_duration = trace_duration * TRACE_VIEWER_MAX_ZOOM_FACTOR;
    double min_duration = TRACE_VIEWER_MIN_ZOOM_DURATION;

    if (selection.active) {
      double t1 = selection.start_time;
      double t2 = selection.end_time;
      if (t1 > t2) std::swap(t1, t2);
      double sel_dur = t2 - t1;
      if (sel_dur > 0) {
        if (sel_dur > min_duration) min_duration = sel_dur;
        double sel_max_dur = sel_dur * 10.0;
        if (sel_max_dur < max_duration) max_duration = sel_max_dur;
      }
    }

    if (max_duration < min_duration) max_duration = min_duration;
    if (new_duration < min_duration) new_duration = min_duration;
    if (new_duration > max_duration) new_duration = max_duration;

    viewport->start_time = mouse_ts - mouse_x_rel * new_duration;

    if (selection.active) {
      double t1 = selection.start_time;
      double t2 = selection.end_time;
      if (t1 > t2) std::swap(t1, t2);
      if (viewport->start_time > t1) viewport->start_time = t1;
      if (viewport->start_time + new_duration < t2)
        viewport->start_time = t2 - new_duration;
    }
    viewport->end_time = viewport->start_time + new_duration;
  }
}
