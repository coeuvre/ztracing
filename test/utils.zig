const std = @import("std");
const c = @import("c.zig");

const log = std.log;

pub fn expect_Vec2_IsEqual(expected: c.Vec2, actual: c.Vec2) !void {
    if (c.Vec2_IsEqual(expected, actual) == 0) {
        log.err("expected Vec2({d:.2}, {d:.2}), but got Vec2({d:.2}, {d:.2})", .{
            expected.x,
            expected.y,
            actual.x,
            actual.y,
        });
        return error.TestExpectedEqual;
    }
}

