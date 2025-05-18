const std = @import("std");
const c = @import("c.zig");
const utils = @import("utils.zig");

const testing = std.testing;
const expect_Vec2_IsEqual = utils.expect_Vec2_IsEqual;

test "multiple buttons down" {
    c.ui_set_viewport(c.vec2(0, 0), c.vec2(100, 100));

    var down = c.ui_pointer_event_none();
    var move = c.ui_pointer_event_none();
    var up = c.ui_pointer_event_none();
    c.ui_begin_frame();
    c.ui_pointer_listener_begin(&.{ .down = &down, .move = &move, .up = &up, .behaviour = c.UI_HIT_TEST_BEHAVIOUR_OPAQUE });
    c.ui_pointer_listener_end();
    c.ui_end_frame();
    try testing.expect(!down.present);
    try testing.expect(!move.present);
    try testing.expect(!up.present);

    // no button is down, UI_POINTER_EVENT_HOVER should be emitted
    var hover = c.ui_pointer_event_none();
    c.ui_on_mouse_move(c.vec2(50, 50));
    c.ui_begin_frame();
    c.ui_pointer_listener_begin(&.{ .down = &down, .move = &move, .hover = &hover, .up = &up, .behaviour = c.UI_HIT_TEST_BEHAVIOUR_OPAQUE });
    c.ui_pointer_listener_end();
    c.ui_end_frame();
    try testing.expect(!down.present);
    try testing.expect(!move.present);
    try testing.expect(!up.present);
    try testing.expect(hover.present);
    try testing.expect(hover.value.pointer == 0);
    try expect_Vec2_IsEqual(c.vec2(50, 50), hover.value.local_position);

    c.ui_on_mouse_button_down(c.vec2(50, 50), c.UI_MOUSE_BUTTON_PRIMARY);
    c.ui_begin_frame();
    c.ui_pointer_listener_begin(&.{ .down = &down, .move = &move, .up = &up, .behaviour = c.UI_HIT_TEST_BEHAVIOUR_OPAQUE });
    c.ui_pointer_listener_end();
    c.ui_end_frame();
    try testing.expect(down.present);
    try expect_Vec2_IsEqual(c.vec2(50, 50), down.value.local_position);
    const pointer = down.value.pointer;
    try testing.expect(down.value.button == c.UI_BUTTON_PRIMARY);
    try testing.expect(!move.present);
    try testing.expect(!up.present);

    c.ui_on_mouse_move(c.vec2(75, 50));
    c.ui_begin_frame();
    c.ui_pointer_listener_begin(&.{ .down = &down, .move = &move, .up = &up, .behaviour = c.UI_HIT_TEST_BEHAVIOUR_OPAQUE });
    c.ui_pointer_listener_end();
    c.ui_end_frame();
    try testing.expect(!down.present);
    try testing.expect(move.present);
    try expect_Vec2_IsEqual(c.vec2(75, 50), move.value.local_position);
    try testing.expect(move.value.pointer == pointer);
    try testing.expect(move.value.button == c.UI_BUTTON_PRIMARY);
    try testing.expect(!up.present);

    c.ui_begin_frame();
    c.ui_pointer_listener_begin(&.{ .down = &down, .move = &move, .behaviour = c.UI_HIT_TEST_BEHAVIOUR_OPAQUE });
    c.ui_pointer_listener_end();
    c.ui_end_frame();
    try testing.expect(!down.present);
    try testing.expect(!move.present);
    try testing.expect(!up.present);

    // press button while another button is already down, emitting UI_POINTER_EVENT_MOVE.
    c.ui_on_mouse_button_down(c.vec2(80, 50), c.UI_BUTTON_SECONDARY);
    c.ui_begin_frame();
    c.ui_pointer_listener_begin(&.{ .down = &down, .move = &move, .up = &up, .behaviour = c.UI_HIT_TEST_BEHAVIOUR_OPAQUE });
    c.ui_pointer_listener_end();
    c.ui_end_frame();
    try testing.expect(!down.present);
    try testing.expect(move.present);
    try expect_Vec2_IsEqual(c.vec2(80, 50), move.value.local_position);
    try testing.expect(move.value.pointer == pointer);
    try testing.expect(move.value.button == (c.UI_BUTTON_PRIMARY | c.UI_BUTTON_SECONDARY));
    try testing.expect(!up.present);

    // although the first button is up, there is still other button down, UI_POINTER_EVENT_UP is not emitted.
    c.ui_on_mouse_button_up(c.vec2(30, 50), c.UI_BUTTON_PRIMARY);
    c.ui_begin_frame();
    c.ui_pointer_listener_begin(&.{ .down = &down, .move = &move, .up = &up, .behaviour = c.UI_HIT_TEST_BEHAVIOUR_OPAQUE });
    c.ui_pointer_listener_end();
    c.ui_end_frame();
    try testing.expect(!down.present);
    try testing.expect(move.present);
    try testing.expect(move.value.pointer == pointer);
    try testing.expect(move.value.button == c.UI_BUTTON_SECONDARY);
    try expect_Vec2_IsEqual(c.vec2(30, 50), move.value.local_position);
    try testing.expect(!up.present);

    // No button is down, UI_POINTER_EVENT_UP should be emitted.
    c.ui_on_mouse_button_up(c.vec2(20, 50), c.UI_BUTTON_SECONDARY);
    c.ui_begin_frame();
    c.ui_pointer_listener_begin(&.{ .down = &down, .move = &move, .up = &up, .behaviour = c.UI_HIT_TEST_BEHAVIOUR_OPAQUE });
    c.ui_pointer_listener_end();
    c.ui_end_frame();
    try testing.expect(!down.present);
    try testing.expect(!move.present);
    try testing.expect(up.present);
    try testing.expect(up.value.pointer == pointer);
    try expect_Vec2_IsEqual(c.vec2(20, 50), up.value.local_position);
}

