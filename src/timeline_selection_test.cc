#include "src/timeline_selection.h"

#include <gtest/gtest.h>

TEST(TimelineSelectionTest, ClickToClearRuler) {
  TimelineSelectionState state = {};
  state.active = true;
  state.start_time = 100.0;
  state.end_time = 200.0;
  state.drag_mode = TimelineDragMode::NONE;

  TimelineViewportMapping mapping = {0.0, 1000.0, 0.0f, 1000.0f}; // 1 unit per px
  TimelineSnappingState snap = {};
  timeline_snapping_init(&snap, 150.0, 5.0f);

  // Frame 1: Mouse down on ruler (not near boundaries)
  TimelineInteraction i1 = {};
  i1.mouse_px = 150.0f;
  i1.click_px = 150.0f;
  i1.ruler_active = true;
  i1.ruler_activated = true;
  i1.drag_delta_x = 0.0f;
  i1.drag_threshold = 5.0f;

  timeline_selection_step(&state, i1, mapping, snap);
  // active remains true because it was already true, but zero-width selection is NOT yet committed.
  EXPECT_TRUE(state.active);
  EXPECT_EQ(state.drag_mode, TimelineDragMode::RULER_NEW);

  // Frame 2: Mouse release without exceeding drag threshold
  TimelineInteraction i2 = i1;
  i2.ruler_active = false;
  i2.ruler_activated = false;
  i2.ruler_deactivated = true;
  i2.drag_delta_x = 1.0f; // < threshold

  timeline_selection_step(&state, i2, mapping, snap);
  EXPECT_FALSE(state.active);
  EXPECT_EQ(state.drag_mode, TimelineDragMode::NONE);
}

TEST(TimelineSelectionTest, KeepExistingSelectionOnPress) {
  TimelineSelectionState state = {};
  state.active = true;
  state.start_time = 100.0;
  state.end_time = 200.0;
  state.drag_mode = TimelineDragMode::NONE;

  TimelineViewportMapping mapping = {0.0, 1000.0, 0.0f, 1000.0f};
  TimelineSnappingState snap = {};
  timeline_snapping_init(&snap, 150.0, 5.0f);

  // Frame 1: Mouse down on ruler
  TimelineInteraction i1 = {};
  i1.mouse_px = 150.0f;
  i1.click_px = 150.0f;
  i1.ruler_active = true;
  i1.ruler_activated = true;
  i1.drag_delta_x = 0.0f;
  i1.drag_threshold = 5.0f;

  timeline_selection_step(&state, i1, mapping, snap);
  // active remains true, and OLD times remain unchanged!
  EXPECT_TRUE(state.active);
  EXPECT_EQ(state.drag_mode, TimelineDragMode::RULER_NEW);
  EXPECT_DOUBLE_EQ(state.start_time, 100.0);
  EXPECT_DOUBLE_EQ(state.end_time, 200.0);

  // Frame 2: Drag slightly
  TimelineInteraction i2 = i1;
  i2.ruler_activated = false;
  i2.mouse_px = 152.0f;
  i2.drag_delta_x = 2.0f;

  timeline_selection_step(&state, i2, mapping, snap);
  EXPECT_DOUBLE_EQ(state.start_time, 100.0);
  EXPECT_DOUBLE_EQ(state.end_time, 200.0);

  // Frame 3: Drag past threshold -> NOW it updates
  TimelineInteraction i3 = i1;
  i3.ruler_activated = false;
  i3.mouse_px = 160.0f;
  i3.drag_delta_x = 10.0f;

  TimelineSnappingState snap3 = {};
  timeline_snapping_init(&snap3, 160.0, 5.0f);

  timeline_selection_step(&state, i3, mapping, snap3);
  EXPECT_DOUBLE_EQ(state.start_time, 150.0);
  EXPECT_DOUBLE_EQ(state.end_time, 160.0);
}

