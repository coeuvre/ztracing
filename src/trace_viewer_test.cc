#include "src/trace_viewer.h"
#include <gtest/gtest.h>
#include <algorithm>
#include "src/allocator.h"

class TraceViewerTest : public ::testing::Test {
protected:
    Allocator allocator;
    TraceViewer tv;
    TraceData td;

    void SetUp() override {
        allocator = allocator_get_default();
        tv = {};
        td = {};
    }

    void TearDown() override {
        trace_viewer_deinit(&tv, allocator);
        trace_data_deinit(&td, allocator);
    }
};

TEST_F(TraceViewerTest, ZoomInAroundMouse) {
    tv.viewport.min_ts = 0;
    tv.viewport.max_ts = 1000000;
    tv.viewport.start_time = 0;
    tv.viewport.end_time = 1000000;

    TraceViewerInput input = {};
    input.canvas_x = 0;
    input.canvas_width = 1000.0f;
    input.mouse_x = 500.0f; // Center
    input.mouse_wheel = 1.0f; // Zoom in
    input.is_ctrl_down = true;
    input.tracks_hovered = true;

    trace_viewer_step(&tv, &td, input, allocator);

    double new_duration = tv.viewport.end_time - tv.viewport.start_time;
    EXPECT_LT(new_duration, 1000000.0);
    // Should be centered around 500,000
    EXPECT_NEAR((tv.viewport.start_time + tv.viewport.end_time) * 0.5, 500000.0, 1.0);
}

TEST_F(TraceViewerTest, Panning) {
    tv.viewport.min_ts = 0;
    tv.viewport.max_ts = 1000000;
    tv.viewport.start_time = 100000;
    tv.viewport.end_time = 200000;

    TraceViewerInput input = {};
    input.canvas_x = 0;
    input.canvas_width = 1000.0f;
    input.mouse_delta_x = 10.0f; // Pan right by 10 pixels
    input.is_mouse_down = true;
    input.tracks_hovered = true;

    // 10 pixels in 1000 pixels width is 1% of viewport duration (100,000) = 1,000 units
    trace_viewer_step(&tv, &td, input, allocator);

    EXPECT_NEAR(tv.viewport.start_time, 99000.0, 1.0);
    EXPECT_NEAR(tv.viewport.end_time, 199000.0, 1.0);
}

TEST_F(TraceViewerTest, HitTestingThreadEvent) {
    // Add a dummy event
    TraceEventPersisted e = {};
    e.ts = 500000;
    e.dur = 100000;
    e.name_ref = 0; 
    e.ph_ref = 0; // thread event
    array_list_push_back(&td.events, allocator, e);

    // Add a track
    Track t = {};
    t.type = TRACK_TYPE_THREAD;
    array_list_push_back(&t.event_indices, allocator, (size_t)0);
    track_update_max_dur(&t, &td, allocator);
    track_calculate_depths(&t, &td, allocator);
    array_list_push_back(&tv.tracks, allocator, t);

    tv.viewport.start_time = 0;
    tv.viewport.end_time = 1000000;
    tv.last_inner_width = 1000.0f;
    tv.last_tracks_x = 0;

    TraceViewerInput input = {};
    input.canvas_x = 0;
    input.canvas_y = 0;
    input.canvas_width = 1000.0f;
    input.canvas_height = 1000.0f;
    input.ruler_height = 20.0f;
    input.lane_height = 20.0f;
    input.tracks_hovered = true;

    // Event is at [500,000, 600,000], which is [500px, 600px] on 1000px canvas
    // It is in the first lane (depth 0), which is at Y = ruler_height + lane_height = 40px
    input.mouse_x = 550.0f;
    input.mouse_y = 50.0f; // Middle of the lane [40, 60]

    trace_viewer_step(&tv, &td, input, allocator);

    ASSERT_EQ(tv.hover_matches.size, 1u);
    EXPECT_EQ(tv.hover_matches[0].track_idx, 0u);
    EXPECT_EQ(tv.hover_matches[0].rb.event_idx, 0u);
}

TEST_F(TraceViewerTest, ClickToFocusAndCenter) {
    // Add two tracks to test vertical scrolling
    for (int i = 0; i < 2; i++) {
        TraceEventPersisted e = {};
        e.ts = 5000;
        e.dur = 1000;
        e.tid = i;
        size_t event_idx = td.events.size;
        array_list_push_back(&td.events, allocator, e);

        Track t = {};
        t.type = TRACK_TYPE_THREAD;
        t.tid = i;
        array_list_push_back(&t.event_indices, allocator, event_idx);
        track_update_max_dur(&t, &td, allocator);
        track_calculate_depths(&t, &td, allocator);
        array_list_push_back(&tv.tracks, allocator, t);
    }

    tv.viewport.min_ts = 0;
    tv.viewport.max_ts = 10000;
    tv.viewport.start_time = 0;
    tv.viewport.end_time = 10000;

    TraceViewerInput input = {};
    input.canvas_x = 0;
    input.canvas_y = 0;
    input.canvas_width = 1000.0f;
    input.canvas_height = 200.0f; // Viewport height
    input.ruler_height = 20.0f;
    input.lane_height = 20.0f;

    // Focus on event in the second track (index 1)
    tv.target_focused_event_idx = 1;

    // First step: Handles focus request, zooms, and calculates scroll to center
    trace_viewer_step(&tv, &td, input, allocator);

    EXPECT_EQ(tv.focused_event_idx, 1);
    EXPECT_TRUE(tv.selection_active);
    EXPECT_EQ(tv.selection_start_time, 5000.0);
    EXPECT_EQ(tv.selection_end_time, 6000.0);
    
    // Viewport should be zoomed to [5000, 6000] with some padding
    EXPECT_LT(tv.viewport.start_time, 5000.0);
    EXPECT_GT(tv.viewport.end_time, 6000.0);
    
    // Track 0 height = (0 + 2) * 20 = 40
    // Track 1 height = (0 + 2) * 20 = 40
    // Viewport content height = 200 - 20 (ruler) = 180
    // Track 1 top = 40
    // Center scroll = track_top - (viewport_height - track_height) * 0.5
    //               = 40 - (180 - 40) * 0.5
    //               = 40 - 70 = -30
    EXPECT_NEAR(tv.target_scroll_y, -30.0f, 0.1f);
}

TEST_F(TraceViewerTest, SelectionOnClick) {
    // Add a dummy event
    TraceEventPersisted e = {};
    e.ts = 500000;
    e.dur = 100000;
    array_list_push_back(&td.events, allocator, e);

    Track t = {};
    t.type = TRACK_TYPE_THREAD;
    array_list_push_back(&t.event_indices, allocator, (size_t)0);
    track_update_max_dur(&t, &td, allocator);
    track_calculate_depths(&t, &td, allocator);
    array_list_push_back(&tv.tracks, allocator, t);

    tv.viewport.start_time = 0;
    tv.viewport.end_time = 1000000;
    tv.last_inner_width = 1000.0f;

    TraceViewerInput input = {};
    input.canvas_x = 0;
    input.canvas_y = 0;
    input.canvas_width = 1000.0f;
    input.canvas_height = 1000.0f;
    input.ruler_height = 20.0f;
    input.lane_height = 20.0f;
    input.tracks_hovered = true;
    input.mouse_x = 550.0f;
    input.mouse_y = 50.0f;
    input.is_mouse_released = true;
    input.drag_threshold = 5.0f;

    trace_viewer_step(&tv, &td, input, allocator);

    EXPECT_EQ(tv.focused_event_idx, 0);
    EXPECT_TRUE(tv.show_details_panel);
}

