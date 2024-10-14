const std = @import("std");
const c = @cImport({
    @cInclude("src/draw.h");
    @cInclude("src/math.h");
    @cInclude("src/ui.h");
});

const testing = std.testing;
const log = std.log;

const MainAxisOption = struct {
    @"align": c.UIMainAxisAlign,
    rel_pos: f32,
};
const CrossAxisOption = struct {
    @"align": c.UICrossAxisAlign,
    rel_pos: f32,
};

fn expectUIBuildError(expected: []const u8) !void {
    var maybe_err = c.GetFirstUIBuildError();
    while (maybe_err) |err| : (maybe_err = maybe_err.*.next) {
        const actual = sliceFromStr8(err.*.message);
        if (std.mem.eql(u8, actual, expected)) {
            return;
        }
    }

    log.err("Expected build error \"{s}\" not found", .{expected});
    log.err("Existing build error:", .{});
    maybe_err = c.GetFirstUIBuildError();
    while (maybe_err) |err| : (maybe_err = maybe_err.*.next) {
        log.err("    {s}", .{sliceFromStr8(err.*.message)});
    }
    return error.TestExpectedEqual;
}

fn expectEqualVec2(expected: c.Vec2, actual: c.Vec2) !void {
    if (c.IsEqualVec2(expected, actual) == 0) {
        log.err("expected Vec2({d:.2}, {d:.2}), but got Vec2({d:.2}, {d:.2})", .{
            expected.x,
            expected.y,
            actual.x,
            actual.y,
        });
        return error.TestExpectedEqual;
    }
}

fn sliceFromStr8(str: c.Str8) []u8 {
    return str.ptr[0..str.len];
}

fn expectBoxKey(box: [*c]c.UIBox, expected_key: []const u8) !void {
    try testing.expectEqualStrings(expected_key, sliceFromStr8(box.*.build.key_str));
}

fn expectBoxSize(box: [*c]c.UIBox, expected_size: c.Vec2) !void {
    try expectEqualVec2(expected_size, box.*.computed.size);
}

fn expectBoxRelPos(box: [*c]c.UIBox, expected_rel_pos: c.Vec2) !void {
    try expectEqualVec2(expected_rel_pos, box.*.computed.rel_pos);
}

fn expectBoxText(box: [*c]c.UIBox, expected_text: []const u8) !void {
    try testing.expectEqualStrings(expected_text, sliceFromStr8(box.*.build.text));
}

