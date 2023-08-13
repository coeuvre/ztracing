const std = @import("std");

pub fn expectOptional(expected: anytype, actual: anytype) !bool {
    switch (@typeInfo(@TypeOf(expected))) {
        .Optional => {
            switch (@typeInfo(@TypeOf(actual))) {
                .Optional => {
                    if (expected) |_| {
                        if (actual) |_| {
                            return true;
                        } else {
                            std.debug.print("expected not null, found null\n", .{});
                            return error.TestExpectedEqual;
                        }
                    } else {
                        if (actual) |_| {
                            std.debug.print("expected null, found not null\n", .{});
                            return error.TestExpectedEqual;
                        } else {
                            return false;
                        }
                    }
                },
                else => @compileError("value of type " ++ @typeName(@TypeOf(actual)) ++ " encountered"),
            }
        },
        else => @compileError("value of type " ++ @typeName(@TypeOf(expected)) ++ " encountered"),
    }
}