TEST_F(TraceViewerTest, TimelineClickToClearRuler) {
    tv.viewport.min_ts = 0;
    tv.viewport.max_ts = 1000;
    tv.viewport.start_time = 0;
    tv.viewport.end_time = 1000;
    tv.selection_active = true;
    tv.selection_start_time = 100.0;
    tv.selection_end_time = 200.0;
    tv.selection_drag_mode = InteractionDragMode::NONE;
    tv.last_inner_width = 1000.0f;

    // Frame 1: Mouse down on ruler (not near boundaries)
    TraceViewerInput i1 = {};
    i1.canvas_width = 1000.0f;
    i1.mouse_x = 150.0f;
    i1.click_x = 150.0f;
    i1.ruler_active = true;
    i1.ruler_activated = true;
    i1.drag_delta_x = 0.0f;
    i1.drag_threshold = 5.0f;

    trace_viewer_step(&tv, &td, i1, allocator);
    EXPECT_TRUE(tv.selection_active);
    EXPECT_EQ(tv.selection_drag_mode, InteractionDragMode::RULER_NEW);

    // Frame 2: Mouse release without exceeding drag threshold
    TraceViewerInput i2 = i1;
    i2.ruler_active = false;
    i2.ruler_activated = false;
    i2.ruler_deactivated = true;
    i2.drag_delta_x = 1.0f; // < threshold

    trace_viewer_step(&tv, &td, i2, allocator);
    EXPECT_FALSE(tv.selection_active);
    EXPECT_EQ(tv.selection_drag_mode, InteractionDragMode::NONE);
}

TEST_F(TraceViewerTest, TimelineKeepExistingSelectionOnPress) {
    tv.viewport.min_ts = 0;
    tv.viewport.max_ts = 1000;
    tv.viewport.start_time = 0;
    tv.viewport.end_time = 1000;
    tv.selection_active = true;
    tv.selection_start_time = 100.0;
    tv.selection_end_time = 200.0;
    tv.selection_drag_mode = InteractionDragMode::NONE;
    tv.last_inner_width = 1000.0f;

    // Frame 1: Mouse down on ruler
    TraceViewerInput i1 = {};
    i1.canvas_width = 1000.0f;
    i1.mouse_x = 150.0f;
    i1.click_x = 150.0f;
    i1.ruler_active = true;
    i1.ruler_activated = true;
    i1.drag_delta_x = 0.0f;
    i1.drag_threshold = 5.0f;

    trace_viewer_step(&tv, &td, i1, allocator);
    EXPECT_TRUE(tv.selection_active);
    EXPECT_EQ(tv.selection_drag_mode, InteractionDragMode::RULER_NEW);
    EXPECT_DOUBLE_EQ(tv.selection_start_time, 100.0);
    EXPECT_DOUBLE_EQ(tv.selection_end_time, 200.0);

    // Frame 2: Drag slightly
    TraceViewerInput i2 = i1;
    i2.ruler_activated = false;
    i2.mouse_x = 152.0f;
    i2.drag_delta_x = 2.0f;

    trace_viewer_step(&tv, &td, i2, allocator);
    EXPECT_DOUBLE_EQ(tv.selection_start_time, 100.0);
    EXPECT_DOUBLE_EQ(tv.selection_end_time, 200.0);

    // Frame 3: Drag past threshold
    TraceViewerInput i3 = i1;
    i3.ruler_activated = false;
    i3.mouse_x = 160.0f;
    i3.drag_delta_x = 10.0f;

    trace_viewer_step(&tv, &td, i3, allocator);
    EXPECT_DOUBLE_EQ(tv.selection_start_time, 150.0);
    EXPECT_DOUBLE_EQ(tv.selection_end_time, 160.0);
}

TEST_F(TraceViewerTest, ViewportZoomClampingWithSelection) {
    tv.viewport.min_ts = 0;
    tv.viewport.max_ts = 1000000;
    tv.viewport.start_time = 0;
    tv.viewport.end_time = 1000;
    tv.selection_active = true;
    tv.selection_start_time = 400.0;
    tv.selection_end_time = 600.0;
    tv.last_inner_width = 1000.0f;

    TraceViewerInput input = {};
    input.canvas_width = 1000.0f;
    input.tracks_hovered = true;
    input.mouse_wheel = -1.0f; // Zoom out
    input.is_ctrl_down = true;
    input.mouse_x = 500.0f;

    trace_viewer_step(&tv, &td, input, allocator);

    EXPECT_DOUBLE_EQ(tv.viewport.end_time - tv.viewport.start_time, 1200.0);
    EXPECT_LE(tv.viewport.start_time, 400.0);
    EXPECT_GE(tv.viewport.end_time, 600.0);
}

TEST_F(TraceViewerTest, TimelineSnapping) {
    tv.viewport.min_ts = 0;
    tv.viewport.max_ts = 1000;
    tv.viewport.start_time = 0;
    tv.viewport.end_time = 1000;
    tv.last_inner_width = 1000.0f;

    // Add a dummy event for snapping
    TraceEventPersisted e = {};
    e.ts = 102;
    e.dur = 10; // Ensure it's not a zero-width event
    array_list_push_back(&td.events, allocator, e);

    Track t = {};
    t.type = TRACK_TYPE_THREAD;
    array_list_push_back(&t.event_indices, allocator, (size_t)0);
    track_update_max_dur(&t, &td, allocator);
    track_calculate_depths(&t, &td, allocator);
    array_list_push_back(&tv.tracks, allocator, t);

    TraceViewerInput input = {};
    input.canvas_x = 0.0f;
    input.canvas_y = 0.0f;
    input.canvas_width = 1000.0f;
    input.canvas_height = 1000.0f;
    tv.last_tracks_x = 0.0f;
    tv.last_inner_width = 1000.0f;
    input.ruler_height = 20.0f;
    input.lane_height = 20.0f;
    input.mouse_x = 100.0f;
    input.click_x = 100.0f;
    input.ruler_active = true;
    input.ruler_activated = true;
    input.tracks_hovered = true;
    input.drag_threshold = 5.0f;

    // Frame 1: Press (no drag yet)
    input.drag_delta_x = 0.0f;
    trace_viewer_step(&tv, &td, input, allocator);
    EXPECT_FALSE(tv.selection_active);
    EXPECT_FALSE(tv.snap_has_snap);

    // Frame 2: Real Drag
    input.ruler_activated = false;
    input.drag_delta_x = 10.0f;
    input.mouse_x = 104.0f; // Near ts=104, within 5px threshold of ts=102
    trace_viewer_step(&tv, &td, input, allocator);
    
    EXPECT_TRUE(tv.selection_active);
    EXPECT_DOUBLE_EQ(tv.selection_start_time, 100.0);
    EXPECT_DOUBLE_EQ(tv.selection_end_time, 102.0); // Snapped to event start
    EXPECT_TRUE(tv.snap_has_snap);
    EXPECT_FLOAT_EQ(tv.snap_px, 102.0f);
}

TEST_F(TraceViewerTest, InteractionOutsideSelectionIgnored) {
    tv.viewport.min_ts = 0;
    tv.viewport.max_ts = 1000;
    tv.viewport.start_time = 0;
    tv.viewport.end_time = 1000;
    tv.selection_active = true;
    tv.selection_start_time = 400.0;
    tv.selection_end_time = 600.0;
    tv.last_inner_width = 1000.0f;
    array_list_push_back(&tv.selected_event_indices, allocator, (int64_t)123);
    std::sort(tv.selected_event_indices.data, tv.selected_event_indices.data + tv.selected_event_indices.size);

    // Add an event outside selection (at ts=100)
    TraceEventPersisted e = {};
    e.ts = 100;
    e.dur = 50;
    array_list_push_back(&td.events, allocator, e);

    Track t = {};
    t.type = TRACK_TYPE_THREAD;
    array_list_push_back(&t.event_indices, allocator, (size_t)0);
    track_update_max_dur(&t, &td, allocator);
    track_calculate_depths(&t, &td, allocator);
    array_list_push_back(&tv.tracks, allocator, t);

    TraceViewerInput input = {};
    input.canvas_width = 1000.0f;
    input.canvas_height = 1000.0f;
    input.ruler_height = 20.0f;
    input.lane_height = 20.0f;
    input.tracks_hovered = true;
    input.mouse_x = 125.0f; // Over the event at ts=100
    input.mouse_y = 50.0f;
    input.is_mouse_released = true;
    input.drag_threshold = 5.0f;

    trace_viewer_step(&tv, &td, input, allocator);

    // Hover matches should be empty because it's outside selection
    EXPECT_EQ(tv.hover_matches.size, 0u);
    // Range is cleared, but events are kept because click was consumed by range clearing
    EXPECT_FALSE(tv.selection_active);
    EXPECT_EQ(tv.selected_event_indices.size, 1u);
    EXPECT_EQ(tv.selected_event_indices[0], 123);
}

