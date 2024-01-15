const std = @import("std");
const c = @import("c.zig");
const tracy = @import("tracy.zig");

const Allocator = std.mem.Allocator;

pub const Heap = struct {
    const Self = @This();

    raw: *c.mi_heap_t,

    pub fn new() Self {
        return .{
            .raw = c.mi_heap_new().?,
        };
    }

    pub fn from_raw(raw: *c.mi_heap_t) Self {
        return .{
            .raw = raw,
        };
    }

    pub fn delete(self: Self) void {
        c.mi_heap_delete(self.raw);
    }

    pub fn destroy(self: Self) void {
        c.mi_heap_destroy(self.raw);
    }

    pub fn allocator(self: Self) Allocator {
        return .{
            .ptr = self.raw,
            .vtable = &.{
                .alloc = alloc,
                .resize = resize,
                .free = free,
            },
        };
    }

    fn alloc(ctx: *anyopaque, len: usize, log2_ptr_align: u8, _: usize) ?[*]u8 {
        const raw: *c.mi_heap_t = @ptrCast(@alignCast(ctx));
        const alignment = @as(usize, 1) << @intCast(log2_ptr_align);
        if (c.mi_heap_malloc_aligned(raw, len, alignment)) |ptr| {
            return @ptrCast(ptr);
        } else {
            return null;
        }
    }

    fn resize(
        _: *anyopaque,
        old_mem: []u8,
        _: u8,
        new_size: usize,
        _: usize,
    ) bool {
        if (c.mi_expand(old_mem.ptr, new_size) != null) {
            return true;
        }
        return false;
    }

    fn free(
        _: *anyopaque,
        old_mem: []u8,
        log2_old_align_u8: u8,
        _: usize,
    ) void {
        const alignment = @as(usize, 1) << @intCast(log2_old_align_u8);
        c.mi_free_size_aligned(old_mem.ptr, old_mem.len, alignment);
    }
};

const GlobalAllocator = struct {
    pub fn allocator() Allocator {
        return .{
            .ptr = @constCast(&{}),
            .vtable = &.{
                .alloc = alloc,
                .resize = resize,
                .free = free,
            },
        };
    }
    fn alloc(_: *anyopaque, len: usize, log2_ptr_align: u8, _: usize) ?[*]u8 {
        const alignment = @as(usize, 1) << @intCast(log2_ptr_align);
        if (c.mi_malloc_aligned(len, alignment)) |ptr| {
            return @ptrCast(ptr);
        } else {
            return null;
        }
    }

    fn resize(
        _: *anyopaque,
        old_mem: []u8,
        _: u8,
        new_size: usize,
        _: usize,
    ) bool {
        if (c.mi_expand(old_mem.ptr, new_size) != null) {
            return true;
        }
        return false;
    }

    fn free(
        _: *anyopaque,
        old_mem: []u8,
        log2_old_align_u8: u8,
        _: usize,
    ) void {
        const alignment = @as(usize, 1) << @intCast(log2_old_align_u8);
        c.mi_free_size_aligned(old_mem.ptr, old_mem.len, alignment);
    }
};

pub const allocator = GlobalAllocator.allocator;
