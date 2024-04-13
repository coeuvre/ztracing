const builtin = @import("builtin");
const std = @import("std");

const Allocator = std.mem.Allocator;
const Arena = std.heap.ArenaAllocator;

pub fn MessageQueue(Message: type) type {
    return struct {
        const Self = @This();
        const Queue = std.DoublyLinkedList(Message);

        mutex: Mutex = .{},
        condition: Condition = .{},
        arena: Arena,
        queue: Queue = .{},
        free_nodes: Queue = .{},

        pub fn init(allocator: Allocator) Self {
            return .{
                .arena = Arena.init(allocator),
            };
        }

        pub fn deinit(self: *Self) void {
            self.mutex.lock();
            defer self.mutex.unlock();

            self.arena.deinit();
        }

        pub fn get(self: *Self) Message {
            self.mutex.lock();
            defer self.mutex.unlock();

            while (self.queue.len == 0) {
                self.condition.wait(&self.mutex);
            }

            return self.pop_first();
        }

        // the lock must be held and there is at least one node in the queue
        fn pop_first(self: *Self) Message {
            const node = self.queue.popFirst().?;
            self.free_nodes.append(node);

            if (self.queue.len > 0) {
                self.condition.signal();
            }

            return node.data;
        }

        pub fn try_get(self: *Self) ?Message {
            self.mutex.lock();
            defer self.mutex.unlock();

            if (self.queue.items.len == 0) {
                return null;
            }

            return self.pop_first();
        }

        pub fn put(self: *Self, message: Message) void {
            self.mutex.lock();
            defer self.mutex.unlock();

            const node = self.free_nodes.pop() orelse
                self.arena.allocator().create(Queue.Node) catch unreachable;
            node.data = message;
            self.queue.append(node);
            self.condition.signal();
        }
    };
}

pub const Mutex = struct {
    const Self = @This();

    impl: std.Thread.Mutex = .{},

    pub fn lock(self: *Self) void {
        if (comptime builtin.os.tag == .wasi) {
            // We need to busy wait because:
            // 1. wasm doesn't support atomic.wait on main thread
            // 2. singal might be lost due to compiler bug
            while (!self.impl.tryLock()) {}
            return;
        } else {
            self.impl.lock();
        }
    }

    pub fn unlock(self: *Self) void {
        self.impl.unlock();
    }
};

pub const Condition = struct {
    const Self = @This();

    impl: std.Thread.Condition = .{},

    pub fn wait(self: *Self, mutex: *Mutex) void {
        if (comptime builtin.os.tag == .wasi) {
            // singal might be lost due to compiler bug, so we need to use timedWait.
            self.impl.timedWait(&mutex.impl, 1e6) catch |err| {
                switch (err) {
                    error.Timeout => {},
                }
            };
        } else {
            self.impl.wait(&mutex.impl);
        }
    }

    pub fn signal(self: *Self) void {
        self.impl.signal();
    }
};