TEST_F(TraceViewerTest, CounterInteractionOutsideSelectionIgnored) {
    tv.viewport.min_ts = 0;
    tv.viewport.max_ts = 1000;
    tv.viewport.start_time = 0;
    tv.viewport.end_time = 1000;
    tv.selection_active = true;
    tv.selection_start_time = 400.0;
    tv.selection_end_time = 600.0;
    tv.last_inner_width = 1000.0f;
    array_list_push_back(&tv.selected_event_indices, allocator, (int64_t)123);
    std::sort(tv.selected_event_indices.data, tv.selected_event_indices.data + tv.selected_event_indices.size);

    // Add counter events
    TraceArgPersisted arg = {};
    arg.key_ref = 1;
    arg.val_double = 1.0;
    array_list_push_back(&td.args, allocator, arg);

    TraceEventPersisted e1 = {};
    e1.ts = 100;
    e1.args_offset = 0;
    e1.args_count = 1;
    array_list_push_back(&td.events, allocator, e1);

    TraceEventPersisted e2 = {};
    e2.ts = 200;
    e2.args_offset = 0;
    e2.args_count = 1;
    array_list_push_back(&td.events, allocator, e2);

    Track t = {};
    t.type = TRACK_TYPE_COUNTER;
    array_list_push_back(&t.event_indices, allocator, (size_t)0);
    array_list_push_back(&t.event_indices, allocator, (size_t)1);
    array_list_push_back(&t.counter_series, allocator, (StringRef)1);
    t.counter_max_total = 1.0;
    track_update_max_dur(&t, &td, allocator);
    array_list_push_back(&tv.tracks, allocator, t);

    TraceViewerInput input = {};
    input.canvas_width = 1000.0f;
    input.canvas_height = 1000.0f;
    input.ruler_height = 20.0f;
    input.lane_height = 20.0f;
    input.tracks_hovered = true;
    input.mouse_x = 150.0f; // Over the counter event (ts=100 to ts=200 is one bucket, ts=150 is in middle)
    input.mouse_y = 60.0f;  // Inside counter track content area
    input.is_mouse_released = true;
    input.drag_threshold = 5.0f;

    trace_viewer_step(&tv, &td, input, allocator);

    // Should be 0 because gating is currently ENABLED
    EXPECT_EQ(tv.hover_matches.size, 0u);
    // Range is cleared, but events are kept because click was consumed by range clearing
    EXPECT_FALSE(tv.selection_active);
    EXPECT_EQ(tv.selected_event_indices.size, 1u);
    EXPECT_EQ(tv.selected_event_indices[0], 123);
}

TEST_F(TraceViewerTest, TimelineClickOutsideToClearTracks) {
    tv.viewport.min_ts = 0;
    tv.viewport.max_ts = 1000;
    tv.viewport.start_time = 0;
    tv.viewport.end_time = 1000;
    tv.selection_active = true;
    tv.selection_start_time = 400.0;
    tv.selection_end_time = 600.0;
    tv.last_inner_width = 1000.0f;
    tv.last_inner_height = 1000.0f;
    tv.last_tracks_x = 0.0f;
    tv.last_tracks_y = 0.0f;
    
    // Add a dummy box selection
    array_list_push_back(&tv.selected_event_indices, allocator, (int64_t)123);

    TraceViewerInput input = {};
    input.canvas_width = 1000.0f;
    input.canvas_height = 1000.0f;
    input.ruler_height = 0.0f;
    input.tracks_hovered = true;
    input.mouse_x = 100.0f; // Outside selection [400, 600]
    input.mouse_y = 500.0f; 
    input.is_mouse_released = true;
    input.drag_threshold = 5.0f;

    trace_viewer_step(&tv, &td, input, allocator);
    EXPECT_FALSE(tv.selection_active);
    // Events are kept because click was consumed by range clearing
    EXPECT_EQ(tv.selected_event_indices.size, 1u);
}

TEST_F(TraceViewerTest, TimelineClickInsideKeepsSelection) {
    tv.viewport.min_ts = 0;
    tv.viewport.max_ts = 1000;
    tv.viewport.start_time = 0;
    tv.viewport.end_time = 1000;
    tv.selection_active = true;
    tv.selection_start_time = 400.0;
    tv.selection_end_time = 600.0;
    tv.last_inner_width = 1000.0f;

    TraceViewerInput input = {};
    input.canvas_width = 1000.0f;
    input.tracks_hovered = true;
    input.mouse_x = 500.0f; // Inside selection [400, 600]
    input.is_mouse_released = true;
    input.drag_threshold = 5.0f;

    trace_viewer_step(&tv, &td, input, allocator);
    EXPECT_TRUE(tv.selection_active);
}

TEST_F(TraceViewerTest, TimelineClickBoundaryKeepsSelection) {
    tv.viewport.min_ts = 0;
    tv.viewport.max_ts = 1000;
    tv.viewport.start_time = 0;
    tv.viewport.end_time = 1000;
    tv.selection_active = true;
    tv.selection_start_time = 400.0;
    tv.selection_end_time = 600.0;
    tv.last_inner_width = 1000.0f;

    TraceViewerInput input = {};
    input.canvas_width = 1000.0f;
    input.tracks_hovered = true;
    input.mouse_x = 400.0f; // Exactly on start boundary
    input.is_mouse_released = true;
    input.drag_threshold = 5.0f;

    trace_viewer_step(&tv, &td, input, allocator);
    EXPECT_TRUE(tv.selection_active);
}

TEST_F(TraceViewerTest, CounterInteractionInsideSelectionWorks) {
    tv.viewport.min_ts = 0;
    tv.viewport.max_ts = 1000;
    tv.viewport.start_time = 0;
    tv.viewport.end_time = 1000;
    tv.selection_active = true;
    tv.selection_start_time = 100.0;
    tv.selection_end_time = 200.0;
    tv.last_inner_width = 1000.0f;

    // Add counter events
    TraceArgPersisted arg = {};
    arg.key_ref = 1;
    arg.val_double = 1.0;
    array_list_push_back(&td.args, allocator, arg);

    TraceEventPersisted e1 = {};
    e1.ts = 150;
    e1.args_offset = 0;
    e1.args_count = 1;
    array_list_push_back(&td.events, allocator, e1);

    TraceEventPersisted e2 = {};
    e2.ts = 300;
    e2.args_offset = 0;
    e2.args_count = 1;
    array_list_push_back(&td.events, allocator, e2);

    Track t = {};
    t.type = TRACK_TYPE_COUNTER;
    array_list_push_back(&t.event_indices, allocator, (size_t)0);
    array_list_push_back(&t.event_indices, allocator, (size_t)1);
    array_list_push_back(&t.counter_series, allocator, (StringRef)1);
    t.counter_max_total = 1.0;
    track_update_max_dur(&t, &td, allocator);
    array_list_push_back(&tv.tracks, allocator, t);

    TraceViewerInput input = {};
    input.canvas_width = 1000.0f;
    input.canvas_height = 1000.0f;
    input.ruler_height = 20.0f;
    input.lane_height = 20.0f;
    input.tracks_hovered = true;
    input.mouse_x = 155.0f;
    input.mouse_y = 60.0f;
    input.is_mouse_released = true;
    input.drag_threshold = 5.0f;

    trace_viewer_step(&tv, &td, input, allocator);

    EXPECT_EQ(tv.hover_matches.size, 1u);
    EXPECT_EQ(tv.focused_event_idx, 0);
}

