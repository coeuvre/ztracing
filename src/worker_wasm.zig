const std = @import("std");

const SharedState = @import("shared.zig").SharedState;
const Allocator = std.mem.Allocator;

const Worker = struct {
    allocator: Allocator,
};

export fn init(shared_state_ptr: *void) *void {
    const shared_state: *SharedState = @ptrCast(@alignCast(shared_state_ptr));
    var allocator = shared_state.count_allocator.allocator();
    var worker = allocator.create(Worker) catch unreachable;
    worker.allocator = allocator;
    return @ptrCast(worker);
}
