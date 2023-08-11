const Allocator = @import("std").mem.Allocator;

pub const CountAllocator = struct {
    underlying: Allocator,
    allocated_bytes: usize,
    peek_allocated_bytes: usize,

    pub fn init(underlying: Allocator) CountAllocator {
        return .{
            .underlying = underlying,
            .allocated_bytes = 0,
            .peek_allocated_bytes = 0,
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

    pub fn peekAllocatedBytes(self: *const CountAllocator) usize {
        return self.peek_allocated_bytes;
    }

    pub fn allocatedBytes(self: *const CountAllocator) usize {
        return self.allocated_bytes;
    }

    fn alloc(ctx: *anyopaque, len: usize, log2_ptr_align: u8, ret_addr: usize) ?[*]u8 {
        const self: *CountAllocator = @ptrCast(@alignCast(ctx));
        self.allocated_bytes += len;
        self.peek_allocated_bytes = @max(self.peek_allocated_bytes, self.allocated_bytes);
        return self.underlying.rawAlloc(len, log2_ptr_align, ret_addr);
    }

    fn resize(ctx: *anyopaque, buf: []u8, buf_align: u8, new_len: usize, ret_addr: usize) bool {
        const self: *CountAllocator = @ptrCast(@alignCast(ctx));
        self.allocated_bytes -= buf.len;
        self.allocated_bytes += new_len;
        self.peek_allocated_bytes = @max(self.peek_allocated_bytes, self.allocated_bytes);
        return self.underlying.rawResize(buf, buf_align, new_len, ret_addr);
    }

    fn free(ctx: *anyopaque, buf: []u8, buf_align: u8, ret_addr: usize) void {
        const self: *CountAllocator = @ptrCast(@alignCast(ctx));
        self.allocated_bytes -= buf.len;
        self.underlying.rawFree(buf, buf_align, ret_addr);
    }
};