test "Key, return the same box across frame" {
    var box1: [*c]c.UIBox = null;
    var box2: [*c]c.UIBox = null;

    c.BeginUIFrame(c.V2(100, 100));
    c.BeginUILayer();
    c.BeginUIBox(.{});
    {
        c.BeginUIBox(.{});
        c.EndUIBox();

        const key = c.PushUIKeyF("KEY");
        c.BeginUIBox(.{ .key = key });
        box1 = c.GetUIBox(key);
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();

    c.BeginUIFrame(c.V2(100, 100));
    c.BeginUILayer();
    c.BeginUIBox(.{});
    {
        const key = c.PushUIKeyF("KEY");
        c.BeginUIBox(.{ .key = key });
        box2 = c.GetUIBox(key);
        c.EndUIBox();

        c.BeginUIBox(.{});
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();

    try testing.expectEqual(box1, box2);
}

test "Layout, root has the same size as the screen" {
    const sizes: []const c.Vec2 = &.{
        c.V2(c.kUISizeUndefined, c.kUISizeUndefined),
        c.V2(50, 50),
        c.V2(50, 200),
        c.V2(200, 50),
        c.V2(200, 200),
    };

    for (sizes) |size| {
        c.BeginUIFrame(c.V2(100, 100));
        c.BeginUILayer();
        const key = c.PushUIKeyF("Root");
        c.BeginUIBox(.{ .key = key, .size = size });
        c.EndUIBox();
        c.EndUILayer();
        c.EndUIFrame();

        const root = c.GetUIBox(key);
        try expectBoxSize(root, c.V2(100, 100));
    }
}

test "Layout, aligns" {
    const main_axis_options: []const MainAxisOption = &.{
        .{ .@"align" = c.kUIMainAxisAlignStart, .rel_pos = 0 },
        .{ .@"align" = c.kUIMainAxisAlignCenter, .rel_pos = 25 },
        .{ .@"align" = c.kUIMainAxisAlignEnd, .rel_pos = 50 },
    };
    const cross_axis_options: []const CrossAxisOption = &.{
        .{ .@"align" = c.kUICrossAxisAlignStart, .rel_pos = 0 },
        .{ .@"align" = c.kUICrossAxisAlignCenter, .rel_pos = 25 },
        .{ .@"align" = c.kUICrossAxisAlignEnd, .rel_pos = 50 },
    };

    for (main_axis_options) |main_axis_option| {
        for (cross_axis_options) |cross_axis_option| {
            c.BeginUIFrame(c.V2(100, 100));
            c.BeginUILayer();
            const key = c.PushUIKeyF("key");
            c.BeginUIBox(.{
                .main_axis_align = main_axis_option.@"align",
                .cross_axis_align = cross_axis_option.@"align",
            });
            {
                c.BeginUIBox(.{ .key = key, .size = c.V2(50, 50) });
                c.EndUIBox();
            }
            c.EndUIBox();
            c.EndUILayer();
            c.EndUIFrame();

            const container = c.GetUIBox(key);
            try expectBoxRelPos(container, c.V2(main_axis_option.rel_pos, cross_axis_option.rel_pos));
            try expectBoxSize(container, c.V2(50, 50));
        }
    }
}

test "Layout, padding" {
    const padding = c.UIEdgeInsetsFromSTEB(1, 2, 3, 4);
    const main_axis_options: []const MainAxisOption = &.{
        .{ .@"align" = c.kUIMainAxisAlignStart, .rel_pos = 1 },
        .{ .@"align" = c.kUIMainAxisAlignCenter, .rel_pos = 24 },
        .{ .@"align" = c.kUIMainAxisAlignEnd, .rel_pos = 47 },
    };
    const cross_axis_options: []const CrossAxisOption = &.{
        .{ .@"align" = c.kUICrossAxisAlignStart, .rel_pos = 2 },
        .{ .@"align" = c.kUICrossAxisAlignCenter, .rel_pos = 24 },
        .{ .@"align" = c.kUICrossAxisAlignEnd, .rel_pos = 46 },
    };

    for (main_axis_options) |main_axis_option| {
        for (cross_axis_options) |cross_axis_option| {
            c.BeginUIFrame(c.V2(100, 100));
            const key = c.PushUIKeyF("key");
            c.BeginUILayer();
            c.BeginUIBox(.{
                .main_axis_align = main_axis_option.@"align",
                .cross_axis_align = cross_axis_option.@"align",
                .padding = padding,
            });
            {
                c.BeginUIBox(.{ .key = key });
                {
                    c.BeginUIBox(.{ .size = c.V2(50, 50) });
                    c.EndUIBox();
                }
                c.EndUIBox();
            }
            c.EndUIBox();
            c.EndUILayer();
            c.EndUIFrame();

            const container = c.GetUIBox(key);
            try expectBoxRelPos(container, c.V2(main_axis_option.rel_pos, cross_axis_option.rel_pos));
            try expectBoxSize(container, c.V2(50, 50));
        }
    }
}

test "Layout, no children, no fixed size, as small as possible" {
    c.BeginUIFrame(c.V2(100, 100));
    const key = c.PushUIKeyF("key");
    c.BeginUILayer();
    c.BeginUIBox(.{});
    {
        c.BeginUIBox(.{ .key = key });
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();

    const container = c.GetUIBox(key);
    try expectBoxSize(container, c.V2(0, 0));
}

test "Layout, no children, no fixed size, flex, main axis is as big as possible" {
    c.BeginUIFrame(c.V2(100, 100));
    const key = c.PushUIKeyF("key");
    c.BeginUILayer();
    c.BeginUIBox(.{});
    {
        c.BeginUIBox(.{ .key = key, .flex = 1 });
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();

    const container = c.GetUIBox(key);
    try expectBoxSize(container, c.V2(100, 0));
}

test "Layout, with one child, size around it" {
    c.BeginUIFrame(c.V2(100, 100));
    const key = c.PushUIKeyF("key");
    c.BeginUILayer();
    c.BeginUIBox(.{});
    {
        c.BeginUIBox(.{});
        {
            c.BeginUIBox(.{ .key = key, .size = c.V2(50, 50) });
            c.EndUIBox();
        }
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();

    const container = c.GetUIBox(key);
    try expectBoxSize(container, c.V2(50, 50));
}

test "Layout, with fixed size" {
    c.BeginUIFrame(c.V2(100, 100));
    const container_key = c.PushUIKeyF("container");
    const child_key = c.PushUIKeyF("child");
    c.BeginUILayer();
    c.BeginUIBox(.{});
    {
        c.BeginUIBox(.{ .key = container_key, .size = c.V2(30, 20) });
        {
            c.BeginUIBox(.{ .key = child_key, .size = c.V2(50, 40) });
            c.EndUIBox();
        }
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();

    const container = c.GetUIBox(container_key);
    const child = c.GetUIBox(child_key);
    try expectBoxSize(container, c.V2(30, 20));
    try expectBoxSize(child, c.V2(30, 20));
}

test "Layout, with fixed size, negative" {
    c.BeginUIFrame(c.V2(100, 100));
    const container_key = c.PushUIKeyF("container");
    const child_key = c.PushUIKeyF("child");
    c.BeginUILayer();
    c.BeginUIBox(.{});
    {
        c.BeginUIBox(.{ .key = container_key, .size = c.V2(30, -20) });
        {
            c.BeginUIBox(.{ .key = child_key, .size = c.V2(50, 50) });
            c.EndUIBox();
        }
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();

    const container = c.GetUIBox(container_key);
    const child = c.GetUIBox(child_key);
    try expectBoxSize(container, c.V2(30, 0));
    try expectBoxSize(child, c.V2(30, 0));
}

test "Layout, main axis size, child has different main axis than parent" {
    const main_axis_sizes: []const c.UIMainAxisSize = &.{
        c.kUIMainAxisSizeMin,
        c.kUIMainAxisSizeMax,
    };

    for (main_axis_sizes) |main_axis_size| {
        c.BeginUIFrame(c.V2(100, 100));
        const key = c.PushUIKeyF("key");
        c.BeginUILayer();
        c.BeginUIBox(.{ .main_axis = c.kAxis2Y });
        {
            c.BeginUIBox(.{ .key = key, .main_axis_size = main_axis_size });
            {
                c.BeginUIBox(.{ .size = c.V2(30, 20) });
                c.EndUIBox();
            }
            c.EndUIBox();
        }
        c.EndUIBox();
        c.EndUILayer();
        c.EndUIFrame();

        const row = c.GetUIBox(key);
        if (main_axis_size == c.kUIMainAxisSizeMin) {
            try expectBoxSize(row, c.V2(30, 20));
        } else {
            try expectBoxSize(row, c.V2(100, 20));
        }
    }
}

test "Layout, main axis size, child has same main axis as parent" {
    const main_axis_sizes: []const c.UIMainAxisSize = &.{
        c.kUIMainAxisSizeMin,
        c.kUIMainAxisSizeMax,
    };

    for (main_axis_sizes) |main_axis_size| {
        c.BeginUIFrame(c.V2(100, 100));
        const key = c.PushUIKeyF("key");
        c.BeginUILayer();
        c.BeginUIBox(.{});
        {
            c.BeginUIBox(.{ .key = key, .main_axis_size = main_axis_size });
            {
                c.BeginUIBox(.{ .size = c.V2(20, 20) });
                c.EndUIBox();
            }
            c.EndUIBox();
        }
        c.EndUIBox();
        c.EndUILayer();
        c.EndUIFrame();

        const row = c.GetUIBox(key);
        if (main_axis_size == c.kUIMainAxisSizeMin) {
            try expectBoxSize(row, c.V2(20, 20));
        } else {
            try expectBoxSize(row, c.V2(100, 20));
        }
    }
}

// TODO: test min/max size

test "Layout, no fixed size, size around text" {
    c.BeginUIFrame(c.V2(100, 100));
    c.BeginUILayer();
    const key = c.PushUIKeyF("key");
    c.BeginUIBox(.{});
    {
        c.BeginUIBox(.{ .key = key, .text = c.PushUITextF("text") });
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();

    const text = c.GetUIBox(key);
    const text_metrics = c.GetTextMetricsStr8(c.STR8_LIT("Text"), c.kUIFontSizeDefault);
    try expectBoxSize(text, text_metrics.size);
}

test "Layout, fixed size, truncate text" {
    c.BeginUIFrame(c.V2(100, 100));
    const key = c.PushUIKeyF("key");
    c.BeginUILayer();
    c.BeginUIBox(.{});
    {
        c.BeginUIBox(.{ .key = key, .size = c.V2(2, 2), .text = c.PushUITextF("Text") });
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();

    const text = c.GetUIBox(key);
    try expectBoxSize(text, c.V2(2, 2));
}

test "Layout, row, no constraints on children" {
    c.BeginUIFrame(c.V2(1000, 100));
    const key0 = c.PushUIKeyF("key0");
    const key1 = c.PushUIKeyF("key1");
    c.BeginUILayer();
    c.BeginUIBox(.{});
    {
        c.BeginUIBox(.{ .key = key0, .text = c.PushUITextF("Hello!") });
        c.EndUIBox();

        c.BeginUIBox(.{ .key = key1, .text = c.PushUITextF("Goodbye!") });
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();

    const c0 = c.GetUIBox(key0);
    const c1 = c.GetUIBox(key1);
    const c0_text_size = c.GetTextMetricsStr8(c.STR8_LIT("Hello!"), c.kUIFontSizeDefault).size;
    const c1_text_size = c.GetTextMetricsStr8(c.STR8_LIT("Goodbye!"), c.kUIFontSizeDefault).size;

    try testing.expect(c0_text_size.x + c1_text_size.x < 1000);
    try expectBoxSize(c0, c0_text_size);
    try expectBoxRelPos(c0, c.V2(0, 0));
    try expectBoxSize(c1, c1_text_size);
    try expectBoxRelPos(c1, c.V2(c0_text_size.x, 0));
}

test "Layout, row, no constraints on children, but truncate" {
    c.BeginUIFrame(c.V2(100, 100));
    const key0 = c.PushUIKeyF("key0");
    const key1 = c.PushUIKeyF("key1");
    c.BeginUILayer();
    c.BeginUIBox(.{});
    {
        c.BeginUIBox(.{ .key = key0, .text = c.PushUITextF("Hello!") });
        c.EndUIBox();

        c.BeginUIBox(.{ .key = key1, .text = c.PushUITextF("Goodbye!") });
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();

    const c0 = c.GetUIBox(key0);
    const c1 = c.GetUIBox(key1);
    const c0_text_size = c.GetTextMetricsStr8(c.STR8_LIT("Hello!"), c.kUIFontSizeDefault).size;
    const c1_text_size = c.GetTextMetricsStr8(c.STR8_LIT("Goodbye!"), c.kUIFontSizeDefault).size;

    try testing.expect(c0_text_size.x + c1_text_size.x > 100);
    try expectBoxSize(c0, c0_text_size);
    try expectBoxRelPos(c0, c.V2(0, 0));
    try expectBoxSize(c1, c.V2(100 - c0_text_size.x, c1_text_size.y));
    try expectBoxRelPos(c1, c.V2(c0_text_size.x, 0));
}

test "Layout, row, constraint flex" {
    c.BeginUIFrame(c.V2(100, 100));
    const key0 = c.PushUIKeyF("key0");
    const key1 = c.PushUIKeyF("key1");
    c.BeginUILayer();
    c.BeginUIBox(.{});
    {
        c.BeginUIBox(.{
            .key = key0,
            .flex = 1,
            .text = c.PushUITextF("A very long text that doesn't fit in one line!"),
        });
        c.EndUIBox();

        c.BeginUIBox(.{ .key = key1, .text = c.PushUITextF("Goodbye!") });
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();

    const c0 = c.GetUIBox(key0);
    const c1 = c.GetUIBox(key1);
    const c0_text_size = c.GetTextMetricsStr8(c.STR8_LIT("A very long text that doesn't fit in one line!"), c.kUIFontSizeDefault).size;
    const c1_text_size = c.GetTextMetricsStr8(c.STR8_LIT("Goodbye!"), c.kUIFontSizeDefault).size;

    try testing.expect(c0_text_size.x > 100);
    try expectBoxSize(c0, c.V2(100 - c1_text_size.x, c0_text_size.y));
    try expectBoxRelPos(c0, c.V2(0, 0));
    try expectBoxSize(c1, c1_text_size);
    try expectBoxRelPos(c1, c.V2(100 - c1_text_size.x, 0));
}

test "Layout, main axis unbounded" {
    c.BeginUIFrame(c.V2(100, 100));
    const container_key = c.PushUIKeyF("container");
    const child_key = c.PushUIKeyF("child");
    c.BeginUILayer();
    c.BeginUIBox(.{});
    {
        c.BeginUIBox(.{ .key = container_key, .size = c.V2(c.kUISizeInfinity, c.kUISizeUndefined) });
        {
            c.BeginUIBox(.{ .key = child_key });
            {
                c.BeginUIBox(.{ .size = c.V2(100000, 10) });
                c.EndUIBox();

                c.BeginUIBox(.{ .size = c.V2(100000, 20) });
                c.EndUIBox();
            }
            c.EndUIBox();
        }
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();

    const container = c.GetUIBox(container_key);
    const child = c.GetUIBox(child_key);
    try expectBoxSize(container, c.V2(100, 20));
    try expectBoxSize(child, c.V2(200000, 20));
}

test "Layout, main axis unbounded, with unbounded content" {
    c.BeginUIFrame(c.V2(100, 100));
    c.BeginUILayer();
    c.BeginUIBox(.{});
    {
        c.BeginUIBox(.{ .size = c.V2(c.kUISizeInfinity, c.kUISizeUndefined) });
        {
            c.BeginUIBox(.{ .size = c.V2(c.kUISizeInfinity, c.kUISizeUndefined) });
            c.EndUIBox();
        }
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();

    try expectUIBuildError("Cannot have unbounded content within unbounded constraint");
}