TEST_F(TraceViewerTest, SnappingOnlyInVisibleTracks) {
    tv.viewport.min_ts = 0;
    tv.viewport.max_ts = 1000;
    tv.viewport.start_time = 0;
    tv.viewport.end_time = 1000;
    tv.last_inner_width = 1000.0f;

    // Add a dummy event at ts=102
    TraceEventPersisted e = {};
    e.ts = 102;
    e.dur = 10;
    array_list_push_back(&td.events, allocator, e);

    Track t = {};
    t.type = TRACK_TYPE_THREAD;
    array_list_push_back(&t.event_indices, allocator, (size_t)0);
    track_update_max_dur(&t, &td, allocator);
    track_calculate_depths(&t, &td, allocator);
    array_list_push_back(&tv.tracks, allocator, t);

    TraceViewerInput input = {};
    input.canvas_x = 0.0f;
    input.canvas_y = 0.0f;
    input.canvas_width = 1000.0f;
    input.canvas_height = 1000.0f;
    input.ruler_height = 20.0f;
    input.lane_height = 20.0f;
    input.mouse_x = 100.0f; // Near ts=102
    input.ruler_active = true;
    input.ruler_activated = true;
    input.drag_delta_x = 10.0f; // Exceed threshold
    input.drag_threshold = 5.0f;
    input.tracks_hovered = true;

    tv.last_tracks_x = 0.0f;
    tv.last_inner_width = 1000.0f;
    // Scenario 1: Track is invisible (scrolled past)
    // track_y = canvas_y + ruler_height + cumulative_y - tracks_scroll_y
    // track_y = 0 + 20 + 0 - 60 = -40
    // track_height = (0 + 2) * 20 = 40
    // track_y + track_height = 0 ( < canvas_y + ruler_height = 20)
    input.tracks_scroll_y = 60.0f;
    trace_viewer_step(&tv, &td, input, allocator);
    EXPECT_FALSE(tv.snap_has_snap);

    // Scenario 2: Track is visible
    input.tracks_scroll_y = 10.0f;
    input.drag_delta_x = 10.0f;
    trace_viewer_step(&tv, &td, input, allocator);
    EXPECT_TRUE(tv.snap_has_snap);
    EXPECT_DOUBLE_EQ(tv.snap_best_ts, 102.0);
}

TEST_F(TraceViewerTest, SnappingDisabledWhenNotDragging) {
    tv.viewport.min_ts = 0;
    tv.viewport.max_ts = 1000;
    tv.viewport.start_time = 0;
    tv.viewport.end_time = 1000;
    tv.last_inner_width = 1000.0f;

    // Add a dummy event at ts=102
    TraceEventPersisted e = {};
    e.ts = 102;
    e.dur = 10;
    array_list_push_back(&td.events, allocator, e);

    Track t = {};
    t.type = TRACK_TYPE_THREAD;
    array_list_push_back(&t.event_indices, allocator, (size_t)0);
    track_update_max_dur(&t, &td, allocator);
    track_calculate_depths(&t, &td, allocator);
    array_list_push_back(&tv.tracks, allocator, t);

    TraceViewerInput input = {};
    input.canvas_x = 0;
    input.canvas_y = 0;
    input.canvas_width = 1000.0f;
    input.canvas_height = 1000.0f;
    input.ruler_height = 20.0f;
    input.lane_height = 20.0f;
    input.mouse_x = 100.0f; // Near ts=102
    input.tracks_hovered = true;

    // 1. Just pressed: snapping should be disabled
    input.ruler_active = true;
    input.ruler_activated = true;
    input.drag_delta_x = 0.0f;
    input.drag_threshold = 5.0f;
    trace_viewer_step(&tv, &td, input, allocator);
    EXPECT_FALSE(tv.snap_has_snap);

    // 2. Dragging boundary in tracks past threshold: snapping should be enabled
    tv.selection_active = true;
    tv.selection_start_time = 100.0;
    tv.selection_end_time = 200.0;
    input.ruler_active = false;
    input.is_mouse_clicked = true;
    input.is_mouse_down = true;
    input.drag_delta_x = 10.0f;
    trace_viewer_step(&tv, &td, input, allocator);
    EXPECT_EQ(tv.selection_drag_mode, InteractionDragMode::TRACKS_START);
    EXPECT_TRUE(tv.snap_has_snap);
    EXPECT_DOUBLE_EQ(tv.snap_best_ts, 102.0);
}

TEST_F(TraceViewerTest, PanningClampingWithSelection) {
    tv.viewport.min_ts = 0;
    tv.viewport.max_ts = 10000;
    tv.viewport.start_time = 0;
    tv.viewport.end_time = 1000;
    tv.selection_active = true;
    tv.selection_start_time = 400.0;
    tv.selection_end_time = 600.0;
    tv.last_inner_width = 1000.0f;

    TraceViewerInput input = {};
    input.canvas_width = 1000.0f;
    input.tracks_hovered = true;
    input.is_mouse_down = true;
    input.is_mouse_clicked = false;
    input.drag_threshold = 5.0f;

    // Pan right (moving viewport left), should clamp start_time at 400
    input.mouse_delta_x = -1000.0f; 
    trace_viewer_step(&tv, &td, input, allocator);
    EXPECT_DOUBLE_EQ(tv.viewport.start_time, 400.0);
    EXPECT_DOUBLE_EQ(tv.viewport.end_time, 1400.0);

    // Pan left (moving viewport right), should clamp end_time at 600
    // resetting start_time for next sub-test
    tv.viewport.start_time = 100.0;
    tv.viewport.end_time = 1100.0;
    input.mouse_delta_x = 1000.0f;
    trace_viewer_step(&tv, &td, input, allocator);
    // duration is 1000. t2 is 600.
    // viewport.start_time + 1000 < 600 is false.
    // wait, if I pan left (positive dx), viewport.start_time decreases.
    // viewport.start_time = 100 - (1000/1000)*1000 = -900.
    // clamped: if (start_time + 1000 < 600) start_time = 600 - 1000 = -400.
    EXPECT_DOUBLE_EQ(tv.viewport.end_time, 600.0);
    EXPECT_DOUBLE_EQ(tv.viewport.start_time, -400.0);
}

TEST_F(TraceViewerTest, TimelineProximityCheck) {
    tv.selection_active = true;
    tv.selection_start_time = 100.0;
    tv.selection_end_time = 200.0;

    TraceViewerInput input = {};
    input.canvas_width = 1000.0f;
    tv.last_inner_width = 1000.0f;
    tv.viewport.start_time = 0;
    tv.viewport.end_time = 1000;

    // Mouse near start
    input.mouse_x = 102.0f;
    trace_viewer_step(&tv, &td, input, allocator);
    // Proximity is internal to step, but we can verify it indirectly via drag_mode if we trigger a click
    input.is_mouse_clicked = true;
    input.is_mouse_down = true;
    input.tracks_hovered = true;
    trace_viewer_step(&tv, &td, input, allocator);
    EXPECT_EQ(tv.selection_drag_mode, InteractionDragMode::TRACKS_START);

    // Mouse near end
    tv.selection_drag_mode = InteractionDragMode::NONE;
    input.mouse_x = 198.0f;
    trace_viewer_step(&tv, &td, input, allocator);
    EXPECT_EQ(tv.selection_drag_mode, InteractionDragMode::TRACKS_END);
}

TEST_F(TraceViewerTest, TimelineNoZeroWidthSelectionOnPress) {
    tv.selection_active = false;
    tv.viewport.start_time = 0;
    tv.viewport.end_time = 1000;
    tv.last_inner_width = 1000.0f;

    TraceViewerInput input = {};
    input.canvas_width = 1000.0f;
    input.mouse_x = 150.0f;
    input.click_x = 150.0f;
    input.ruler_active = true;
    input.ruler_activated = true;
    input.drag_delta_x = 0.0f;
    input.drag_threshold = 5.0f;

    trace_viewer_step(&tv, &td, input, allocator);
    EXPECT_FALSE(tv.selection_active);
    
    // Drag past threshold
    input.ruler_activated = false;
    input.mouse_x = 160.0f;
    input.drag_delta_x = 10.0f;
    trace_viewer_step(&tv, &td, input, allocator);
    EXPECT_TRUE(tv.selection_active);
    EXPECT_DOUBLE_EQ(tv.selection_start_time, 150.0);
    EXPECT_DOUBLE_EQ(tv.selection_end_time, 160.0);
}

