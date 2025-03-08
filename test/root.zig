const std = @import("std");

test {
    _ = @import("ui_pointer_listener_test.zig");
    _ = @import("ui_test.zig");

    std.testing.refAllDeclsRecursive(@This());
}
