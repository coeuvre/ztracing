const std = @import("std");
const c = @cImport({
    @cInclude("src/draw.h");
    @cInclude("src/math.h");
    @cInclude("src/ui.h");
});

const testing = std.testing;
const log = std.log;

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

test "Root has the same size as the screen" {
    c.BeginUIFrame(c.V2(100, 100), 1);
    c.BeginUIBox(c.STR8_LIT("Root"));
    c.EndUIBox();
    c.EndUIFrame();

    const root = c.GetUIRoot();
    try expectBoxKey(root, "Root");
    try expectBoxSize(root, c.V2(100, 100));
}

test "Root has the same size as the screen, with fixed size" {
    const sizes: []const c.Vec2 = &.{
        c.V2(50, 50),
        c.V2(50, 200),
        c.V2(200, 50),
        c.V2(200, 200),
    };

    for (sizes) |size| {
        c.BeginUIFrame(c.V2(100, 100), 1);
        c.BeginUIBox(c.STR8_LIT("Root"));
        c.SetUISize(size);
        c.EndUIBox();
        c.EndUIFrame();

        const root = c.GetUIRoot();
        try expectBoxSize(root, c.V2(100, 100));
    }
}

test "Alignments" {
    const MainAxisOption = struct {
        @"align": c.UIMainAxisAlign,
        rel_pos: f32,
    };
    const CrossAxisOption = struct {
        @"align": c.UICrossAxisAlign,
        rel_pos: f32,
    };
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
            c.BeginUIFrame(c.V2(100, 100), 1);
            c.BeginUIBox(c.STR8_LIT("Root"));
            c.SetUIMainAxisAlign(main_axis_option.@"align");
            c.SetUICrossAxisAlign(cross_axis_option.@"align");
            {
                c.BeginUIBox(c.STR8_LIT("Container"));
                c.SetUISize(c.V2(50, 50));
                c.EndUIBox();
            }
            c.EndUIBox();
            c.EndUIFrame();

            const root = c.GetUIRoot();
            const container = c.GetUIChild(root, c.STR8_LIT("Container"));
            try expectBoxRelPos(container, c.V2(main_axis_option.rel_pos, cross_axis_option.rel_pos));
            try expectBoxSize(container, c.V2(50, 50));
        }
    }
}

test "No children, no fixed size, as small as possible" {
    c.BeginUIFrame(c.V2(100, 100), 1);
    c.BeginUIBox(c.STR8_LIT("Root"));
    {
        c.BeginUIBox(c.STR8_LIT("Container"));
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUIFrame();

    const root = c.GetUIRoot();
    const container = c.GetUIChild(root, c.STR8_LIT("Container"));
    try expectBoxSize(container, c.V2(0, 0));
}

test "No children, no fixed size, flex, as big as possible" {
    c.BeginUIFrame(c.V2(100, 100), 1);
    c.BeginUIBox(c.STR8_LIT("Root"));
    {
        c.BeginUIBox(c.STR8_LIT("Container"));
        c.SetUIFlex(1);
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUIFrame();

    const root = c.GetUIRoot();
    const container = c.GetUIChild(root, c.STR8_LIT("Container"));
    try expectBoxSize(container, c.V2(100, 100));
}

test "With one child, size around it" {
    c.BeginUIFrame(c.V2(100, 100), 1);
    c.BeginUIBox(c.STR8_LIT("Root"));
    {
        c.BeginUIBox(c.STR8_LIT("Container"));
        {
            c.BeginUIBox(c.STR8_LIT("Child"));
            c.SetUISize(c.V2(50, 50));
            c.EndUIBox();
        }
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUIFrame();

    const root = c.GetUIRoot();
    const container = c.GetUIChild(root, c.STR8_LIT("Container"));
    try expectBoxSize(container, c.V2(50, 50));
}

// TODO: test padding

// TODO: test min/max size

test "No fixed size, size around text" {
    c.BeginUIFrame(c.V2(100, 100), 1);
    c.BeginUIBox(c.STR8_LIT("Root"));
    {
        c.BeginUIBox(c.STR8_LIT("Text"));
        c.SetUIText(c.STR8_LIT("Text"));
        c.EndUIBox();
    }
    c.EndUIBox();
    c.EndUIFrame();

    const root = c.GetUIRoot();
    const text = c.GetUIChild(root, c.STR8_LIT("Text"));
    const text_metrics = c.GetTextMetricsStr8(c.STR8_LIT("Text"), c.KUITextSizeDefault);
    try expectBoxSize(text, text_metrics.size);
}

test "Fixed size, truncate text" {
    c.BeginUIFrame(c.V2(100, 100), 1);
    c.BeginUIBox(c.STR8_LIT("Root"));
    c.BeginUIBox(c.STR8_LIT("Text"));
    c.SetUISize(c.V2(2, 2));
    c.SetUIText(c.STR8_LIT("Text"));
    c.EndUIBox();
    c.EndUIBox();
    c.EndUIFrame();

    const root = c.GetUIRoot();
    const text = c.GetUIChild(root, c.STR8_LIT("Text"));
    try expectBoxSize(text, c.V2(2, 2));
}