TEST(TimelineSelectionTest, ViewportZoomClamping) {
  ViewportState vs = {0.0, 1000.0, 0, 1000000};
  TimelineSelectionState sel = {};
  sel.active = true;
  sel.start_time = 400.0;
  sel.end_time = 600.0;

  TimelineViewportMapping mapping = {0.0, 1000.0, 0.0f, 1000.0f};
  TimelineInteraction interaction = {};
  interaction.tracks_hovered = true;
  interaction.mouse_wheel = -1.0f; // Zoom out
  interaction.is_ctrl_down = true;
  interaction.mouse_px = 500.0f; // Zoom around center

  viewport_step(&vs, interaction, mapping, sel);

  // Viewport duration should increase, but clamped to keep 400-600 visible.
  // With zoom_factor 1.2, new_duration = 1200.
  EXPECT_DOUBLE_EQ(vs.end_time - vs.start_time, 1200.0);
  EXPECT_LE(vs.start_time, 400.0);
  EXPECT_GE(vs.end_time, 600.0);
}

TEST(TimelineSelectionTest, NoZeroWidthSelectionOnPress) {
  TimelineSelectionState state = {};
  state.active = false; // Start with NO selection

  TimelineViewportMapping mapping = {0.0, 1000.0, 0.0f, 1000.0f};
  TimelineSnappingState snap = {};
  timeline_snapping_init(&snap, 150.0, 5.0f);

  // Frame 1: Mouse down on ruler
  TimelineInteraction i1 = {};
  i1.mouse_px = 150.0f;
  i1.click_px = 150.0f;
  i1.ruler_active = true;
  i1.ruler_activated = true;
  i1.drag_delta_x = 0.0f;
  i1.drag_threshold = 5.0f;

  timeline_selection_step(&state, i1, mapping, snap);
  // active MUST remain false until dragging starts!
  EXPECT_FALSE(state.active);
  EXPECT_EQ(state.drag_mode, TimelineDragMode::RULER_NEW);
  // times should stay at 0 if active was false!
  EXPECT_DOUBLE_EQ(state.start_time, 0.0);
  EXPECT_DOUBLE_EQ(state.end_time, 0.0);

  // Frame 2: Drag slightly (below threshold)
  TimelineInteraction i2 = i1;
  i2.ruler_activated = false;
  i2.mouse_px = 152.0f;
  i2.drag_delta_x = 2.0f;

  timeline_selection_step(&state, i2, mapping, snap);
  EXPECT_FALSE(state.active);
  EXPECT_DOUBLE_EQ(state.start_time, 0.0);

  // Frame 3: Drag past threshold
  TimelineInteraction i3 = i1;
  i3.ruler_activated = false;
  i3.mouse_px = 160.0f;
  i3.drag_delta_x = 10.0f;

  TimelineSnappingState snap3 = {};
  timeline_snapping_init(&snap3, 160.0, 5.0f);

  timeline_selection_step(&state, i3, mapping, snap3);
  EXPECT_TRUE(state.active);
  EXPECT_DOUBLE_EQ(state.start_time, 150.0);
  EXPECT_DOUBLE_EQ(state.end_time, 160.0);
}

TEST(TimelineSelectionTest, SnappingWithHighlight) {
  TimelineSnappingState snap = {};
  timeline_snapping_init(&snap, 100.0, 5.0f);
  EXPECT_FALSE(snap.has_snap);

  // Suggest a candidate within threshold
  timeline_snapping_suggest(&snap, 102.0, 102.0f, 100.0f, 10.0f, 20.0f);
  EXPECT_TRUE(snap.has_snap);
  EXPECT_FLOAT_EQ(snap.snap_px, 102.0f);
  EXPECT_FLOAT_EQ(snap.snap_y1, 10.0f);
  EXPECT_FLOAT_EQ(snap.snap_y2, 20.0f);

  // Suggest a closer candidate
  timeline_snapping_suggest(&snap, 99.0, 99.0f, 100.0f, 30.0f, 40.0f);
  EXPECT_TRUE(snap.has_snap);
  EXPECT_FLOAT_EQ(snap.snap_px, 99.0f);
  EXPECT_FLOAT_EQ(snap.snap_y1, 30.0f);
  EXPECT_FLOAT_EQ(snap.snap_y2, 40.0f);
}

