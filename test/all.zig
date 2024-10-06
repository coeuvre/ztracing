const std = @import("std");
const c = @cImport({
    @cInclude("src/math.h");
    @cInclude("src/ui.h");
    @cInclude("src/ui_widgets.h");
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

fn expectBoxKey(box: [*c]c.UIBox, expected_key: []const u8) !void {
    const actual_key = box.*.build.key_str.ptr[0..box.*.build.key_str.len];
    try testing.expectEqualStrings(expected_key, actual_key);
}

fn expectBoxSize(box: [*c]c.UIBox, expected_size: c.Vec2) !void {
    try expectEqualVec2(expected_size, box.*.computed.size);
}

test "ui" {
    const screen_size = c.V2(100, 100);
    c.UIBeginFrame(screen_size, 1);
    c.UIBeginBox(c.STR8_LIT("Root"));
    c.UIEndBox();
    c.UIEndFrame();

    const root = c.GetUIRoot();
    try expectBoxKey(root, "Root");
    try expectBoxSize(root, screen_size);
}