TEST_F(TraceViewerTest, TimelineSnappedBoundaryDragTracks) {
    tv.selection_active = true;
    tv.selection_start_time = 100.0;
    tv.selection_end_time = 200.0;
    tv.viewport.start_time = 0;
    tv.viewport.end_time = 1000;
    tv.last_inner_width = 1000.0f;

    // Add a dummy event for snapping at ts=50
    TraceEventPersisted e = {};
    e.ts = 50;
    e.dur = 10;
    array_list_push_back(&td.events, allocator, e);
    Track t = {};
    t.type = TRACK_TYPE_THREAD;
    array_list_push_back(&t.event_indices, allocator, (size_t)0);
    track_update_max_dur(&t, &td, allocator);
    track_calculate_depths(&t, &td, allocator);
    array_list_push_back(&tv.tracks, allocator, t);

    TraceViewerInput input = {};
    input.canvas_width = 1000.0f;
    input.canvas_height = 1000.0f;
    input.ruler_height = 20.0f;
    input.lane_height = 20.0f;
    input.tracks_hovered = true;

    // Click near start (100.0)
    input.mouse_x = 101.0f;
    input.click_x = 101.0f;
    input.is_mouse_clicked = true;
    input.is_mouse_down = true;
    trace_viewer_step(&tv, &td, input, allocator);
    EXPECT_EQ(tv.selection_drag_mode, InteractionDragMode::TRACKS_START);

    // Drag towards 50, should snap to 50
    input.is_mouse_clicked = false;
    input.mouse_x = 52.0f;
    trace_viewer_step(&tv, &td, input, allocator);
    EXPECT_DOUBLE_EQ(tv.selection_start_time, 50.0);
    EXPECT_DOUBLE_EQ(tv.selection_end_time, 200.0);
}

TEST_F(TraceViewerTest, TimelineAreaIsolation) {
    tv.selection_active = false;
    tv.viewport.start_time = 0;
    tv.viewport.end_time = 1000;
    tv.last_inner_width = 1000.0f;

    // Click in tracks when no selection exists
    TraceViewerInput input = {};
    input.canvas_width = 1000.0f;
    input.tracks_hovered = true;
    input.mouse_x = 100.0f;
    input.is_mouse_clicked = true;
    input.is_mouse_down = true;
    trace_viewer_step(&tv, &td, input, allocator);

    EXPECT_FALSE(tv.selection_active);
    EXPECT_EQ(tv.selection_drag_mode, InteractionDragMode::NONE);
}

TEST_F(TraceViewerTest, BoxSelectEvents) {
    // Track 1: Thread with 2 events
    {
        TraceEventPersisted e1 = {}; e1.ts = 100; e1.dur = 50;
        TraceEventPersisted e2 = {}; e2.ts = 200; e2.dur = 50;
        array_list_push_back(&td.events, allocator, e1);
        array_list_push_back(&td.events, allocator, e2);
        Track t = {};
        t.type = TRACK_TYPE_THREAD;
        array_list_push_back(&t.event_indices, allocator, (size_t)0);
        array_list_push_back(&t.event_indices, allocator, (size_t)1);
        array_list_resize(&t.depths, allocator, 2);
        t.depths[0] = 0; t.depths[1] = 0;
        track_update_max_dur(&t, &td, allocator);
        array_list_push_back(&tv.tracks, allocator, t);
    }
    // Track 2: Thread with 1 event in depth 1
    {
        TraceEventPersisted e3 = {}; e3.ts = 150; e3.dur = 100;
        array_list_push_back(&td.events, allocator, e3);
        Track t = {};
        t.type = TRACK_TYPE_THREAD;
        array_list_push_back(&t.event_indices, allocator, (size_t)2);
        array_list_resize(&t.depths, allocator, 1);
        t.depths[0] = 1;
        track_update_max_dur(&t, &td, allocator);
        t.max_depth = 1;
        array_list_push_back(&tv.tracks, allocator, t);
    }

    tv.viewport.min_ts = 0;
    tv.viewport.max_ts = 1000;
    tv.viewport.start_time = 0;
    tv.viewport.end_time = 1000;
    tv.last_inner_width = 1000.0f;
    tv.last_tracks_x = 0;
    tv.last_tracks_y = 20.0f;
    tv.last_lane_height = 20.0f;

    // First frame: Start box select
    // Box covers TS [120, 180] and Y [30, 70]
    // Track 1: vi.y = 20, vi.height = 40 (lane 0: [40, 60], header: [20, 40])
    // Track 2: vi.y = 60, vi.height = 60 (lane 0: [80, 100], lane 1: [100, 120], header: [60, 80])
    // Wait, track height = (max_depth + 2) * lane_height
    // Track 1: max_depth=0, height=40. y=20. Header=[20,40], Lane0=[40,60].
    // Track 2: max_depth=1, height=60. y=60. Header=[60,80], Lane0=[80,100], Lane1=[100,120].

    TraceViewerInput input = {};
    input.canvas_width = 1000.0f;
    input.canvas_height = 1000.0f;
    input.ruler_height = 20.0f;
    input.lane_height = 20.0f;
    input.is_shift_down = true;
    input.is_mouse_clicked = true;
    input.is_mouse_down = true;
    input.mouse_x = 120.0f;
    input.mouse_y = 30.0f;
    input.tracks_hovered = true;

    trace_viewer_step(&tv, &td, input, allocator);
    EXPECT_EQ(tv.selection_drag_mode, InteractionDragMode::BOX_SELECT);
    EXPECT_FALSE(tv.snap_has_snap); // Snapping should be disabled
    EXPECT_FLOAT_EQ(tv.box_select_start.x, 120.0f);

    // Second frame: End box select at [180, 110]
    // Box area: X=[120, 180], Y=[30, 110]
    // TS area: [120, 180]
    // Y area: [30, 110]
    // Overlapping events:
    // e1: ts=[100, 150], lane0=[40, 60]. Overlaps X and Y.
    // e2: ts=[200, 250], lane0=[40, 60]. X doesn't overlap.
    // e3: ts=[150, 250], lane1=[100, 120]. Overlaps X and Y.
    
    input.is_mouse_clicked = false;
    input.is_mouse_down = false;
    input.is_mouse_released = true;
    input.mouse_x = 180.0f;
    input.mouse_y = 110.0f;
    
    trace_viewer_step(&tv, &td, input, allocator);
    EXPECT_EQ(tv.selection_drag_mode, InteractionDragMode::NONE);
    EXPECT_EQ(tv.selected_event_indices.size, 2u);
    EXPECT_EQ(tv.selected_event_indices[0], 0); // e1
    EXPECT_EQ(tv.selected_event_indices[1], 2); // e3
}

TEST_F(TraceViewerTest, BoxSelectLongEvent) {
    // Event that starts at 0 and ends at 1000
    TraceEventPersisted e = {}; e.ts = 0; e.dur = 1000;
    array_list_push_back(&td.events, allocator, e);
    Track t = {};
    t.type = TRACK_TYPE_THREAD;
    array_list_push_back(&t.event_indices, allocator, (size_t)0);
    array_list_resize(&t.depths, allocator, 1);
    t.depths[0] = 0;
    track_update_max_dur(&t, &td, allocator);
    array_list_push_back(&tv.tracks, allocator, t);

    tv.viewport.min_ts = 0;
    tv.viewport.max_ts = 1000;
    tv.viewport.start_time = 0;
    tv.viewport.end_time = 1000;
    tv.last_inner_width = 1000.0f;
    tv.last_tracks_x = 0;
    tv.last_tracks_y = 20.0f;
    tv.last_lane_height = 20.0f;

    // Box from TS [500, 600], Y [40, 60] (Lane 0)
    TraceViewerInput input = {};
    input.canvas_width = 1000.0f;
    input.canvas_height = 1000.0f;
    input.ruler_height = 20.0f;
    input.lane_height = 20.0f;
    input.is_shift_down = true;
    input.is_mouse_clicked = true;
    input.is_mouse_down = true;
    input.mouse_x = 500.0f;
    input.mouse_y = 45.0f;
    input.tracks_hovered = true;

    trace_viewer_step(&tv, &td, input, allocator);
    
    input.is_mouse_clicked = false;
    input.is_mouse_down = false;
    input.is_mouse_released = true;
    input.mouse_x = 600.0f;
    input.mouse_y = 55.0f;
    
    trace_viewer_step(&tv, &td, input, allocator);
    EXPECT_EQ(tv.selected_event_indices.size, 1u);
    EXPECT_EQ(tv.selected_event_indices[0], 0);
}