TEST(TimelineSelectionTest, PreciseStartAndSnappedDrag) {
  TimelineSelectionState state = {};
  state.active = true; // start with it true to verify it keeps old values
  state.start_time = 10.0;
  state.end_time = 20.0;

  TimelineViewportMapping mapping = {0.0, 1000.0, 0.0f, 1000.0f};
  TimelineSnappingState snap = {};

  // Frame 1: Start selection at 100.0
  timeline_snapping_init(&snap, 100.0, 5.0f);
  timeline_snapping_suggest(&snap, 102.0, 102.0f, 100.0f, 0.0f, 0.0f);

  TimelineInteraction i1 = {};
  i1.mouse_px = 100.0f;
  i1.click_px = 100.0f;
  i1.ruler_active = true;
  i1.ruler_activated = true;
  i1.drag_threshold = 5.0f;

  timeline_selection_step(&state, i1, mapping, snap);
  // Still 10/20 because we haven't dragged!
  EXPECT_DOUBLE_EQ(state.start_time, 10.0);
  EXPECT_DOUBLE_EQ(state.end_time, 20.0);

  // Frame 2: Drag to 108.0 (total delta 8px > 5px threshold)
  timeline_snapping_init(&snap, 108.0, 5.0f);
  timeline_snapping_suggest(&snap, 110.0, 110.0f, 108.0f, 0.0f, 0.0f);

  TimelineInteraction i2 = i1;
  i2.ruler_activated = false;
  i2.mouse_px = 108.0f;
  i2.drag_delta_x = 8.0f;

  timeline_selection_step(&state, i2, mapping, snap);
  EXPECT_TRUE(state.active);
  EXPECT_DOUBLE_EQ(state.start_time, 100.0); // Precise from click_px
  EXPECT_DOUBLE_EQ(state.end_time, 110.0); // Snapped
}

TEST(TimelineSelectionTest, SnappedBoundaryDragTracks) {
  TimelineSelectionState state = {};
  state.active = true;
  state.start_time = 100.0;
  state.end_time = 200.0;

  TimelineViewportMapping mapping = {0.0, 1000.0, 0.0f, 1000.0f};
  TimelineSnappingState snap = {};

  // Frame 1: Click near start (100.0) in tracks
  timeline_snapping_init(&snap, 101.0, 5.0f);
  
  TimelineInteraction i1 = {};
  i1.tracks_hovered = true;
  i1.mouse_px = 101.0f;
  i1.click_px = 101.0f;
  i1.is_mouse_clicked = true;
  i1.is_mouse_down = true;

  timeline_selection_step(&state, i1, mapping, snap);
  EXPECT_EQ(state.drag_mode, TimelineDragMode::TRACKS_START);
  EXPECT_DOUBLE_EQ(state.start_time, 101.0); // Precise on first click

  // Frame 2: Drag start towards 50.0, snap to 48.0
  timeline_snapping_init(&snap, 50.0, 5.0f);
  timeline_snapping_suggest(&snap, 48.0, 48.0f, 50.0f, 0.0f, 0.0f);

  TimelineInteraction i2 = i1;
  i2.is_mouse_clicked = false;
  i2.mouse_px = 50.0f;

  timeline_selection_step(&state, i2, mapping, snap);
  EXPECT_DOUBLE_EQ(state.start_time, 48.0);
  EXPECT_DOUBLE_EQ(state.end_time, 200.0);
}

