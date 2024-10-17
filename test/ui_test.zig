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

fn expectBoxSize(key: c.UIKey, expected_size: c.Vec2) !void {
    const box = c.GetUIBox(key);
    try expectEqualVec2(expected_size, box.*.computed.size);
}

fn expectBoxRelPos(key: c.UIKey, expected_rel_pos: c.Vec2) !void {
    const box = c.GetUIBox(key);
    try expectEqualVec2(expected_rel_pos, box.*.computed.rel_pos);
}

const State = extern struct {
    value: u32,
};

fn pushBoxState(key: c.UIKey) [*c]State {
    return @ptrCast(@alignCast(c.PushUIBoxState(key, "State", @sizeOf(State))));
}

test "State, return the same state across frame" {
    c.InitUI();
    defer c.QuitUI();

    c.BeginUIFrame();
    c.SetUICanvasSize(c.V2(100, 100));
    c.BeginUILayer(.{ .key = c.STR8_LIT("Layer") });
    _ = c.BeginUIBox(.{});
    {
        const box0 = c.BeginUIBox(.{});
        {
            const state = pushBoxState(box0);
            state.*.value = 10;
        }
        c.EndUIBox();

        const box1 = c.BeginUIBox(.{});
        {
            const state = pushBoxState(box1);
            state.*.value = 11;
        }
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();

    c.BeginUIFrame();
    c.SetUICanvasSize(c.V2(100, 100));
    c.BeginUILayer(.{ .key = c.STR8_LIT("Layer") });
    _ = c.BeginUIBox(.{});
    {
        const box0 = c.BeginUIBox(.{});
        {
            const state = pushBoxState(box0);
            try testing.expectEqual(10, state.*.value);
        }
        c.EndUIBox();

        const box1 = c.BeginUIBox(.{});
        {
            const state = pushBoxState(box1);
            try testing.expectEqual(11, state.*.value);
        }
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();
}

test "State, reset state if key is different" {
    c.InitUI();
    defer c.QuitUI();

    c.BeginUIFrame();
    c.SetUICanvasSize(c.V2(100, 100));
    c.BeginUILayer(.{ .key = c.STR8_LIT("Layer") });
    _ = c.BeginUIBox(.{});
    {
        const box0 = c.BeginUIBox(.{ .key = c.STR8_LIT("Key0") });
        {
            const state = pushBoxState(box0);
            state.*.value = 10;
        }
        c.EndUIBox();

        const box1 = c.BeginUIBox(.{});
        {
            const state = pushBoxState(box1);
            state.*.value = 11;
        }
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();

    c.BeginUIFrame();
    c.SetUICanvasSize(c.V2(100, 100));
    c.BeginUILayer(.{ .key = c.STR8_LIT("Layer") });
    _ = c.BeginUIBox(.{});
    {
        const box0 = c.BeginUIBox(.{ .key = c.STR8_LIT("Key1") });
        {
            const state = pushBoxState(box0);
            try testing.expectEqual(0, state.*.value);
        }
        c.EndUIBox();

        const box1 = c.BeginUIBox(.{});
        {
            const state = pushBoxState(box1);
            try testing.expectEqual(11, state.*.value);
        }
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();
}

test "State, reset state if tag is different" {
    c.InitUI();
    defer c.QuitUI();

    c.BeginUIFrame();
    c.SetUICanvasSize(c.V2(100, 100));
    c.BeginUILayer(.{ .key = c.STR8_LIT("Layer") });
    _ = c.BeginUIBox(.{});
    {
        const box0 = c.BeginUIBox(.{});
        {
            const state = pushBoxState(box0);
            state.*.value = 10;
        }
        c.EndUIBox();

        const box1 = c.BeginUIBox(.{});
        {
            const state = pushBoxState(box1);
            state.*.value = 11;
        }
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();

    c.BeginUIFrame();
    c.SetUICanvasSize(c.V2(100, 100));
    c.BeginUILayer(.{ .key = c.STR8_LIT("Layer") });
    _ = c.BeginUIBox(.{});
    {
        const box0 = c.BeginUITag("Tag", .{});
        {
            const state = pushBoxState(box0);
            try testing.expectEqual(0, state.*.value);
        }
        c.EndUITag("Tag");

        const box1 = c.BeginUIBox(.{});
        {
            const state = pushBoxState(box1);
            try testing.expectEqual(11, state.*.value);
        }
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();
}

test "State, reset state if box is not build from last frame" {
    c.InitUI();
    defer c.QuitUI();

    c.BeginUIFrame();
    c.SetUICanvasSize(c.V2(100, 100));
    c.BeginUILayer(.{ .key = c.STR8_LIT("Layer") });
    _ = c.BeginUIBox(.{});
    {
        const box0 = c.BeginUIBox(.{});
        {
            const state = pushBoxState(box0);
            state.*.value = 10;
        }
        c.EndUIBox();

        const box1 = c.BeginUIBox(.{});
        {
            const state = pushBoxState(box1);
            state.*.value = 11;
        }
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();

    c.BeginUIFrame();
    c.SetUICanvasSize(c.V2(100, 100));
    c.BeginUILayer(.{ .key = c.STR8_LIT("Layer") });
    _ = c.BeginUIBox(.{});
    {
        const box0 = c.BeginUIBox(.{});
        {
            const state = pushBoxState(box0);
            try testing.expectEqual(10, state.*.value);
        }
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();

    c.BeginUIFrame();
    c.SetUICanvasSize(c.V2(100, 100));
    c.BeginUILayer(.{ .key = c.STR8_LIT("Layer") });
    _ = c.BeginUIBox(.{});
    {
        const box0 = c.BeginUIBox(.{});
        {
            const state = pushBoxState(box0);
            try testing.expectEqual(10, state.*.value);
        }
        c.EndUIBox();

        const box1 = c.BeginUIBox(.{});
        {
            const state = pushBoxState(box1);
            try testing.expectEqual(0, state.*.value);
        }
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();
}

test "State, reset state if parent is different" {
    c.InitUI();
    defer c.QuitUI();

    c.BeginUIFrame();
    c.SetUICanvasSize(c.V2(100, 100));
    c.BeginUILayer(.{ .key = c.STR8_LIT("Layer") });
    _ = c.BeginUIBox(.{});
    {
        _ = c.BeginUIBox(.{});
        {
            const box1 = c.BeginUIBox(.{});
            {
                const state = pushBoxState(box1);
                state.*.value = 11;
            }
            c.EndUIBox();
        }
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();

    c.BeginUIFrame();
    c.SetUICanvasSize(c.V2(100, 100));
    c.BeginUILayer(.{ .key = c.STR8_LIT("Layer") });
    _ = c.BeginUIBox(.{});
    {
        _ = c.BeginUITag("Tag", .{});
        {
            const box1 = c.BeginUIBox(.{});
            {
                const state = pushBoxState(box1);
                try testing.expectEqual(0, state.*.value);
            }
            c.EndUIBox();
        }
        c.EndUITag("Tag");
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();
}

test "Layout, root has the same size as the screen" {
    c.InitUI();
    defer c.QuitUI();

    const sizes: []const c.Vec2 = &.{
        c.V2(c.kUISizeUndefined, c.kUISizeUndefined),
        c.V2(50, 50),
        c.V2(50, 200),
        c.V2(200, 50),
        c.V2(200, 200),
    };

    for (sizes) |size| {
        c.SetUICanvasSize(c.V2(100, 100));
        c.BeginUIFrame();
        c.BeginUILayer(.{ .key = c.STR8_LIT("Layer") });
        const root = c.BeginUIBox(.{ .size = size });
        c.EndUIBox();
        c.EndUILayer();
        c.EndUIFrame();

        try expectBoxSize(root, c.V2(100, 100));
    }
}

test "Layout, aligns" {
    c.InitUI();
    defer c.QuitUI();

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

    c.SetUICanvasSize(c.V2(100, 100));
    for (main_axis_options) |main_axis_option| {
        for (cross_axis_options) |cross_axis_option| {
            var container: c.UIKey = undefined;

            c.BeginUIFrame();
            c.BeginUILayer(.{ .key = c.STR8_LIT("Layer") });
            _ = c.BeginUIBox(.{
                .main_axis_align = main_axis_option.@"align",
                .cross_axis_align = cross_axis_option.@"align",
            });
            {
                container = c.BeginUIBox(.{ .size = c.V2(50, 50) });
                c.EndUIBox();
            }
            c.EndUIBox();
            c.EndUILayer();
            c.EndUIFrame();

            try expectBoxRelPos(container, c.V2(main_axis_option.rel_pos, cross_axis_option.rel_pos));
            try expectBoxSize(container, c.V2(50, 50));
        }
    }
}

test "Layout, padding" {
    c.InitUI();
    defer c.QuitUI();

    const padding = c.UIEdgeInsetsFromLTRB(1, 2, 3, 4);
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

    c.SetUICanvasSize(c.V2(100, 100));
    for (main_axis_options) |main_axis_option| {
        for (cross_axis_options) |cross_axis_option| {
            var container: c.UIKey = undefined;

            c.BeginUIFrame();
            c.BeginUILayer(.{ .key = c.STR8_LIT("Layer") });
            _ = c.BeginUIBox(.{
                .main_axis_align = main_axis_option.@"align",
                .cross_axis_align = cross_axis_option.@"align",
                .padding = padding,
            });
            {
                container = c.BeginUIBox(.{});
                {
                    _ = c.BeginUIBox(.{ .size = c.V2(50, 50) });
                    c.EndUIBox();
                }
                c.EndUIBox();
            }
            c.EndUIBox();
            c.EndUILayer();
            c.EndUIFrame();

            try expectBoxRelPos(container, c.V2(main_axis_option.rel_pos, cross_axis_option.rel_pos));
            try expectBoxSize(container, c.V2(50, 50));
        }
    }
}

test "Layout, no children, no fixed size, as small as possible" {
    c.InitUI();
    defer c.QuitUI();

    var container: c.UIKey = undefined;

    c.SetUICanvasSize(c.V2(100, 100));
    c.BeginUIFrame();
    c.BeginUILayer(.{ .key = c.STR8_LIT("Layer") });
    _ = c.BeginUIBox(.{});
    {
        container = c.BeginUIBox(.{});
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();

    try expectBoxSize(container, c.V2(0, 0));
}

test "Layout, no children, no fixed size, flex, main axis is as big as possible" {
    c.InitUI();
    defer c.QuitUI();

    var container: c.UIKey = undefined;

    c.SetUICanvasSize(c.V2(100, 100));
    c.BeginUIFrame();
    c.BeginUILayer(.{ .key = c.STR8_LIT("Layer") });
    _ = c.BeginUIBox(.{});
    {
        container = c.BeginUIBox(.{ .flex = 1 });
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();

    try expectBoxSize(container, c.V2(100, 0));
}

test "Layout, with one child, size around it" {
    c.InitUI();
    defer c.QuitUI();

    var container: c.UIKey = undefined;

    c.SetUICanvasSize(c.V2(100, 100));
    c.BeginUIFrame();
    c.BeginUILayer(.{ .key = c.STR8_LIT("Layer") });
    _ = c.BeginUIBox(.{});
    {
        _ = c.BeginUIBox(.{});
        {
            container = c.BeginUIBox(.{ .size = c.V2(50, 50) });
            c.EndUIBox();
        }
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();

    try expectBoxSize(container, c.V2(50, 50));
}

test "Layout, with fixed size" {
    c.InitUI();
    defer c.QuitUI();

    var container: c.UIKey = undefined;
    var child: c.UIKey = undefined;

    c.SetUICanvasSize(c.V2(100, 100));
    c.BeginUIFrame();
    c.BeginUILayer(.{ .key = c.STR8_LIT("Layer") });
    _ = c.BeginUIBox(.{});
    {
        container = c.BeginUIBox(.{ .size = c.V2(30, 20) });
        {
            child = c.BeginUIBox(.{ .size = c.V2(50, 40) });
            c.EndUIBox();
        }
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();

    try expectBoxSize(container, c.V2(30, 20));
    try expectBoxSize(child, c.V2(30, 20));
}

test "Layout, with fixed size, negative" {
    c.InitUI();
    defer c.QuitUI();

    var container: c.UIKey = undefined;
    var child: c.UIKey = undefined;

    c.SetUICanvasSize(c.V2(100, 100));
    c.BeginUIFrame();
    c.BeginUILayer(.{ .key = c.STR8_LIT("Layer") });
    _ = c.BeginUIBox(.{});
    {
        container = c.BeginUIBox(.{ .size = c.V2(30, -20) });
        {
            child = c.BeginUIBox(.{ .size = c.V2(50, 50) });
            c.EndUIBox();
        }
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();

    try expectBoxSize(container, c.V2(30, 0));
    try expectBoxSize(child, c.V2(30, 0));
}

test "Layout, main axis size, child has different main axis than parent" {
    c.InitUI();
    defer c.QuitUI();

    const main_axis_sizes: []const c.UIMainAxisSize = &.{
        c.kUIMainAxisSizeMin,
        c.kUIMainAxisSizeMax,
    };

    c.SetUICanvasSize(c.V2(100, 100));
    for (main_axis_sizes) |main_axis_size| {
        var row: c.UIKey = undefined;

        c.BeginUIFrame();
        c.BeginUILayer(.{ .key = c.STR8_LIT("Layer") });
        _ = c.BeginUIBox(.{ .main_axis = c.kAxis2Y });
        {
            row = c.BeginUIBox(.{ .main_axis_size = main_axis_size });
            {
                _ = c.BeginUIBox(.{ .size = c.V2(30, 20) });
                c.EndUIBox();
            }
            c.EndUIBox();
        }
        c.EndUIBox();
        c.EndUILayer();
        c.EndUIFrame();

        if (main_axis_size == c.kUIMainAxisSizeMin) {
            try expectBoxSize(row, c.V2(30, 20));
        } else {
            try expectBoxSize(row, c.V2(100, 20));
        }
    }
}

test "Layout, main axis size, child has same main axis as parent" {
    c.InitUI();
    defer c.QuitUI();

    const main_axis_sizes: []const c.UIMainAxisSize = &.{
        c.kUIMainAxisSizeMin,
        c.kUIMainAxisSizeMax,
    };

    c.SetUICanvasSize(c.V2(100, 100));
    for (main_axis_sizes) |main_axis_size| {
        var row: c.UIKey = undefined;

        c.BeginUIFrame();
        c.BeginUILayer(.{ .key = c.STR8_LIT("Layer") });
        _ = c.BeginUIBox(.{});
        {
            row = c.BeginUIBox(.{ .main_axis_size = main_axis_size });
            {
                _ = c.BeginUIBox(.{ .size = c.V2(20, 20) });
                c.EndUIBox();
            }
            c.EndUIBox();
        }
        c.EndUIBox();
        c.EndUILayer();
        c.EndUIFrame();

        if (main_axis_size == c.kUIMainAxisSizeMin) {
            try expectBoxSize(row, c.V2(20, 20));
        } else {
            try expectBoxSize(row, c.V2(100, 20));
        }
    }
}

// TODO: test min/max size

test "Layout, no fixed size, size around text" {
    c.InitUI();
    defer c.QuitUI();

    var text: c.UIKey = undefined;

    c.SetUICanvasSize(c.V2(100, 100));
    c.BeginUIFrame();
    c.BeginUILayer(.{ .key = c.STR8_LIT("Layer") });
    _ = c.BeginUIBox(.{});
    {
        text = c.BeginUIBox(.{ .text = c.PushUIStr8F("text") });
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();

    const text_metrics = c.GetTextMetricsStr8(c.STR8_LIT("Text"), c.kUIFontSizeDefault);
    try expectBoxSize(text, text_metrics.size);
}

test "Layout, fixed size, truncate text" {
    c.InitUI();
    defer c.QuitUI();

    var text: c.UIKey = undefined;

    c.SetUICanvasSize(c.V2(100, 100));
    c.BeginUIFrame();
    c.BeginUILayer(.{ .key = c.STR8_LIT("Layer") });
    _ = c.BeginUIBox(.{});
    {
        text = c.BeginUIBox(.{ .size = c.V2(2, 2), .text = c.PushUIStr8F("Text") });
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();

    try expectBoxSize(text, c.V2(2, 2));
}

test "Layout, row, no constraints on children" {
    c.InitUI();
    defer c.QuitUI();

    var c0: c.UIKey = undefined;
    var c1: c.UIKey = undefined;

    c.SetUICanvasSize(c.V2(1000, 100));
    c.BeginUIFrame();
    c.BeginUILayer(.{ .key = c.STR8_LIT("Layer") });
    _ = c.BeginUIBox(.{});
    {
        c0 = c.BeginUIBox(.{ .text = c.PushUIStr8F("Hello!") });
        c.EndUIBox();

        c1 = c.BeginUIBox(.{ .text = c.PushUIStr8F("Goodbye!") });
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();

    const c0_text_size = c.GetTextMetricsStr8(c.STR8_LIT("Hello!"), c.kUIFontSizeDefault).size;
    const c1_text_size = c.GetTextMetricsStr8(c.STR8_LIT("Goodbye!"), c.kUIFontSizeDefault).size;

    try testing.expect(c0_text_size.x + c1_text_size.x < 1000);
    try expectBoxSize(c0, c0_text_size);
    try expectBoxRelPos(c0, c.V2(0, 0));
    try expectBoxSize(c1, c1_text_size);
    try expectBoxRelPos(c1, c.V2(c0_text_size.x, 0));
}

test "Layout, row, no constraints on children, but truncate" {
    c.InitUI();
    defer c.QuitUI();

    var c0: c.UIKey = undefined;
    var c1: c.UIKey = undefined;

    c.SetUICanvasSize(c.V2(100, 100));
    c.BeginUIFrame();
    c.BeginUILayer(.{ .key = c.STR8_LIT("Layer") });
    _ = c.BeginUIBox(.{});
    {
        c0 = c.BeginUIBox(.{ .text = c.PushUIStr8F("Hello!") });
        c.EndUIBox();

        c1 = c.BeginUIBox(.{ .text = c.PushUIStr8F("Goodbye!") });
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();

    const c0_text_size = c.GetTextMetricsStr8(c.STR8_LIT("Hello!"), c.kUIFontSizeDefault).size;
    const c1_text_size = c.GetTextMetricsStr8(c.STR8_LIT("Goodbye!"), c.kUIFontSizeDefault).size;

    try testing.expect(c0_text_size.x + c1_text_size.x > 100);
    try expectBoxSize(c0, c0_text_size);
    try expectBoxRelPos(c0, c.V2(0, 0));
    try expectBoxSize(c1, c.V2(100 - c0_text_size.x, c1_text_size.y));
    try expectBoxRelPos(c1, c.V2(c0_text_size.x, 0));
}

test "Layout, row, constraint flex" {
    c.InitUI();
    defer c.QuitUI();

    var c0: c.UIKey = undefined;
    var c1: c.UIKey = undefined;

    c.SetUICanvasSize(c.V2(100, 100));
    c.BeginUIFrame();
    c.BeginUILayer(.{ .key = c.STR8_LIT("Layer") });
    _ = c.BeginUIBox(.{});
    {
        c0 = c.BeginUIBox(.{
            .flex = 1,
            .text = c.PushUIStr8F("A very long text that doesn't fit in one line!"),
        });
        c.EndUIBox();

        c1 = c.BeginUIBox(.{ .text = c.PushUIStr8F("Goodbye!") });
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();

    const c0_text_size = c.GetTextMetricsStr8(c.STR8_LIT("A very long text that doesn't fit in one line!"), c.kUIFontSizeDefault).size;
    const c1_text_size = c.GetTextMetricsStr8(c.STR8_LIT("Goodbye!"), c.kUIFontSizeDefault).size;

    try testing.expect(c0_text_size.x > 100);
    try expectBoxSize(c0, c.V2(100 - c1_text_size.x, c0_text_size.y));
    try expectBoxRelPos(c0, c.V2(0, 0));
    try expectBoxSize(c1, c1_text_size);
    try expectBoxRelPos(c1, c.V2(100 - c1_text_size.x, 0));
}

test "Layout, main axis unbounded" {
    c.InitUI();
    defer c.QuitUI();

    var container: c.UIKey = undefined;
    var child: c.UIKey = undefined;

    c.SetUICanvasSize(c.V2(100, 100));
    c.BeginUIFrame();
    c.BeginUILayer(.{ .key = c.STR8_LIT("Layer") });
    _ = c.BeginUIBox(.{});
    {
        container = c.BeginUIBox(.{ .size = c.V2(c.kUISizeInfinity, c.kUISizeUndefined) });
        {
            child = c.BeginUIBox(.{});
            {
                _ = c.BeginUIBox(.{ .size = c.V2(100000, 10) });
                c.EndUIBox();

                _ = c.BeginUIBox(.{ .size = c.V2(100000, 20) });
                c.EndUIBox();
            }
            c.EndUIBox();
        }
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();

    try expectBoxSize(container, c.V2(100, 20));
    try expectBoxSize(child, c.V2(200000, 20));
}

test "Layout, main axis unbounded, with unbounded content" {
    c.InitUI();
    defer c.QuitUI();

    c.SetUICanvasSize(c.V2(100, 100));
    c.BeginUIFrame();
    c.BeginUILayer(.{ .key = c.STR8_LIT("Layer") });
    _ = c.BeginUIBox(.{});
    {
        _ = c.BeginUIBox(.{ .size = c.V2(c.kUISizeInfinity, c.kUISizeUndefined) });
        {
            _ = c.BeginUIBox(.{ .size = c.V2(c.kUISizeInfinity, c.kUISizeUndefined) });
            c.EndUIBox();
        }
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUILayer();
    c.EndUIFrame();

    try expectUIBuildError("Cannot have unbounded content within unbounded constraint");
}