TEST_F(TraceViewerTest, BoxSelectCounterHeaderIgnored) {
    // Add counter events
    TraceArgPersisted arg = {};
    arg.key_ref = 1;
    arg.val_double = 1.0;
    array_list_push_back(&td.args, allocator, arg);

    TraceEventPersisted e1 = {}; e1.ts = 100; e1.args_offset = 0; e1.args_count = 1;
    array_list_push_back(&td.events, allocator, e1);

    Track t = {};
    t.type = TRACK_TYPE_COUNTER;
    array_list_push_back(&t.event_indices, allocator, (size_t)0);
    array_list_push_back(&t.counter_series, allocator, (StringRef)1);
    t.counter_max_total = 1.0;
    track_update_max_dur(&t, &td, allocator);
    array_list_push_back(&tv.tracks, allocator, t);

    tv.viewport.min_ts = 0;
    tv.viewport.max_ts = 1000;
    tv.viewport.start_time = 0;
    tv.viewport.end_time = 1000;
    tv.last_inner_width = 1000.0f;
    tv.last_tracks_x = 0;
    tv.last_tracks_y = 20.0f;
    tv.last_lane_height = 20.0f;

    // Track 0 starts at Y=20.
    // Header is [20, 40], Chart is [40, 100] (counter height = 3 * lane_height + header_lane = 80)
    // Wait, counter_track_height = 3.0f * input.lane_height = 60.
    // Total height = 60 + header_lane (20) = 80.
    // Header: [20, 40]. Chart: [40, 80].

    TraceViewerInput input = {};
    input.canvas_width = 1000.0f;
    input.canvas_height = 1000.0f;
    input.ruler_height = 20.0f;
    input.lane_height = 20.0f;
    input.is_shift_down = true;
    input.is_mouse_clicked = true;
    input.is_mouse_down = true;
    input.tracks_hovered = true;

    // Case 1: Box overlaps ONLY the header [25, 35]
    input.mouse_x = 50.0f;
    input.mouse_y = 25.0f;
    trace_viewer_step(&tv, &td, input, allocator);
    
    input.is_mouse_clicked = false;
    input.is_mouse_down = false;
    input.is_mouse_released = true;
    input.mouse_x = 150.0f;
    input.mouse_y = 35.0f;
    trace_viewer_step(&tv, &td, input, allocator);
    
    EXPECT_EQ(tv.selected_event_indices.size, 0u);

    // Case 2: Box overlaps the chart area [45, 55]
    input.is_mouse_clicked = true;
    input.is_mouse_down = true;
    input.is_mouse_released = false;
    input.mouse_x = 50.0f;
    input.mouse_y = 45.0f;
    trace_viewer_step(&tv, &td, input, allocator);

    input.is_mouse_clicked = false;
    input.is_mouse_down = false;
    input.is_mouse_released = true;
    input.mouse_x = 150.0f;
    input.mouse_y = 55.0f;
    trace_viewer_step(&tv, &td, input, allocator);
    
    EXPECT_EQ(tv.selected_event_indices.size, 1u);
    EXPECT_EQ(tv.selected_event_indices[0], 0);
}

TEST_F(TraceViewerTest, TrackHeaderNameFormatting) {
    // 1. Thread with name
    TraceData td_local = {};
    StringRef name_ref = trace_data_push_string(&td_local, allocator, "MainThread");
    
    Track t1 = {};
    t1.type = TRACK_TYPE_THREAD;
    t1.name_ref = name_ref;
    t1.tid = 123;
    array_list_push_back(&tv.tracks, allocator, t1);

    // 2. Thread without name (fallback to TID)
    Track t2 = {};
    t2.type = TRACK_TYPE_THREAD;
    t2.tid = 456;
    array_list_push_back(&tv.tracks, allocator, t2);

    // 3. Counter with name and ID
    StringRef c_name_ref = trace_data_push_string(&td_local, allocator, "Memory");
    StringRef c_id_ref = trace_data_push_string(&td_local, allocator, "0x1");
    Track t3 = {};
    t3.type = TRACK_TYPE_COUNTER;
    t3.name_ref = c_name_ref;
    t3.id_ref = c_id_ref;
    array_list_push_back(&tv.tracks, allocator, t3);

    TraceViewerInput input = {};
    input.canvas_width = 1000.0f;
    input.canvas_height = 1000.0f;
    input.ruler_height = 20.0f;
    input.lane_height = 20.0f;

    trace_viewer_step(&tv, &td_local, input, allocator);

    ASSERT_EQ(tv.track_infos.size, 3u);
    EXPECT_STREQ(tv.track_infos[0].name, "MainThread");
    EXPECT_STREQ(tv.track_infos[1].name, "Thread 456");
    EXPECT_STREQ(tv.track_infos[2].name, "Memory (0x1)");

    trace_data_deinit(&td_local, allocator);
}

TEST_F(TraceViewerTest, TrackLayoutAndCulling) {
    // Add 3 tracks, each 2 lanes high (1 header + 1 content, simplified for test)
    // Lane height = 20. Track height = (0 + 2) * 20 = 40.
    for (int i = 0; i < 3; i++) {
        Track t = {};
        t.type = TRACK_TYPE_THREAD;
        t.max_depth = 0; 
        array_list_push_back(&tv.tracks, allocator, t);
    }

    TraceViewerInput input = {};
    input.canvas_y = 100.0f;
    input.canvas_height = 100.0f; // Viewport: [100, 200]
    input.ruler_height = 20.0f;  // Track area starts at 120
    input.lane_height = 20.0f;
    input.tracks_scroll_y = 0.0f;

    trace_viewer_step(&tv, &td, input, allocator);

    EXPECT_FLOAT_EQ(tv.total_tracks_height, 120.0f); // 3 * 40
    ASSERT_EQ(tv.track_infos.size, 3u);

    // Track 0: y = 100 + 20 + 0 - 0 = 120. Height 40. Range [120, 160]. Visible.
    EXPECT_FLOAT_EQ(tv.track_infos[0].y, 120.0f);
    EXPECT_TRUE(tv.track_infos[0].visible);

    // Track 1: y = 120 + 40 = 160. Height 40. Range [160, 200]. Visible.
    EXPECT_FLOAT_EQ(tv.track_infos[1].y, 160.0f);
    EXPECT_TRUE(tv.track_infos[1].visible);

    // Track 2: y = 160 + 40 = 200. Height 40. Range [200, 240]. 
    // Culled because it starts exactly at canvas_y + canvas_height = 200? 
    // Logic: vi.y <= input.canvas_y + input.canvas_height -> 200 <= 200 is true.
    // So it's technically visible (at the very bottom edge).
    EXPECT_TRUE(tv.track_infos[2].visible);

    // Scroll down by 50 pixels
    input.tracks_scroll_y = 50.0f;
    trace_viewer_step(&tv, &td, input, allocator);

    // Track 0: y = 120 - 50 = 70. Height 40. End = 110. 
    // Culled because end 110 < track area start 120.
    EXPECT_FALSE(tv.track_infos[0].visible);

    // Track 1: y = 160 - 50 = 110. Height 40. End = 150.
    // Visible because end 150 >= 120 and start 110 <= 200.
    EXPECT_TRUE(tv.track_infos[1].visible);
}

