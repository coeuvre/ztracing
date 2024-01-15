const std = @import("std");
const Allocator = std.mem.Allocator;

pub const Arena = struct {
    ptr: *anyopaque,
    vtable: *const VTable,

    pub const VTable = struct {
        allocator: *const fn (ctx: *anyopaque) Allocator,
        deinit: *const fn (ctx: *anyopaque) void,
    };

    pub inline fn allocator(self: Arena) Allocator {
        return self.vtable.allocator(self.ptr);
    }

    pub inline fn deinit(self: Arena) void {
        self.vtable.deinit(self.ptr);
    }
};

pub const StdArena = struct {
    pub fn init(child_allocator: Allocator) !Arena {
        const arena: *std.heap.ArenaAllocator = try child_allocator.create(std.heap.ArenaAllocator);
        arena.* = std.heap.ArenaAllocator.init(child_allocator);
        return .{
            .ptr = arena,
            .vtable = &.{
                .allocator = StdArena.allocator,
                .deinit = StdArena.deinit,
            },
        };
    }

    fn allocator(p: *anyopaque) Allocator {
        var arena: *std.heap.ArenaAllocator = @ptrCast(@alignCast(p));
        return arena.allocator();
    }

    fn deinit(p: *anyopaque) void {
        var arena: *std.heap.ArenaAllocator = @ptrCast(@alignCast(p));
        arena.deinit();
        arena.child_allocator.destroy(arena);
    }
};
