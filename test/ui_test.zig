const std = @import("std");
const c = @import("c.zig");
const utils = @import("utils.zig");

const testing = std.testing;
const log = std.log;
const expect_vec2_eq = utils.expect_vec2_eq;

const AlignmentOption = struct {
    alignment: c.UIAlignment,
    offset: c.Vec2,
};

// fn expectUIBuildError(expected: []const u8) !void {
//     var maybe_err = c.ui_get_first_build_error();
//     while (maybe_err) |err| : (maybe_err = maybe_err.*.next) {
//         const actual = sliceFromStr8(err.*.message);
//         if (std.mem.eql(u8, actual, expected)) {
//             return;
//         }
//     }
//
//     log.err("Expected build error \"{s}\" not found", .{expected});
//     log.err("Existing build error:", .{});
//     maybe_err = c.ui_get_first_build_error();
//     while (maybe_err) |err| : (maybe_err = maybe_err.*.next) {
//         log.err("    {s}", .{sliceFromStr8(err.*.message)});
//     }
//     return error.TestExpectedEqual;
// }

// fn sliceFromStr8(str: c.Str8) []u8 {
//     return str.ptr[0..str.len];
// }
//
// fn expectBoxKey(box: [*c]c.UIBox, expected_key: []const u8) !void {
//     try testing.expectEqualStrings(expected_key, sliceFromStr8(box.*.build.key_str));
// }
//
fn expect_widget_size(widget: [*c]c.UIWidget, expected_size: c.Vec2) !void {
    try expect_vec2_eq(expected_size, widget.*.size);
}

fn expect_widget_offset(widget: [*c]c.UIWidget, expected_offset: c.Vec2) !void {
    try expect_vec2_eq(expected_offset, widget.*.offset);
}