TEST_F(TraceViewerTest, SelectionOverlayLayoutComputation) {
    tv.viewport.start_time = 0;
    tv.viewport.end_time = 1000;
    tv.selection_active = true;
    tv.selection_start_time = 200;
    tv.selection_end_time = 800;

    TraceViewerInput input = {};
    input.canvas_x = 100.0f;
    input.canvas_width = 1000.0f;
    
    trace_viewer_step(&tv, &td, input, allocator);

    // 1000 width, 1000 duration -> 1 unit = 1 pixel.
    // x1 = 100 + (200 - 0)/1000 * 1000 = 300
    // x2 = 100 + (800 - 0)/1000 * 1000 = 900
    EXPECT_FLOAT_EQ(tv.selection_layout.x1, 300.0f);
    EXPECT_FLOAT_EQ(tv.selection_layout.x2, 900.0f);

    EXPECT_STREQ(tv.selection_layout.duration_label, "600 us");
}

TEST_F(TraceViewerTest, RulerTickGeneration) {
    tv.viewport.min_ts = 0;
    tv.viewport.start_time = 0;
    tv.viewport.end_time = 1000; // 1ms

    TraceViewerInput input = {};
    input.canvas_x = 0.0f;
    input.canvas_width = 1000.0f;

    trace_viewer_step(&tv, &td, input, allocator);

    // 1000 pixels, 1000us. Interval should be around 100us or 200us.
    // calculate_tick_interval(1000, 1000, 100) -> 100.0
    EXPECT_GT(tv.ruler_ticks.size, 5u);
    
    // Check first few ticks
    EXPECT_STREQ(tv.ruler_ticks[0].label, "0");

    EXPECT_FLOAT_EQ(tv.ruler_ticks[1].x, 100.0f);
    EXPECT_STREQ(tv.ruler_ticks[1].label, "100 us");
}

TEST_F(TraceViewerTest, DoubleClickToZoomEdgeStability) {
    const Theme* theme = theme_get_dark();
    
    // Add an event at the very beginning
    TraceEvent ev = {.name = "event1", .ph = "X", .ts = 0, .dur = 1000, .pid = 1, .tid = 1};
    trace_data_add_event(&td, allocator, theme, &ev);
    
    Track t = {.type = TRACK_TYPE_THREAD, .pid = 1, .tid = 1};
    array_list_push_back(&t.event_indices, allocator, (size_t)0);
    track_update_max_dur(&t, &td, allocator);
    track_calculate_depths(&t, &td, allocator);
    array_list_push_back(&tv.tracks, allocator, t);

    tv.viewport.min_ts = 0;
    tv.viewport.max_ts = 100000;
    tv.viewport.start_time = 0;
    tv.viewport.end_time = 100000;
    tv.last_inner_width = 1000.0f;
    tv.last_inner_height = 500.0f;
    tv.last_tracks_x = 0.0f;
    tv.last_tracks_y = 40.0f;

    TraceViewerInput input = {};
    input.canvas_x = 0.0f;
    input.canvas_y = 0.0f;
    input.canvas_width = 1000.0f;
    input.canvas_height = 500.0f;
    input.lane_height = 20.0f;
    input.ruler_height = 20.0f;
    input.tracks_hovered = true;
    
    // Position mouse over the event at the start (x=0)
    input.mouse_x = 5.0f; 
    input.mouse_y = 50.0f; 
    input.is_mouse_double_clicked = true;
    input.is_mouse_clicked = true;

    trace_viewer_step(&tv, &td, input, allocator);

    // After zoom, the event (0-1000) is centered with 5% padding (50us).
    // Start time: 0 - 50 = -50.
    // End time: 1000 + 50 = 1050.
    EXPECT_DOUBLE_EQ(tv.viewport.start_time, -50.0);
    EXPECT_DOUBLE_EQ(tv.viewport.end_time, 1050.0);
    EXPECT_TRUE(tv.selection_active);
    EXPECT_TRUE(tv.ignore_next_release);

    // Simulate next frame: mouse is still at x=5, but viewport has shifted.
    // mouse_ts at x=5 with viewport [-50, 1050] and width 1000 is:
    // -50 + (5/1000) * 1100 = -50 + 5.5 = -44.5.
    // This is OUTSIDE the selection [0, 1000].
    
    TraceViewerInput input2 = {};
    input2.canvas_width = 1000.0f;
    input2.canvas_height = 500.0f;
    input2.lane_height = 20.0f;
    input2.ruler_height = 20.0f;
    input2.tracks_hovered = true;
    input2.mouse_x = 5.0f;
    input2.mouse_y = 50.0f;
    input2.is_mouse_released = true;

    trace_viewer_step(&tv, &td, input2, allocator);

    // Selection should STILL be active because of ignore_next_release
    EXPECT_TRUE(tv.selection_active);
    EXPECT_FALSE(tv.ignore_next_release);
}

TEST_F(TraceViewerTest, DoubleClickToZoomSameFrameRelease) {
    const Theme* theme = theme_get_dark();
    
    // Add an event
    TraceEvent ev = {.name = "event1", .ph = "X", .ts = 5000, .dur = 1000, .pid = 1, .tid = 1};
    trace_data_add_event(&td, allocator, theme, &ev);
    
    Track t = {.type = TRACK_TYPE_THREAD, .pid = 1, .tid = 1};
    array_list_push_back(&t.event_indices, allocator, (size_t)0);
    track_update_max_dur(&t, &td, allocator);
    track_calculate_depths(&t, &td, allocator);
    array_list_push_back(&tv.tracks, allocator, t);

    tv.viewport.start_time = 0;
    tv.viewport.end_time = 10000;
    tv.last_inner_width = 1000.0f;
    tv.last_inner_height = 500.0f;
    tv.last_tracks_x = 0.0f;
    tv.last_tracks_y = 40.0f;

    TraceViewerInput input = {};
    input.canvas_width = 1000.0f;
    input.canvas_height = 500.0f;
    input.lane_height = 20.0f;
    input.ruler_height = 20.0f;
    input.tracks_hovered = true;
    input.mouse_x = 550.0f; 
    input.mouse_y = 50.0f; 
    input.is_mouse_double_clicked = true;
    input.is_mouse_released = true; // Release in same frame!

    trace_viewer_step(&tv, &td, input, allocator);

    EXPECT_TRUE(tv.selection_active);
    EXPECT_TRUE(tv.ignore_next_release); // Should still be true for the NEXT frame!

    // Next frame, no release
    TraceViewerInput input2 = {};
    input2.tracks_hovered = true;
    trace_viewer_step(&tv, &td, input2, allocator);
    EXPECT_TRUE(tv.selection_active);
    EXPECT_TRUE(tv.ignore_next_release);

    // Next frame, release
    TraceViewerInput input3 = {};
    input3.tracks_hovered = true;
    input3.is_mouse_released = true;
    trace_viewer_step(&tv, &td, input3, allocator);
    EXPECT_TRUE(tv.selection_active);
    EXPECT_FALSE(tv.ignore_next_release);
}

TEST_F(TraceViewerTest, DoubleClickEventOutsideCurrentSelection) {
    const Theme* theme = theme_get_dark();
    
    // Add two events
    TraceEvent ev1 = {.name = "event1", .ph = "X", .ts = 1000, .dur = 1000, .pid = 1, .tid = 1};
    TraceEvent ev2 = {.name = "event2", .ph = "X", .ts = 8000, .dur = 1000, .pid = 1, .tid = 1};
    trace_data_add_event(&td, allocator, theme, &ev1);
    trace_data_add_event(&td, allocator, theme, &ev2);
    
    Track t = {.type = TRACK_TYPE_THREAD, .pid = 1, .tid = 1};
    array_list_push_back(&t.event_indices, allocator, (size_t)0);
    array_list_push_back(&t.event_indices, allocator, (size_t)1);
    track_update_max_dur(&t, &td, allocator);
    track_calculate_depths(&t, &td, allocator);
    array_list_push_back(&tv.tracks, allocator, t);

    tv.viewport.start_time = 0;
    tv.viewport.end_time = 10000;
    tv.last_inner_width = 1000.0f;
    tv.last_inner_height = 500.0f;
    tv.last_tracks_x = 0.0f;
    tv.last_tracks_y = 40.0f;

    // 1. Create a selection around event1
    tv.selection_active = true;
    tv.selection_start_time = 1000.0;
    tv.selection_end_time = 2000.0;

    // 2. Double click on event2 (at ts 8000, which is x=800)
    TraceViewerInput input = {};
    input.canvas_width = 1000.0f;
    input.canvas_height = 500.0f;
    input.lane_height = 20.0f;
    input.ruler_height = 20.0f;
    input.tracks_hovered = true;
    input.mouse_x = 850.0f; 
    input.mouse_y = 50.0f; 
    input.is_mouse_double_clicked = true;

    trace_viewer_step(&tv, &td, input, allocator);

    // Should have zoomed to event2 (8000-9000)
    // Padding: 1000 * 0.05 = 50
    EXPECT_DOUBLE_EQ(tv.viewport.start_time, 7950.0);
    EXPECT_DOUBLE_EQ(tv.viewport.end_time, 9050.0);
    
    // Selection should now be exactly event2
    EXPECT_TRUE(tv.selection_active);
    EXPECT_DOUBLE_EQ(tv.selection_start_time, 8000.0);
    EXPECT_DOUBLE_EQ(tv.selection_end_time, 9000.0);
    EXPECT_EQ(tv.focused_event_idx, 1);
}

