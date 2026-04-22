#include "src/timeline_selection.h"

#include <gtest/gtest.h>

TEST(TimelineSelectionTest, NewSelection) {
  TimelineSelectionState state = {};
  TimelineSelectionProximity proximity = {false, false};

  // Start new selection
  timeline_selection_handle_ruler_interaction(&state, 100.0, true, true, false,
                                              0.0, 5.0, proximity);
  EXPECT_TRUE(state.active);
  EXPECT_EQ(state.drag_mode, TimelineDragMode::RULER_NEW);
  EXPECT_DOUBLE_EQ(state.start_time, 100.0);
  EXPECT_DOUBLE_EQ(state.end_time, 100.0);

  // Drag
  timeline_selection_handle_ruler_interaction(&state, 200.0, true, false, false,
                                              100.0, 5.0, proximity);
  EXPECT_DOUBLE_EQ(state.start_time, 100.0);
  EXPECT_DOUBLE_EQ(state.end_time, 200.0);

  // Finish
  timeline_selection_handle_ruler_interaction(&state, 200.0, false, false, true,
                                              100.0, 5.0, proximity);
  EXPECT_TRUE(state.active);
  EXPECT_EQ(state.drag_mode, TimelineDragMode::NONE);
}

TEST(TimelineSelectionTest, CancelNewSelectionOnSimpleClick) {
  TimelineSelectionState state = {};
  TimelineSelectionProximity proximity = {false, false};

  // Click
  timeline_selection_handle_ruler_interaction(&state, 100.0, true, true, false,
                                              0.0, 5.0, proximity);
  // Release without dragging
  timeline_selection_handle_ruler_interaction(&state, 100.0, false, false, true,
                                              0.0, 5.0, proximity);
  EXPECT_FALSE(state.active);
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

TEST(TimelineSelectionTest, RulerDragsBoundaries) {
  TimelineSelectionState state = {};
  state.active = true;
  state.start_time = 100.0;
  state.end_time = 200.0;

  TimelineSelectionProximity proximity = {true, false};

  // Click near start on ruler
  timeline_selection_handle_ruler_interaction(&state, 100.0, true, true, false,
                                              0.0, 5.0, proximity);
  // It should start dragging START
  EXPECT_EQ(state.drag_mode, TimelineDragMode::RULER_START);
  EXPECT_DOUBLE_EQ(state.start_time, 100.0);
  EXPECT_DOUBLE_EQ(state.end_time, 200.0);
}

TEST(TimelineSelectionTest, DragStartBoundaryTracks) {
  TimelineSelectionState state = {};
  state.active = true;
  state.start_time = 100.0;
  state.end_time = 200.0;

  TimelineSelectionProximity proximity = {true, false};

  // Click near start
  timeline_selection_handle_tracks_interaction(&state, 100.0, true, true,
                                                proximity);
  EXPECT_EQ(state.drag_mode, TimelineDragMode::TRACKS_START);

  // Drag start
  timeline_selection_handle_tracks_interaction(&state, 50.0, false, true,
                                                proximity);
  EXPECT_DOUBLE_EQ(state.start_time, 50.0);
  EXPECT_DOUBLE_EQ(state.end_time, 200.0);

  // Release
  timeline_selection_handle_tracks_interaction(&state, 50.0, false, false,
                                                proximity);
  EXPECT_EQ(state.drag_mode, TimelineDragMode::NONE);
}

TEST(TimelineSelectionTest, DragEndBoundaryTracks) {
  TimelineSelectionState state = {};
  state.active = true;
  state.start_time = 100.0;
  state.end_time = 200.0;
  state.drag_mode = TimelineDragMode::NONE;

  TimelineSelectionProximity proximity = {false, true};

  // Mouse down near end
  timeline_selection_handle_tracks_interaction(&state, 200.0, true, true,
                                               proximity);
  EXPECT_EQ(state.drag_mode, TimelineDragMode::TRACKS_END);
  EXPECT_DOUBLE_EQ(state.end_time, 200.0);

  // Drag end
  timeline_selection_handle_tracks_interaction(&state, 250.0, false, true,
                                               proximity);
  EXPECT_EQ(state.drag_mode, TimelineDragMode::TRACKS_END);
  EXPECT_DOUBLE_EQ(state.start_time, 100.0);
  EXPECT_DOUBLE_EQ(state.end_time, 250.0);

  // Release
  timeline_selection_handle_tracks_interaction(&state, 250.0, false, false,
                                               proximity);
  EXPECT_EQ(state.drag_mode, TimelineDragMode::NONE);
}

TEST(TimelineSelectionTest, RulerTracksConflict) {
  TimelineSelectionState state = {};
  state.active = true;
  state.start_time = 100.0;
  state.end_time = 200.0;
  state.drag_mode = TimelineDragMode::NONE;

  TimelineSelectionProximity proximity_r = {false, false};
  TimelineSelectionProximity proximity_t = {true, false};

  // Frame 1: Click in tracks area near start
  // Ruler is NOT active
  timeline_selection_handle_ruler_interaction(&state, 100.0, false, false, false, 0.0, 5.0, proximity_r);
  EXPECT_EQ(state.drag_mode, TimelineDragMode::NONE);
  
  // Tracks area interaction
  timeline_selection_handle_tracks_interaction(&state, 100.0, true, true, proximity_t);
  EXPECT_EQ(state.drag_mode, TimelineDragMode::TRACKS_START);

  // Frame 2: Mouse is down, dragging in tracks area
  // Ruler is NOT active
  timeline_selection_handle_ruler_interaction(&state, 100.0, false, false, false, 0.0, 5.0, proximity_r);
  
  // FIXED! It should remain in TRACKS_START
  EXPECT_EQ(state.drag_mode, TimelineDragMode::TRACKS_START);
  
  timeline_selection_handle_tracks_interaction(&state, 150.0, false, true, proximity_t);
  EXPECT_EQ(state.drag_mode, TimelineDragMode::TRACKS_START);
  EXPECT_DOUBLE_EQ(state.start_time, 150.0);
}