// const State = extern struct {
//     value: u32,
// };
//
// fn pushBoxState() [*c]State {
//     return @ptrCast(@alignCast(c.ui_box_push_state("State", @sizeOf(State))));
// }
//
// test "State, return the same state across frame" {
//     c.ui_init();
//     defer c.ui_quit();
//
//     c.ui_begin_frame(c.vec2(100, 100));
//     _ = c.ui_box_begin(.{});
//     {
//         c.ui_box_begin(.{});
//         {
//             const state = pushBoxState();
//             state.*.value = 10;
//         }
//         c.ui_box_end();
//
//         c.ui_box_begin(.{});
//         {
//             const state = pushBoxState();
//             state.*.value = 11;
//         }
//         c.ui_box_end();
//     }
//     c.ui_box_end();
//     c.ui_end_frame();
//
//     c.ui_begin_frame(c.vec2(100, 100));
//     _ = c.ui_box_begin(.{});
//     {
//         c.ui_box_begin(.{});
//         {
//             const state = pushBoxState();
//             try testing.expectEqual(10, state.*.value);
//         }
//         c.ui_box_end();
//
//         c.ui_box_begin(.{});
//         {
//             const state = pushBoxState();
//             try testing.expectEqual(11, state.*.value);
//         }
//         c.ui_box_end();
//     }
//     c.ui_box_end();
//     c.ui_end_frame();
// }
//
// test "State, reset state if key is different" {
//     c.ui_init();
//     defer c.ui_quit();
//
//     c.ui_begin_frame(c.vec2(100, 100));
//     _ = c.ui_box_begin(.{});
//     {
//         c.ui_box_begin(.{ .key = c.str8_lit("Key0") });
//         {
//             const state = pushBoxState();
//             state.*.value = 10;
//         }
//         c.ui_box_end();
//
//         c.ui_box_begin(.{});
//         {
//             const state = pushBoxState();
//             state.*.value = 11;
//         }
//         c.ui_box_end();
//     }
//     c.ui_box_end();
//     c.ui_end_frame();
//
//     c.ui_begin_frame(c.vec2(100, 100));
//     _ = c.ui_box_begin(.{});
//     {
//         c.ui_box_begin(.{ .key = c.str8_lit("Key1") });
//         {
//             const state = pushBoxState();
//             try testing.expectEqual(0, state.*.value);
//         }
//         c.ui_box_end();
//
//         c.ui_box_begin(.{});
//         {
//             const state = pushBoxState();
//             try testing.expectEqual(11, state.*.value);
//         }
//         c.ui_box_end();
//     }
//     c.ui_box_end();
//     c.ui_end_frame();
// }
//
// test "State, reset state if tag is different" {
//     c.ui_init();
//     defer c.ui_quit();
//
//     c.ui_begin_frame(c.vec2(100, 100));
//     _ = c.ui_box_begin(.{});
//     {
//         c.ui_box_begin(.{});
//         {
//             const state = pushBoxState();
//             state.*.value = 10;
//         }
//         c.ui_box_end();
//
//         c.ui_box_begin(.{});
//         {
//             const state = pushBoxState();
//             state.*.value = 11;
//         }
//         c.ui_box_end();
//     }
//     c.ui_box_end();
//     c.ui_end_frame();
//
//     c.ui_begin_frame(c.vec2(100, 100));
//     _ = c.ui_box_begin(.{});
//     {
//         c.ui_tag_begin("Tag", .{});
//         {
//             const state = pushBoxState();
//             try testing.expectEqual(0, state.*.value);
//         }
//         c.ui_tag_end("Tag");
//
//         c.ui_box_begin(.{});
//         {
//             const state = pushBoxState();
//             try testing.expectEqual(11, state.*.value);
//         }
//         c.ui_box_end();
//     }
//     c.ui_box_end();
//     c.ui_end_frame();
// }
//
// test "State, reset state if box is not build from last frame" {
//     c.ui_init();
//     defer c.ui_quit();
//
//     c.ui_begin_frame(c.vec2(100, 100));
//     _ = c.ui_box_begin(.{});
//     {
//         c.ui_box_begin(.{});
//         {
//             const state = pushBoxState();
//             state.*.value = 10;
//         }
//         c.ui_box_end();
//
//         c.ui_box_begin(.{});
//         {
//             const state = pushBoxState();
//             state.*.value = 11;
//         }
//         c.ui_box_end();
//     }
//     c.ui_box_end();
//     c.ui_end_frame();
//
//     c.ui_begin_frame(c.vec2(100, 100));
//     _ = c.ui_box_begin(.{});
//     {
//         c.ui_box_begin(.{});
//         {
//             const state = pushBoxState();
//             try testing.expectEqual(10, state.*.value);
//         }
//         c.ui_box_end();
//     }
//     c.ui_box_end();
//     c.ui_end_frame();
//
//     c.ui_begin_frame(c.vec2(100, 100));
//     _ = c.ui_box_begin(.{});
//     {
//         c.ui_box_begin(.{});
//         {
//             const state = pushBoxState();
//             try testing.expectEqual(10, state.*.value);
//         }
//         c.ui_box_end();
//
//         c.ui_box_begin(.{});
//         {
//             const state = pushBoxState();
//             try testing.expectEqual(0, state.*.value);
//         }
//         c.ui_box_end();
//     }
//     c.ui_box_end();
//     c.ui_end_frame();
// }
//
// test "State, reset state if parent is different" {
//     c.ui_init();
//     defer c.ui_quit();
//
//     c.ui_begin_frame(c.vec2(100, 100));
//     _ = c.ui_box_begin(.{});
//     {
//         _ = c.ui_box_begin(.{});
//         {
//             c.ui_box_begin(.{});
//             {
//                 const state = pushBoxState();
//                 state.*.value = 11;
//             }
//             c.ui_box_end();
//         }
//         c.ui_box_end();
//     }
//     c.ui_box_end();
//     c.ui_end_frame();
//
//     c.ui_begin_frame(c.vec2(100, 100));
//     _ = c.ui_box_begin(.{});
//     {
//         _ = c.ui_tag_begin("Tag", .{});
//         {
//             c.ui_box_begin(.{});
//             {
//                 const state = pushBoxState();
//                 try testing.expectEqual(0, state.*.value);
//             }
//             c.ui_box_end();
//         }
//         c.ui_tag_end("Tag");
//     }
//     c.ui_box_end();
//     c.ui_end_frame();
// }
//