TEST_F(TraceViewerTest, FocusAndSelectionCoexistence) {
    // Add two events
    TraceEventPersisted e1 = {}; e1.ts = 100; e1.dur = 50;
    TraceEventPersisted e2 = {}; e2.ts = 500; e2.dur = 50;
    array_list_push_back(&td.events, allocator, e1);
    array_list_push_back(&td.events, allocator, e2);
    
    Track t = {};
    t.type = TRACK_TYPE_THREAD;
    array_list_push_back(&t.event_indices, allocator, (size_t)0);
    array_list_push_back(&t.event_indices, allocator, (size_t)1);
    array_list_resize(&t.depths, allocator, 2);
    t.depths[0] = 0; t.depths[1] = 0;
    track_update_max_dur(&t, &td, allocator);
    array_list_push_back(&tv.tracks, allocator, t);

    tv.viewport.start_time = 0;
    tv.viewport.end_time = 1000;
    tv.last_inner_width = 1000.0f;
    tv.last_tracks_x = 0;
    tv.last_tracks_y = 20.0f;
    tv.last_lane_height = 20.0f;

    // 1. Box select e1
    TraceViewerInput input = {};
    input.canvas_width = 1000.0f;
    input.canvas_height = 1000.0f;
    input.ruler_height = 20.0f;
    input.lane_height = 20.0f;
    input.is_shift_down = true;
    input.is_mouse_clicked = true;
    input.is_mouse_down = true;
    input.mouse_x = 120.0f;
    input.mouse_y = 45.0f;
    input.tracks_hovered = true;
    input.drag_threshold = 5.0f;
    trace_viewer_step(&tv, &td, input, allocator);

    input.is_mouse_clicked = false;
    input.is_mouse_down = false;
    input.is_mouse_released = true;
    input.mouse_x = 160.0f;
    input.mouse_y = 55.0f;
    input.drag_delta_x = 40.0f; // Simulate drag
    trace_viewer_step(&tv, &td, input, allocator);

    EXPECT_EQ(tv.selected_event_indices.size, 1u);
    EXPECT_EQ(tv.selected_event_indices[0], 0);
    EXPECT_EQ(tv.focused_event_idx, -1);

    // 2. Click e2 to focus it
    input.is_shift_down = false;
    input.is_mouse_clicked = true;
    input.is_mouse_down = true;
    input.is_mouse_released = false;
    input.mouse_x = 525.0f;
    input.mouse_y = 50.0f;
    input.drag_delta_x = 0.0f;
    trace_viewer_step(&tv, &td, input, allocator);

    input.is_mouse_clicked = false;
    input.is_mouse_down = false;
    input.is_mouse_released = true;
    trace_viewer_step(&tv, &td, input, allocator);

    EXPECT_EQ(tv.focused_event_idx, 1);
    EXPECT_EQ(tv.selected_event_indices.size, 1u);
    EXPECT_EQ(tv.selected_event_indices[0], 0);

    // 3. Click empty area to clear both
    input.is_mouse_clicked = true;
    input.is_mouse_down = true;
    input.is_mouse_released = false;
    input.mouse_x = 800.0f;
    trace_viewer_step(&tv, &td, input, allocator);

    input.is_mouse_clicked = false;
    input.is_mouse_down = false;
    input.is_mouse_released = true;
    trace_viewer_step(&tv, &td, input, allocator);

    EXPECT_EQ(tv.focused_event_idx, -1);
    EXPECT_EQ(tv.selected_event_indices.size, 1u);
}

TEST_F(TraceViewerTest, ClearBoxSelectionInsideTimelineSelection) {
    // Add an event
    TraceEventPersisted e1 = {}; e1.ts = 100; e1.dur = 50;
    array_list_push_back(&td.events, allocator, e1);
    
    Track t = {};
    t.type = TRACK_TYPE_THREAD;
    array_list_push_back(&t.event_indices, allocator, (size_t)0);
    array_list_resize(&t.depths, allocator, 1);
    t.depths[0] = 0;
    track_update_max_dur(&t, &td, allocator);
    array_list_push_back(&tv.tracks, allocator, t);

    tv.viewport.start_time = 0;
    tv.viewport.end_time = 1000;
    tv.last_inner_width = 1000.0f;
    tv.last_tracks_x = 0;
    tv.last_tracks_y = 20.0f;
    tv.last_lane_height = 20.0f;

    // 1. Create a timeline selection [400, 600]
    tv.selection_active = true;
    tv.selection_start_time = 400.0;
    tv.selection_end_time = 600.0;

    // 2. Add an event to box selection manually
    array_list_push_back(&tv.selected_event_indices, allocator, (int64_t)0);

    // 3. Click empty space inside timeline selection [500]
    TraceViewerInput input = {};
    input.canvas_width = 1000.0f;
    input.canvas_height = 1000.0f;
    input.tracks_hovered = true;
    input.mouse_x = 500.0f;
    input.mouse_y = 50.0f;
    input.is_mouse_released = true;
    input.drag_threshold = 5.0f;
    
    trace_viewer_step(&tv, &td, input, allocator);

    EXPECT_TRUE(tv.selection_active); // Timeline range kept
    EXPECT_EQ(tv.selected_event_indices.size, 1u);
    EXPECT_EQ(tv.focused_event_idx, -1);
}


TEST_F(TraceViewerTest, FocusZeroDurationEvent) {
    // Add a counter event (dur=0) at ts=5000
    TraceEventPersisted e = {.ts = 5000, .dur = 0, .pid = 1, .tid = 1};
    array_list_push_back(&td.events, allocator, e);
    Track t = {.type = TRACK_TYPE_COUNTER, .pid = 1, .tid = -1};
    array_list_push_back(&t.event_indices, allocator, (size_t)0);
    array_list_push_back(&tv.tracks, allocator, t);

    tv.viewport.start_time = 0;
    tv.viewport.end_time = 10000; // Zoom level: 10000 duration
    tv.selection_active = true;
    tv.selection_start_time = 100;
    tv.selection_end_time = 200;

    // Simulate click on the event in the details panel
    tv.target_focused_event_idx = 0;
    TraceViewerInput input = {};
    trace_viewer_step(&tv, &td, input, allocator);

    // Should NOT have changed zoom (duration remains 10000)
    EXPECT_DOUBLE_EQ(tv.viewport.end_time - tv.viewport.start_time, 10000.0);
    
    // Let's move viewport so it's NOT centered
    tv.viewport.start_time = 8000;
    tv.viewport.end_time = 18000;
    tv.target_focused_event_idx = 0;
    trace_viewer_step(&tv, &td, input, allocator);

    // Centered on 5000 with duration 10000 -> [0, 10000]
    EXPECT_DOUBLE_EQ(tv.viewport.start_time, 0.0);
    EXPECT_DOUBLE_EQ(tv.viewport.end_time, 10000.0);
    
    // Selection should NOT be active
    EXPECT_FALSE(tv.selection_active);
}
