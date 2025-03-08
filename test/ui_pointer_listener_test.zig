const std = @import("std");
const c = @import("c.zig");
const utils = @import("utils.zig");

const testing = std.testing;
const expect_vec2_equal = utils.expect_vec2_equal;

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

    // if no button is down, move is ignored
    c.ui_on_mouse_move(c.vec2(50, 50));
    c.ui_begin_frame();
    c.ui_pointer_listener_begin(&.{ .down = &down, .move = &move, .up = &up, .behaviour = c.UI_HIT_TEST_BEHAVIOUR_OPAQUE });
    c.ui_pointer_listener_end();
    c.ui_end_frame();
    try testing.expect(!down.present);
    try testing.expect(!move.present);
    try testing.expect(!up.present);

    c.ui_on_mouse_button_down(c.vec2(50, 50), c.UI_MOUSE_BUTTON_PRIMARY);
    c.ui_begin_frame();
    c.ui_pointer_listener_begin(&.{ .down = &down, .move = &move, .up = &up, .behaviour = c.UI_HIT_TEST_BEHAVIOUR_OPAQUE });
    c.ui_pointer_listener_end();
    c.ui_end_frame();
    try testing.expect(down.present);
    try expect_vec2_equal(c.vec2(50, 50), down.value.local_position);
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
    try expect_vec2_equal(c.vec2(75, 50), move.value.local_position);
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
    try expect_vec2_equal(c.vec2(80, 50), move.value.local_position);
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
    try testing.expect(!move.present);
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
    try expect_vec2_equal(c.vec2(20, 50), up.value.local_position);
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
}