test "root has the same size as the screen" {
    c.ui_set_viewport(c.vec2(0, 0), c.vec2(100, 100));

    const constraints: []const c.UIBoxConstraintsO = &.{
        c.ui_box_constraints_none(),
        c.ui_box_constraints_some(c.ui_box_constraints_tight(100, 100)),
    };

    for (constraints) |con| {
        c.ui_begin_frame();
        c.ui_container_begin(&.{
            .constraints = con,
        });
        c.ui_container_end();
        c.ui_end_frame();

        const root = c.ui_widget_get_root();
        try expect_widget_size(root, c.vec2(100, 100));
    }
}

test "UIAlign" {
    const options: []const AlignmentOption = &.{
        .{ .alignment = c.ui_alignment_top_left(), .offset = c.vec2(0, 0) },
        .{ .alignment = c.ui_alignment_top_center(), .offset = c.vec2(25, 0) },
        .{ .alignment = c.ui_alignment_top_right(), .offset = c.vec2(50, 0) },
        .{ .alignment = c.ui_alignment_center_left(), .offset = c.vec2(0, 25) },
        .{ .alignment = c.ui_alignment_center(), .offset = c.vec2(25, 25) },
        .{ .alignment = c.ui_alignment_center_right(), .offset = c.vec2(50, 25) },
        .{ .alignment = c.ui_alignment_bottom_left(), .offset = c.vec2(0, 50) },
        .{ .alignment = c.ui_alignment_bottom_center(), .offset = c.vec2(25, 50) },
        .{ .alignment = c.ui_alignment_bottom_right(), .offset = c.vec2(50, 50) },
    };

    c.ui_set_viewport(c.vec2(0, 0), c.vec2(100, 100));
    for (options) |option| {
        var container: [*c]c.UIWidget = undefined;

        c.ui_begin_frame();
        c.ui_align_begin(&.{
            .alignment = option.alignment,
        });
        {
            c.ui_container_begin(&.{
                .width = c.f32_some(50),
                .height = c.f32_some(50),
            });
            c.ui_container_end();
            container = c.ui_widget_get_last_child();
        }
        c.ui_align_end();
        c.ui_end_frame();

        try expect_widget_offset(container, option.offset);
        try expect_widget_size(container, c.vec2(50, 50));
    }
}

test "UIContainer - no child and no size, as big as possible" {
    c.ui_set_viewport(c.vec2(0, 0), c.vec2(100, 100));

    c.ui_begin_frame();
    c.ui_center_begin(&.{});
    {
        c.ui_container_begin(&.{});
        c.ui_container_end();
    }
    const container = c.ui_widget_get_last_child();
    c.ui_center_end();
    c.ui_end_frame();

    try expect_widget_size(container, c.vec2(100, 100));
}

test "UIContainer - no size but has a child, same size as its child" {
    c.ui_set_viewport(c.vec2(0, 0), c.vec2(100, 100));

    c.ui_begin_frame();
    c.ui_center_begin(&.{});
    {
        c.ui_container_begin(&.{});
        c.ui_container_begin(&.{
            .width = c.f32_some(30),
            .height = c.f32_some(30),
        });
        c.ui_container_end();
        c.ui_container_end();
    }
    const container = c.ui_widget_get_last_child();
    c.ui_center_end();
    c.ui_end_frame();

    try expect_widget_size(container, c.vec2(30, 30));
}

test "UIPadding" {
    const padding = c.ui_edge_insets(1, 2, 3, 4);

    c.ui_set_viewport(c.vec2(0, 0), c.vec2(100, 100));
    c.ui_begin_frame();
    _ = c.ui_center_begin(&.{});
    {
        c.ui_container_begin(&.{
            .padding = c.ui_edge_insets_some(padding),
        });
        c.ui_container_begin(&.{
            .width = c.f32_some(30),
            .height = c.f32_some(30),
        });
        c.ui_container_end();
        c.ui_container_end();
    }
    const container = c.ui_widget_get_last_child();
    c.ui_center_end();
    c.ui_end_frame();

    try expect_widget_size(container, c.vec2(33, 37));
}

