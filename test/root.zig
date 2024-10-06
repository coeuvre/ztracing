const std = @import("std");

test {
    _ = @import("ui_test.zig");

    std.testing.refAllDeclsRecursive(@This());
}