test "pointer is different for each pointer down event" {
    c.ui_set_viewport(c.vec2(0, 0), c.vec2(100, 100));

    var down = c.ui_pointer_event_none();
    c.ui_begin_frame();
    c.ui_pointer_listener_begin(&.{ .down = &down, .behaviour = c.UI_HIT_TEST_BEHAVIOUR_OPAQUE });
    c.ui_pointer_listener_end();
    c.ui_end_frame();
    try testing.expect(!down.present);

    c.ui_on_mouse_button_down(c.vec2(50, 50), c.UI_MOUSE_BUTTON_PRIMARY);
    c.ui_begin_frame();
    c.ui_pointer_listener_begin(&.{ .down = &down, .behaviour = c.UI_HIT_TEST_BEHAVIOUR_OPAQUE });
    c.ui_pointer_listener_end();
    c.ui_end_frame();
    try testing.expect(down.present);
    const pointer1 = down.value.pointer;

    c.ui_on_mouse_button_up(c.vec2(50, 50), c.UI_MOUSE_BUTTON_PRIMARY);
    c.ui_begin_frame();
    c.ui_pointer_listener_begin(&.{ .down = &down, .behaviour = c.UI_HIT_TEST_BEHAVIOUR_OPAQUE });
    c.ui_pointer_listener_end();
    c.ui_end_frame();
    try testing.expect(!down.present);

    c.ui_on_mouse_button_down(c.vec2(50, 50), c.UI_MOUSE_BUTTON_PRIMARY);
    c.ui_begin_frame();
    c.ui_pointer_listener_begin(&.{ .down = &down, .behaviour = c.UI_HIT_TEST_BEHAVIOUR_OPAQUE });
    c.ui_pointer_listener_end();
    c.ui_end_frame();
    try testing.expect(down.present);
    const pointer2 = down.value.pointer;
    try testing.expect(pointer1 != pointer2);

    c.ui_on_mouse_button_up(c.vec2(50, 50), c.UI_MOUSE_BUTTON_PRIMARY);
}