test "UIConstrainedBox - respect parent constraints" {
    c.ui_set_viewport(c.vec2(0, 0), c.vec2(100, 100));

    c.ui_begin_frame();
    c.ui_constrained_box_begin(&.{
        .constraints = c.ui_box_constraints(50, 150, 50, 150),
    });
    c.ui_container_begin(&.{
        .width = c.f32_some(10),
        .height = c.f32_some(10),
    });
    c.ui_container_end();
    const container = c.ui_widget_get_last_child();
    c.ui_constrained_box_end();
    c.ui_end_frame();

    try expect_widget_size(container, c.vec2(100, 100));
}

test "UIConstrainedBox - apply additional constraints (1)" {
    c.ui_set_viewport(c.vec2(0, 0), c.vec2(100, 100));

    c.ui_begin_frame();
    c.ui_center_begin(&.{});
    c.ui_constrained_box_begin(&.{
        .constraints = c.ui_box_constraints(50, 150, 50, 150),
    });
    c.ui_container_begin(&.{
        .width = c.f32_some(10),
        .height = c.f32_some(10),
    });
    c.ui_container_end();
    const container = c.ui_widget_get_last_child();
    c.ui_constrained_box_end();
    c.ui_center_end();
    c.ui_end_frame();

    try expect_widget_size(container, c.vec2(50, 50));
}

test "UIConstrainedBox - apply additional constraints (2)" {
    c.ui_set_viewport(c.vec2(0, 0), c.vec2(100, 100));

    c.ui_begin_frame();
    c.ui_center_begin(&.{});
    c.ui_constrained_box_begin(&.{
        .constraints = c.ui_box_constraints(50, 80, 50, 80),
    });
    c.ui_container_begin(&.{
        .width = c.f32_some(1000),
        .height = c.f32_some(1000),
    });
    c.ui_container_end();
    const container = c.ui_widget_get_last_child();
    c.ui_constrained_box_end();
    c.ui_center_end();
    c.ui_end_frame();

    try expect_widget_size(container, c.vec2(80, 80));
}

test "UIConstrainedBox - apply additional constraints (3)" {
    c.ui_set_viewport(c.vec2(0, 0), c.vec2(100, 100));

    c.ui_begin_frame();
    c.ui_center_begin(&.{});
    c.ui_constrained_box_begin(&.{
        .constraints = c.ui_box_constraints(50, 80, 50, 80),
    });
    c.ui_container_begin(&.{
        .width = c.f32_some(60),
        .height = c.f32_some(70),
    });
    c.ui_container_end();
    const container = c.ui_widget_get_last_child();
    c.ui_constrained_box_end();
    c.ui_center_end();
    c.ui_end_frame();

    try expect_widget_size(container, c.vec2(60, 70));
}

test "UIUnconstrainedBox - child is small" {
    c.ui_set_viewport(c.vec2(0, 0), c.vec2(100, 100));

    c.ui_begin_frame();
    c.ui_unconstrained_box_begin(&.{});
    c.ui_container_begin(&.{
        .width = c.f32_some(10),
        .height = c.f32_some(10),
    });
    c.ui_container_end();
    const container = c.ui_widget_get_last_child();
    c.ui_unconstrained_box_end();
    c.ui_end_frame();

    try expect_widget_size(container, c.vec2(10, 10));
}

test "UIUnconstrainedBox - child is large" {
    c.ui_set_viewport(c.vec2(0, 0), c.vec2(100, 100));

    c.ui_begin_frame();
    c.ui_unconstrained_box_begin(&.{});
    c.ui_container_begin(&.{
        .width = c.f32_some(1000),
        .height = c.f32_some(10),
    });
    c.ui_container_end();
    const container = c.ui_widget_get_last_child();
    c.ui_unconstrained_box_end();
    c.ui_end_frame();

    try expect_widget_size(container, c.vec2(1000, 10));
}

