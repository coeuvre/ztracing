const std = @import("std");
const builtin = @import("builtin");
const build_options = @import("build_options");

pub const enable = if (builtin.is_test) false else build_options.enable_tracy;
pub const enable_allocation = enable and build_options.enable_tracy_allocation;
pub const enable_callstack = enable and build_options.enable_tracy_callstack;

pub inline fn set_thread_name(comptime name: [:0]const u8) void {
    if (!enable) return;
    ___tracy_set_thread_name(name.ptr);
}

extern fn ___tracy_set_thread_name(name: ?[*:0]const u8) void;
