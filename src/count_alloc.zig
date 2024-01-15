const builtin = @import("builtin");
const std = @import("std");

const Allocator = std.mem.Allocator;

const thread_safe = !builtin.single_threaded;

const Count = if (thread_safe) std.atomic.Value(usize) else usize;
const zero: Count = if (thread_safe) std.atomic.Value(usize).init(0) else 0;

pub const CountAllocator = struct {
    underlying: Allocator,
    allocated_bytes: Count,
    peek_allocated_bytes: Count,

    pub fn init(underlying: Allocator) CountAllocator {
        return .{
            .underlying = underlying,
            .allocated_bytes = zero,
            .peek_allocated_bytes = zero,
        };
    }

    pub fn allocator(self: *CountAllocator) Allocator {
        return .{
            .ptr = self,
            .vtable = &.{
                .alloc = alloc,
                .resize = resize,
                .free = free,
            },
        };
    }

    pub fn get_peek_allocated_bytes(self: *const CountAllocator) usize {
        if (comptime thread_safe) {
            return self.peek_allocated_bytes.load(.SeqCst);
        } else {
            return self.peek_allocated_bytes;
        }
    }

    pub fn get_allocated_bytes(self: *const CountAllocator) usize {
        if (comptime thread_safe) {
            return self.allocated_bytes.load(.SeqCst);
        } else {
            return self.allocated_bytes;
        }
    }

    fn alloc(ctx: *anyopaque, len: usize, log2_ptr_align: u8, ret_addr: usize) ?[*]u8 {
        const self: *CountAllocator = @ptrCast(@alignCast(ctx));
        if (comptime thread_safe) {
            _ = self.allocated_bytes.fetchAdd(len, .SeqCst);
            _ = self.peek_allocated_bytes.fetchMax(self.allocated_bytes.load(.SeqCst), .SeqCst);
        } else {
            self.allocated_bytes += len;
            self.peek_allocated_bytes = @max(self.peek_allocated_bytes, self.allocated_bytes);
        }
        return self.underlying.rawAlloc(len, log2_ptr_align, ret_addr);
    }

    fn resize(ctx: *anyopaque, buf: []u8, buf_align: u8, new_len: usize, ret_addr: usize) bool {
        const self: *CountAllocator = @ptrCast(@alignCast(ctx));
        if (comptime thread_safe) {
            _ = self.allocated_bytes.fetchSub(buf.len, .SeqCst);
            _ = self.allocated_bytes.fetchAdd(new_len, .SeqCst);
            _ = self.peek_allocated_bytes.fetchMax(self.allocated_bytes.load(.SeqCst), .SeqCst);
        } else {
            self.allocated_bytes -= buf.len;
            self.allocated_bytes += new_len;
            self.peek_allocated_bytes = @max(self.peek_allocated_bytes, self.allocated_bytes);
        }
        return self.underlying.rawResize(buf, buf_align, new_len, ret_addr);
    }

    fn free(ctx: *anyopaque, buf: []u8, buf_align: u8, ret_addr: usize) void {
        const self: *CountAllocator = @ptrCast(@alignCast(ctx));
        if (comptime thread_safe) {
            _ = self.allocated_bytes.fetchSub(buf.len, .SeqCst);
        } else {
            self.allocated_bytes -= buf.len;
        }
        self.underlying.rawFree(buf, buf_align, ret_addr);
    }
};