TEST(TimelineSelectionTest, SnappedBoundaryDragTracksEnd) {
  TimelineSelectionState state = {};
  state.active = true;
  state.start_time = 100.0;
  state.end_time = 200.0;

  TimelineViewportMapping mapping = {0.0, 1000.0, 0.0f, 1000.0f};
  TimelineSnappingState snap = {};

  // Frame 1: Click near end (200.0) in tracks
  timeline_snapping_init(&snap, 199.0, 5.0f);
  
  TimelineInteraction i1 = {};
  i1.tracks_hovered = true;
  i1.mouse_px = 199.0f;
  i1.click_px = 199.0f;
  i1.is_mouse_clicked = true;
  i1.is_mouse_down = true;

  timeline_selection_step(&state, i1, mapping, snap);
  EXPECT_EQ(state.drag_mode, TimelineDragMode::TRACKS_END);
  EXPECT_DOUBLE_EQ(state.end_time, 199.0);

  // Frame 2: Drag end towards 250.0, snap to 252.0
  timeline_snapping_init(&snap, 250.0, 5.0f);
  timeline_snapping_suggest(&snap, 252.0, 252.0f, 250.0f, 0.0f, 0.0f);

  TimelineInteraction i2 = i1;
  i2.is_mouse_clicked = false;
  i2.mouse_px = 250.0f;

  timeline_selection_step(&state, i2, mapping, snap);
  EXPECT_DOUBLE_EQ(state.start_time, 100.0);
  EXPECT_DOUBLE_EQ(state.end_time, 252.0);
}

TEST(TimelineSelectionTest, ProximityCheck) {
  TimelineSelectionState state = {};
  state.active = true;
  state.start_time = 100.0;
  state.end_time = 200.0;

  auto p1 = timeline_selection_check_proximity(state, 102.0, 5.0);
  EXPECT_TRUE(p1.near_start);
  EXPECT_FALSE(p1.near_end);

  auto p2 = timeline_selection_check_proximity(state, 198.0, 5.0);
  EXPECT_FALSE(p2.near_start);
  EXPECT_TRUE(p2.near_end);

  auto p3 = timeline_selection_check_proximity(state, 150.0, 5.0);
  EXPECT_FALSE(p3.near_start);
  EXPECT_FALSE(p3.near_end);
}

TEST(TimelineSelectionTest, AreaIsolation) {
  TimelineSelectionState state = {};
  TimelineViewportMapping mapping = {0.0, 1000.0, 0.0f, 1000.0f};
  TimelineSnappingState snap = {};
  timeline_snapping_init(&snap, 100.0, 5.0f);

  // Interaction in tracks area when nothing is active
  // Should NOT start a new selection (only Ruler creates NEW)
  TimelineInteraction i1 = {};
  i1.tracks_hovered = true;
  i1.mouse_px = 100.0f;
  i1.is_mouse_clicked = true;
  i1.is_mouse_down = true;

  timeline_selection_step(&state, i1, mapping, snap);
  EXPECT_FALSE(state.active);
  EXPECT_EQ(state.drag_mode, TimelineDragMode::NONE);
}

TEST(TimelineSelectionTest, IsMouseInside) {
  TimelineSelectionState state = {};
  state.active = true;
  state.start_time = 100.0;
  state.end_time = 200.0;

  EXPECT_TRUE(timeline_selection_is_mouse_inside(state, 150.0));
  EXPECT_TRUE(timeline_selection_is_mouse_inside(state, 100.0));
  EXPECT_TRUE(timeline_selection_is_mouse_inside(state, 200.0));
  EXPECT_FALSE(timeline_selection_is_mouse_inside(state, 50.0));
  EXPECT_FALSE(timeline_selection_is_mouse_inside(state, 250.0));

  // Swapped boundaries
  state.start_time = 200.0;
  state.end_time = 100.0;
  EXPECT_TRUE(timeline_selection_is_mouse_inside(state, 150.0));
  EXPECT_FALSE(timeline_selection_is_mouse_inside(state, 250.0));

  // Inactive selection (interaction should be enabled everywhere)
  state.active = false;
  EXPECT_TRUE(timeline_selection_is_mouse_inside(state, 50.0));
  EXPECT_TRUE(timeline_selection_is_mouse_inside(state, 1000.0));
}
