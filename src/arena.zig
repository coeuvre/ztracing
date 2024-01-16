const std = @import("std");

const Allocator = std.mem.Allocator;

const Node = struct {
    prev: ?*Node,
    next: ?*Node,
    ptr_align: u8,
    total: usize,

    fn pad(alignment: usize) usize {
        return @rem(@sizeOf(Node), alignment);
    }

    pub fn format(self: *const Node, comptime _: []const u8, _: std.fmt.FormatOptions, writer: anytype) !void {
        try writer.print("{{ .prev = {?*}, .next = {?*}, .ptr_align = {}, .total = {} }}", .{
            self.prev,
            self.next,
            self.ptr_align,
            self.total,
        });
    }
};

/// A simple memory arena that delegate each alloc/resize/free to the `parent_allocator`.
/// It only records the allocations and can free them all at once.
pub const SimpleArena = struct {
    parent_allocator: Allocator,
    head: ?*Node,

    pub fn init(parent_allocator: Allocator) SimpleArena {
        return .{
            .parent_allocator = parent_allocator,
            .head = null,
        };
    }

    pub fn allocator(self: *SimpleArena) Allocator {
        return .{
            .ptr = self,
            .vtable = &.{
                .alloc = alloc,
                .resize = resize,
                .free = free,
            },
        };
    }

    pub fn deinit(self: SimpleArena) void {
        var maybe_node = self.head;
        while (maybe_node) |node| {
            const node_addr: [*]u8 = @ptrCast(node);
            const alignment = @as(usize, 1) << @as(Allocator.Log2Align, @intCast(node.ptr_align));
            const pad = Node.pad(alignment);
            const base = node_addr - pad;
            maybe_node = node.next;
            self.parent_allocator.rawFree(base[0..node.total], node.ptr_align, @returnAddress());
        }
    }

    fn alloc(ctx: *anyopaque, len: usize, ptr_align: u8, _: usize) ?[*]u8 {
        const self: *SimpleArena = @ptrCast(@alignCast(ctx));
        const alignment = @as(usize, 1) << @as(Allocator.Log2Align, @intCast(ptr_align));

        const pad = Node.pad(alignment);
        const total = pad + @sizeOf(Node) + len;

        const base = self.parent_allocator.rawAlloc(total, ptr_align, @returnAddress()) orelse return null;
        const node_addr = base + pad;
        const node: *Node = @ptrCast(@alignCast(node_addr));
        node.ptr_align = ptr_align;
        node.total = total;
        node.prev = null;
        if (self.head) |head| {
            node.next = head;
            head.prev = node;
        } else {
            node.next = null;
        }

        self.head = node;
        return @ptrCast(node_addr + @sizeOf(Node));
    }

    fn resize(ctx: *anyopaque, buf: []u8, buf_align: u8, new_len: usize, _: usize) bool {
        const self: *SimpleArena = @ptrCast(@alignCast(ctx));
        const alignment = @as(usize, 1) << @as(Allocator.Log2Align, @intCast(buf_align));

        const pad = Node.pad(alignment);
        const base = buf.ptr - @sizeOf(Node) - pad;
        const total = pad + @sizeOf(Node) + buf.len;
        const node: *Node = @ptrCast(@alignCast(buf.ptr - @sizeOf(Node)));
        std.debug.assert(node.ptr_align == buf_align);
        std.debug.assert(node.total == total);

        const new_total = pad + @sizeOf(Node) + new_len;
        if (self.parent_allocator.rawResize(base[0..total], buf_align, new_total, @returnAddress())) {
            node.total = new_total;
            return true;
        }

        return false;
    }

    fn free(ctx: *anyopaque, buf: []u8, buf_align: u8, _: usize) void {
        const self: *SimpleArena = @ptrCast(@alignCast(ctx));
        const alignment = @as(usize, 1) << @as(Allocator.Log2Align, @intCast(buf_align));

        const pad = Node.pad(alignment);
        const base = buf.ptr - @sizeOf(Node) - pad;
        const total = pad + @sizeOf(Node) + buf.len;
        const node: *Node = @ptrCast(@alignCast(buf.ptr - @sizeOf(Node)));
        std.debug.assert(node.ptr_align == buf_align);
        std.debug.assert(node.total == total);

        if (self.head == node) {
            self.head = node.next;
        }

        if (node.prev) |prev| {
            prev.next = node.next;
        }
        if (node.next) |next| {
            next.prev = node.prev;
        }

        self.parent_allocator.rawFree(base[0..total], buf_align, @returnAddress());
    }
};
