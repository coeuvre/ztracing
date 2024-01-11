const std = @import("std");
const builtin = @import("builtin");

pub usingnamespace @cImport({
    @cInclude("src/c.h");
});

export fn log_impl(level: i32, cstr: [*:0]const u8) void {
    switch (level) {
        0 => std.log.err("{s}", .{cstr}),
        1 => std.log.warn("{s}", .{cstr}),
        2 => std.log.info("{s}", .{cstr}),
        3 => std.log.debug("{s}", .{cstr}),
        else => unreachable,
    }
}
