const c = @import("c.zig");
const Allocator = @import("std").mem.Allocator;

pub fn getWindowPos() c.ImVec2 {
    var result: c.ImVec2 = undefined;
    c.igGetWindowPos(&result);
    return result;
}

pub fn getWindowContentRegionMin() c.ImVec2 {
    var result: c.ImVec2 = undefined;
    c.igGetWindowContentRegionMin(&result);
    return result;
}

pub fn getWindowContentRegionMax() c.ImVec2 {
    var result: c.ImVec2 = undefined;
    c.igGetWindowContentRegionMax(&result);
    return result;
}

pub fn getMousePos() c.ImVec2 {
    var result: c.ImVec2 = undefined;
    c.igGetMousePos(&result);
    return result;
}

const alignment = 8;
const num_usize = alignment / @sizeOf(usize);

pub fn alloc(size: usize, user_data: ?*anyopaque) callconv(.C) *anyopaque {
    const allocator: *Allocator = @ptrCast(@alignCast(user_data));
    const total_size = @sizeOf(usize) * num_usize + size;
    const buf = allocator.alignedAlloc(u8, alignment, total_size) catch unreachable;
    var ptr: [*]usize = @ptrCast(buf.ptr);
    ptr[0] = total_size;
    return @ptrCast(ptr + num_usize);
}

pub fn free(maybe_ptr: ?*anyopaque, user_data: ?*anyopaque) callconv(.C) void {
    if (maybe_ptr) |ptr| {
        const allocator: *Allocator = @ptrCast(@alignCast(user_data));
        const ptr_after_num_usize: [*]usize = @ptrCast(@alignCast(ptr));
        const base = ptr_after_num_usize - num_usize;
        const total_size = base[0];

        const raw: [*]align(alignment) u8 = @ptrCast(@alignCast(base));
        allocator.free(raw[0..total_size]);
    }
}