test "hover" {
    c.ui_set_viewport(c.vec2(0, 0), c.vec2(100, 100));

    c.ui_begin_frame();
    c.ui_stack_begin(&.{});
    c.ui_positioned_begin(&.{ .left = c.f32_some(10), .top = c.f32_some(20) });
    c.ui_pointer_listener_begin(&.{ .behaviour = c.UI_HIT_TEST_BEHAVIOUR_OPAQUE });
    c.ui_container_begin(&.{ .width = c.f32_some(30), .height = c.f32_some(30) });
    c.ui_container_end();
    c.ui_pointer_listener_end();
    c.ui_positioned_end();
    c.ui_stack_end();
    c.ui_end_frame();

    var hover = c.ui_pointer_event_none();
    c.ui_on_mouse_move(c.vec2(30, 30));
    c.ui_begin_frame();
    c.ui_stack_begin(&.{});
    c.ui_positioned_begin(&.{ .left = c.f32_some(10), .top = c.f32_some(20) });
    c.ui_pointer_listener_begin(&.{ .hover = &hover, .behaviour = c.UI_HIT_TEST_BEHAVIOUR_OPAQUE });
    c.ui_container_begin(&.{ .width = c.f32_some(30), .height = c.f32_some(30) });
    c.ui_container_end();
    c.ui_pointer_listener_end();
    c.ui_positioned_end();
    c.ui_stack_end();
    c.ui_end_frame();
    try testing.expect(hover.present);
    try expect_Vec2_IsEqual(c.vec2(20, 10), hover.value.local_position);
}

test "enter and exit" {
    c.ui_set_viewport(c.vec2(0, 0), c.vec2(100, 100));

    c.ui_on_mouse_move(c.vec2(0, 0));
    c.ui_begin_frame();
    c.ui_stack_begin(&.{});
    c.ui_positioned_begin(&.{ .left = c.f32_some(10), .top = c.f32_some(20) });
    c.ui_pointer_listener_begin(&.{ .behaviour = c.UI_HIT_TEST_BEHAVIOUR_OPAQUE });
    c.ui_container_begin(&.{ .width = c.f32_some(30), .height = c.f32_some(30) });
    c.ui_container_end();
    c.ui_pointer_listener_end();
    c.ui_positioned_end();
    c.ui_stack_end();
    c.ui_end_frame();

    var enter = c.ui_pointer_event_none();
    var exit = c.ui_pointer_event_none();
    c.ui_on_mouse_move(c.vec2(30, 30));
    c.ui_begin_frame();
    c.ui_stack_begin(&.{});
    c.ui_positioned_begin(&.{ .left = c.f32_some(10), .top = c.f32_some(20) });
    c.ui_pointer_listener_begin(&.{ .enter = &enter, .exit = &exit, .behaviour = c.UI_HIT_TEST_BEHAVIOUR_OPAQUE });
    c.ui_container_begin(&.{ .width = c.f32_some(30), .height = c.f32_some(30) });
    c.ui_container_end();
    c.ui_pointer_listener_end();
    c.ui_positioned_end();
    c.ui_stack_end();
    c.ui_end_frame();
    try testing.expect(enter.present);
    try expect_Vec2_IsEqual(c.vec2(20, 10), enter.value.local_position);
    try testing.expect(!exit.present);

    c.ui_on_mouse_move(c.vec2(0, 0));
    c.ui_begin_frame();
    c.ui_stack_begin(&.{});
    c.ui_positioned_begin(&.{ .left = c.f32_some(10), .top = c.f32_some(20) });
    c.ui_pointer_listener_begin(&.{ .enter = &enter, .exit = &exit, .behaviour = c.UI_HIT_TEST_BEHAVIOUR_OPAQUE });
    c.ui_container_begin(&.{ .width = c.f32_some(30), .height = c.f32_some(30) });
    c.ui_container_end();
    c.ui_pointer_listener_end();
    c.ui_positioned_end();
    c.ui_stack_end();
    c.ui_end_frame();
    try testing.expect(!enter.present);
    try testing.expect(exit.present);
}
