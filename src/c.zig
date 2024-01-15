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
    const header_size = @sizeOf(usize) * num_usize;

    pub fn malloc(allocator: Allocator, size: usize) ?*anyopaque {
        const total_size = header_size + size;
        const buf = allocator.alignedAlloc(u8, alignment, total_size) catch unreachable;
        var base: [*]usize = @ptrCast(buf.ptr);
        base[0] = total_size;
        return @ptrCast(base + num_usize);
    }

    pub fn calloc(allocator: Allocator, count: usize, size: usize) ?*anyopaque {
        const total_size = count * size;
        const maybe_ptr = malloc(allocator, total_size);
        if (maybe_ptr) |ptr| {
            const bytes: [*]u8 = @ptrCast(ptr);
            @memset(bytes[0..total_size], 0);
        }
        return maybe_ptr;
    }

    pub fn realloc(allocator: Allocator, maybe_ptr: ?*anyopaque, new_size: usize) ?*anyopaque {
        if (maybe_ptr) |ptr| {
            const slice = raw_to_slice(ptr);
            const new_total_size = header_size + new_size;
            if (allocator.resize(slice, new_total_size)) {
                const base: [*]usize = @ptrCast(@alignCast(slice.ptr));
                base[0] = new_total_size;
                return ptr;
            }
            if (malloc(allocator, new_size)) |new_ptr| {
                const new_slice_no_header: []u8 = @as([*]u8, @ptrCast(new_ptr))[0..new_size];
                @memcpy(new_slice_no_header[0 .. slice.len - header_size], slice[header_size..]);
                free(allocator, ptr);
                return new_ptr;
            }
            return null;
        } else {
            return malloc(allocator, new_size);
        }
    }

    pub fn free(allocator: Allocator, maybe_ptr: ?*anyopaque) void {
        if (maybe_ptr) |ptr| {
            allocator.free(raw_to_slice(ptr));
        }
    }

    fn raw_to_slice(ptr: *anyopaque) []align(alignment) u8 {
        const ptr_after_num_usize: [*]usize = @ptrCast(@alignCast(ptr));
        const base = ptr_after_num_usize - num_usize;
        const total_size = base[0];
        const slice: [*]align(alignment) u8 = @ptrCast(@alignCast(base));
        return slice[0..total_size];
    }
};
