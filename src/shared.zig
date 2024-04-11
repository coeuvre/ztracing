const std = @import("std");
const CountAllocator = @import("./count_alloc.zig").CountAllocator;

pub const SharedState = struct {
    gpa: std.heap.GeneralPurposeAllocator(.{}),
    count_allocator: CountAllocator,
};
