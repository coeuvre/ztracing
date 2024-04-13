const builtin = @import("builtin");
const std = @import("std");

const Allocator = std.mem.Allocator;

pub fn MessageQueue(Message: type) type {
    return struct {
        const Self = @This();

        mutex: Mutex = .{},
        condition: Condition = .{},
        queue: std.ArrayList(Message),

        pub fn init(allocator: Allocator) Self {
            return .{
                .queue = std.ArrayList(Message).init(allocator),
            };
        }

        pub fn get(self: *Self) Message {
            self.mutex.lock();
            defer self.mutex.unlock();

            while (self.queue.items.len == 0) {
                self.condition.wait(&self.mutex);
            }

            const message = self.queue.pop();

            if (self.queue.items.len > 0) {
                self.condition.signal();
            }

            return message;
        }

        pub fn try_get(self: *Self) ?Message {
            self.mutex.lock();
            defer self.mutex.unlock();

            if (self.queue.items.len == 0) {
                return null;
            }

            const message = self.queue.pop();
            if (self.queue.items.len > 0) {
                self.condition.signal();
            }
            return message;
        }

        pub fn put(self: *Self, message: Message) void {
            self.mutex.lock();
            defer self.mutex.unlock();

            self.queue.append(message) catch unreachable;
            self.condition.signal();
        }
    };
}

const Mutex = struct {
    const Self = @This();

    impl: std.Thread.Mutex = .{},

    pub fn lock(self: *Self) void {
        if (comptime builtin.os.tag == .wasi) {
            if (std.Thread.getCurrentId() == 0) {
                // wasm doesn't support atomic.wait on main thread, so we need to busy wait.
                while (!self.impl.tryLock()) {}
                return;
            }
        }

        self.impl.lock();
    }

    pub fn unlock(self: *Self) void {
        self.impl.unlock();
    }
};

const Condition = struct {
    const Self = @This();

    impl: std.Thread.Condition = .{},

    pub fn wait(self: *Self, mutex: *Mutex) void {
        if (comptime builtin.os.tag == .wasi) {
            // singal might be lost due to compiler bug, so we need to use timedWait.
            self.impl.timedWait(&mutex.impl, 10e6) catch |err| {
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
