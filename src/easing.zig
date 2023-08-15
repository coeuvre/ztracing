const std = @import("std");

pub fn easeOutQuint(x: anytype) @TypeOf(x) {
    return 1 - std.math.pow(@TypeOf(x), 1 - x, 5);
}

pub fn easeOutQuad(x: anytype) @TypeOf(x) {
    return 1 - (1 - x) * (1 - x);
}
