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

pub const memory = struct {
    const Allocator = std.mem.Allocator;

    const alignment = 8;
    const num_usize = alignment / @sizeOf(usize);

    pub fn malloc(allocator: Allocator, size: usize) ?*anyopaque {
        const total_size = @sizeOf(usize) * num_usize + size;
        const buf = allocator.alignedAlloc(u8, alignment, total_size) catch unreachable;
        var base: [*]usize = @ptrCast(buf.ptr);
        base[0] = total_size;
        return @ptrCast(base + num_usize);
    }

    pub fn free(allocator: Allocator, maybe_ptr: ?*anyopaque) void {
        if (maybe_ptr) |ptr| {
            const ptr_after_num_usize: [*]usize = @ptrCast(@alignCast(ptr));
            const base = ptr_after_num_usize - num_usize;
            const total_size = base[0];
            const raw: [*]align(alignment) u8 = @ptrCast(@alignCast(base));
            allocator.free(raw[0..total_size]);
        }
    }
};
