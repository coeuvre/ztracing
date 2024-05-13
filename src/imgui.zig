const std = @import("std");
const c = @import("c.zig");

const Allocator = std.mem.Allocator;

pub fn get_cursor_pos() c.ImVec2 {
    var cursor: c.ImVec2 = undefined;
    c.igGetCursorPos(&cursor);
    return cursor;
}

pub fn get_cursor_screen_pos() c.ImVec2 {
    var cursor: c.ImVec2 = undefined;
    c.igGetCursorScreenPos(&cursor);
    return cursor;
}

pub fn calc_text_size(text: []const u8, hide_text_after_double_hash: bool, wrap_width: f32) c.ImVec2 {
    var text_size: c.ImVec2 = undefined;
    c.igCalcTextSize(&text_size, text.ptr, text.ptr + text.len, hide_text_after_double_hash, wrap_width);
    return text_size;
}

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

pub fn alloc(size: usize, user_data: ?*anyopaque) callconv(.C) ?*anyopaque {
    const allocator: *Allocator = @ptrCast(@alignCast(user_data));
    return c.memory.malloc(allocator.*, size);
}

pub fn free(maybe_ptr: ?*anyopaque, user_data: ?*anyopaque) callconv(.C) void {
    const allocator: *Allocator = @ptrCast(@alignCast(user_data));
    c.memory.free(allocator.*, maybe_ptr);
}