// test "Layout, with one child, size around it" {
//     c.ui_init();
//     defer c.ui_quit();
//
//     var container: [*c]c.UIBox = undefined;
//
//     c.ui_begin_frame(c.vec2(100, 100));
//     _ = c.ui_box_begin(.{});
//     {
//         _ = c.ui_box_begin(.{});
//         {
//             c.ui_box_begin(.{ .size = c.vec2(50, 50) });
//             container = c.ui_box_get_current();
//             c.ui_box_end();
//         }
//         c.ui_box_end();
//     }
//     c.ui_box_end();
//     c.ui_end_frame();
//
//     try expectBoxSize(container, c.vec2(50, 50));
// }
//
// test "Layout, with fixed size" {
//     c.ui_init();
//     defer c.ui_quit();
//
//     var container: [*c]c.UIBox = undefined;
//     var child: [*c]c.UIBox = undefined;
//
//     c.ui_begin_frame(c.vec2(100, 100));
//     _ = c.ui_box_begin(.{});
//     {
//         c.ui_box_begin(.{ .size = c.vec2(30, 20) });
//         {
//             container = c.ui_box_get_current();
//             c.ui_box_begin(.{ .size = c.vec2(50, 40) });
//             child = c.ui_box_get_current();
//             c.ui_box_end();
//         }
//         c.ui_box_end();
//     }
//     c.ui_box_end();
//     c.ui_end_frame();
//
//     try expectBoxSize(container, c.vec2(30, 20));
//     try expectBoxSize(child, c.vec2(30, 20));
// }
//
// test "Layout, with fixed size, negative" {
//     c.ui_init();
//     defer c.ui_quit();
//
//     var container: [*c]c.UIBox = undefined;
//     var child: [*c]c.UIBox = undefined;
//
//     c.ui_begin_frame(c.vec2(100, 100));
//     _ = c.ui_box_begin(.{});
//     {
//         c.ui_box_begin(.{ .size = c.vec2(30, -20) });
//         {
//             container = c.ui_box_get_current();
//             c.ui_box_begin(.{ .size = c.vec2(50, 50) });
//             child = c.ui_box_get_current();
//             c.ui_box_end();
//         }
//         c.ui_box_end();
//     }
//     c.ui_box_end();
//     c.ui_end_frame();
//
//     try expectBoxSize(container, c.vec2(30, 0));
//     try expectBoxSize(child, c.vec2(30, 0));
// }
//
// test "Layout, main axis size, child has different main axis than parent" {
//     c.ui_init();
//     defer c.ui_quit();
//
//     const main_axis_sizes: []const c.UIMainAxisSize = &.{
//         c.UI_MAIN_AXIS_SIZE_MIN,
//         c.UI_MAIN_AXIS_SIZE_MAX,
//     };
//
//     for (main_axis_sizes) |main_axis_size| {
//         var row: [*c]c.UIBox = undefined;
//
//         c.ui_begin_frame(c.vec2(100, 100));
//         _ = c.ui_box_begin(.{ .main_axis = c.kAxis2Y });
//         {
//             c.ui_box_begin(.{ .main_axis_size = main_axis_size });
//             {
//                 row = c.ui_box_get_current();
//                 _ = c.ui_box_begin(.{ .size = c.vec2(30, 20) });
//                 c.ui_box_end();
//             }
//             c.ui_box_end();
//         }
//         c.ui_box_end();
//         c.ui_end_frame();
//
//         if (main_axis_size == c.UI_MAIN_AXIS_SIZE_MIN) {
//             try expectBoxSize(row, c.vec2(30, 20));
//         } else {
//             try expectBoxSize(row, c.vec2(100, 20));
//         }
//     }
// }
//
// test "Layout, main axis size, child has same main axis as parent" {
//     c.ui_init();
//     defer c.ui_quit();
//
//     const main_axis_sizes: []const c.UIMainAxisSize = &.{
//         c.UI_MAIN_AXIS_SIZE_MIN,
//         c.UI_MAIN_AXIS_SIZE_MAX,
//     };
//
//     for (main_axis_sizes) |main_axis_size| {
//         var row: [*c]c.UIBox = undefined;
//
//         c.ui_begin_frame(c.vec2(100, 100));
//         _ = c.ui_box_begin(.{});
//         {
//             c.ui_box_begin(.{ .main_axis_size = main_axis_size });
//             {
//                 row = c.ui_box_get_current();
//                 _ = c.ui_box_begin(.{ .size = c.vec2(20, 20) });
//                 c.ui_box_end();
//             }
//             c.ui_box_end();
//         }
//         c.ui_box_end();
//         c.ui_end_frame();
//
//         if (main_axis_size == c.UI_MAIN_AXIS_SIZE_MIN) {
//             try expectBoxSize(row, c.vec2(20, 20));
//         } else {
//             try expectBoxSize(row, c.vec2(100, 20));
//         }
//     }
// }
//
// // TODO: test min/max size
//
// test "Layout, no fixed size, size around text" {
//     c.ui_init();
//     defer c.ui_quit();
//
//     var text: [*c]c.UIBox = undefined;
//
//     c.ui_begin_frame(c.vec2(100, 100));
//     _ = c.ui_box_begin(.{});
//     {
//         c.ui_box_begin(.{ .text = c.ui_push_str8f("text") });
//         text = c.ui_box_get_current();
//         c.ui_box_end();
//     }
//     c.ui_box_end();
//     c.ui_end_frame();
//
//     const text_metrics = c.get_text_metrics_str8(c.str8_lit("Text"), c.kUIFontSizeDefault);
//     try expectBoxSize(text, text_metrics.size);
// }
//
// test "Layout, fixed size, truncate text" {
//     c.ui_init();
//     defer c.ui_quit();
//
//     var text: [*c]c.UIBox = undefined;
//
//     c.ui_begin_frame(c.vec2(100, 100));
//     _ = c.ui_box_begin(.{});
//     {
//         c.ui_box_begin(.{ .size = c.vec2(2, 2), .text = c.ui_push_str8f("Text") });
//         text = c.ui_box_get_current();
//         c.ui_box_end();
//     }
//     c.ui_box_end();
//     c.ui_end_frame();
//
//     try expectBoxSize(text, c.vec2(2, 2));
// }
//
// test "Layout, row, no constraints on children" {
//     c.ui_init();
//     defer c.ui_quit();
//
//     var c0: [*c]c.UIBox = undefined;
//     var c1: [*c]c.UIBox = undefined;
//
//     c.ui_begin_frame(c.vec2(1000, 100));
//     _ = c.ui_box_begin(.{});
//     {
//         c.ui_box_begin(.{ .text = c.ui_push_str8f("Hello!") });
//         c0 = c.ui_box_get_current();
//         c.ui_box_end();
//
//         c.ui_box_begin(.{ .text = c.ui_push_str8f("Goodbye!") });
//         c1 = c.ui_box_get_current();
//         c.ui_box_end();
//     }
//     c.ui_box_end();
//     c.ui_end_frame();
//
//     const c0_text_size = c.get_text_metrics_str8(c.str8_lit("Hello!"), c.kUIFontSizeDefault).size;
//     const c1_text_size = c.get_text_metrics_str8(c.str8_lit("Goodbye!"), c.kUIFontSizeDefault).size;
//
//     try testing.expect(c0_text_size.x + c1_text_size.x < 1000);
//     try expectBoxSize(c0, c0_text_size);
//     try expectBoxRelPos(c0, c.vec2(0, 0));
//     try expectBoxSize(c1, c1_text_size);
//     try expectBoxRelPos(c1, c.vec2(c0_text_size.x, 0));
// }
//
// test "Layout, row, no constraints on children, but truncate" {
//     c.ui_init();
//     defer c.ui_quit();
//
//     var c0: [*c]c.UIBox = undefined;
//     var c1: [*c]c.UIBox = undefined;
//
//     c.ui_begin_frame(c.vec2(100, 100));
//     _ = c.ui_box_begin(.{});
//     {
//         c.ui_box_begin(.{ .text = c.ui_push_str8f("Hello!") });
//         c0 = c.ui_box_get_current();
//         c.ui_box_end();
//
//         c.ui_box_begin(.{ .text = c.ui_push_str8f("Goodbye!") });
//         c1 = c.ui_box_get_current();
//         c.ui_box_end();
//     }
//     c.ui_box_end();
//     c.ui_end_frame();
//
//     const c0_text_size = c.get_text_metrics_str8(c.str8_lit("Hello!"), c.kUIFontSizeDefault).size;
//     const c1_text_size = c.get_text_metrics_str8(c.str8_lit("Goodbye!"), c.kUIFontSizeDefault).size;
//
//     try testing.expect(c0_text_size.x + c1_text_size.x > 100);
//     try expectBoxSize(c0, c0_text_size);
//     try expectBoxRelPos(c0, c.vec2(0, 0));
//     try expectBoxSize(c1, c.vec2(100 - c0_text_size.x, c1_text_size.y));
//     try expectBoxRelPos(c1, c.vec2(c0_text_size.x, 0));
// }
//
// test "Layout, row, constraint flex" {
//     c.ui_init();
//     defer c.ui_quit();
//
//     var c0: [*c]c.UIBox = undefined;
//     var c1: [*c]c.UIBox = undefined;
//
//     c.ui_begin_frame(c.vec2(100, 100));
//     _ = c.ui_box_begin(.{});
//     {
//         c.ui_box_begin(.{
//             .flex = 1,
//             .text = c.ui_push_str8f("A very long text that doesn't fit in one line!"),
//         });
//         c0 = c.ui_box_get_current();
//         c.ui_box_end();
//
//         c.ui_box_begin(.{ .text = c.ui_push_str8f("Goodbye!") });
//         c1 = c.ui_box_get_current();
//         c.ui_box_end();
//     }
//     c.ui_box_end();
//     c.ui_end_frame();
//
//     const c0_text_size = c.get_text_metrics_str8(c.str8_lit("A very long text that doesn't fit in one line!"), c.kUIFontSizeDefault).size;
//     const c1_text_size = c.get_text_metrics_str8(c.str8_lit("Goodbye!"), c.kUIFontSizeDefault).size;
//
//     try testing.expect(c0_text_size.x > 100);
//     try expectBoxSize(c0, c.vec2(100 - c1_text_size.x, c0_text_size.y));
//     try expectBoxRelPos(c0, c.vec2(0, 0));
//     try expectBoxSize(c1, c1_text_size);
//     try expectBoxRelPos(c1, c.vec2(100 - c1_text_size.x, 0));
// }
//
// test "Layout, main axis unbounded" {
//     c.ui_init();
//     defer c.ui_quit();
//
//     var container: [*c]c.UIBox = undefined;
//     var child: [*c]c.UIBox = undefined;
//
//     c.ui_begin_frame(c.vec2(100, 100));
//     _ = c.ui_box_begin(.{});
//     {
//         c.ui_box_begin(.{ .size = c.vec2(c.kUISizeInfinity, c.kUISizeUndefined) });
//         {
//             container = c.ui_box_get_current();
//             c.ui_box_begin(.{});
//             {
//                 child = c.ui_box_get_current();
//                 _ = c.ui_box_begin(.{ .size = c.vec2(100000, 10) });
//                 c.ui_box_end();
//
//                 _ = c.ui_box_begin(.{ .size = c.vec2(100000, 20) });
//                 c.ui_box_end();
//             }
//             c.ui_box_end();
//         }
//         c.ui_box_end();
//     }
//     c.ui_box_end();
//     c.ui_end_frame();
//
//     try expectBoxSize(container, c.vec2(100, 20));
//     try expectBoxSize(child, c.vec2(200000, 20));
// }
//
// test "Layout, main axis unbounded, with unbounded content" {
//     c.ui_init();
//     defer c.ui_quit();
//
//     c.ui_begin_frame(c.vec2(100, 100));
//     _ = c.ui_box_begin(.{});
//     {
//         _ = c.ui_box_begin(.{ .size = c.vec2(c.kUISizeInfinity, c.kUISizeUndefined) });
//         {
//             _ = c.ui_box_begin(.{ .size = c.vec2(c.kUISizeInfinity, c.kUISizeUndefined) });
//             c.ui_box_end();
//         }
//         c.ui_box_end();
//     }
//     c.ui_box_end();
//     c.ui_end_frame();
//
//     try expectUIBuildError("Cannot have unbounded content within unbounded constraint");
// }
