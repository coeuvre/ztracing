const std = @import("std");

const Allocator = std.mem.Allocator;

pub const ProfileCounterValue = struct {
    name: []u8,
    value: f64,
};

pub const ProfileCounter = struct {
    time_us: i64,
    values: std.ArrayList(ProfileCounterValue),
};

const ProfileCounterLane = struct {
    name: []u8,
    max_value: f64,
    counters: std.ArrayList(ProfileCounter),
    num_series: usize,

    fn init(allocator: Allocator, name: []const u8) ProfileCounterLane {
        return .{
            .name = allocator.dupe(u8, name) catch unreachable,
            .max_value = 0,
            .counters = std.ArrayList(ProfileCounter).init(allocator),
            .num_series = 0,
        };
    }

    pub fn addCounter(self: *ProfileCounterLane, time_us: i64, values: std.ArrayList(ProfileCounterValue)) !void {
        self.num_series = @max(self.num_series, values.items.len);
        try self.counters.append(.{ .time_us = time_us, .values = values });
        var sum: f64 = 0;
        for (values.items) |value| {
            sum += value.value;
        }
        self.max_value = @max(self.max_value, sum);
    }

    pub fn done(self: *ProfileCounterLane) void {
        std.sort.block(ProfileCounter, self.counters.items, {}, profileCounterLessThan);
    }

    fn profileCounterLessThan(_: void, lhs: ProfileCounter, rhs: ProfileCounter) bool {
        return lhs.time_us < rhs.time_us;
    }

    pub fn iter(self: *const ProfileCounterLane, start_time_us: i64, min_duration_us: i64) ProfileCounterIter {
        const counters = self.counters.items;
        var index = counters.len;
        // TODO: Optimize with binary search
        for (counters, 0..) |counter, i| {
            if (counter.time_us > start_time_us) {
                index = if (i > 0) i - 1 else 0;
                break;
            }
        }
        return .{
            .counters = counters,
            .index = index,
            .min_duration_us = min_duration_us,
        };
    }
};

const ProfileCounterIter = struct {
    counters: []const ProfileCounter,
    index: usize,
    min_duration_us: i64,

    pub fn next(self: *ProfileCounterIter) ?ProfileCounter {
        if (self.index >= self.counters.len) {
            return null;
        }
        const counter_to_return = self.counters[self.index];
        var has_next = false;
        const start_time_us = counter_to_return.time_us;
        for (self.counters[self.index..], self.index..) |counter, i| {
            if (counter.time_us - start_time_us >= self.min_duration_us) {
                self.index = i;
                has_next = true;
                break;
            }
        }

        if (!has_next) {
            self.index = self.counters.len;
        }
        return counter_to_return;
    }
};

const Span = struct {
    name: []u8,
    start_time_us: i64,
    duration_us: i64,
};

const ThreadLane = struct {
    allocator: Allocator,
    tid: i64,
    spans: std.ArrayList(Span),

    name: ?[]u8 = null,

    pub fn init(allocator: Allocator, tid: i64) ThreadLane {
        return .{
            .allocator = allocator,
            .tid = tid,
            .spans = std.ArrayList(Span).init(allocator),
        };
    }

    pub fn setName(self: *ThreadLane, name: []const u8) !void {
        if (self.name) |n| {
            self.allocator.free(n);
        }
        self.name = try self.allocator.dupe(u8, name);
    }

    pub fn addSpan(self: *ThreadLane, name: []const u8, start_time_us: i64, duration_us: i64) !void {
        try self.spans.append(.{
            .name = try self.allocator.dupe(u8, name),
            .start_time_us = start_time_us,
            .duration_us = duration_us,
        });
    }

    pub fn done(self: *ThreadLane) void {
        std.sort.block(Span, self.spans.items, {}, spanLessThan);
    }

    fn spanLessThan(_: void, lhs: Span, rhs: Span) bool {
        return lhs.start_time_us < rhs.start_time_us;
    }
};

pub const Profile = struct {
    allocator: Allocator,
    min_time_us: i64,
    max_time_us: i64,
    counter_lanes: std.ArrayList(ProfileCounterLane),
    thread_lanes: std.ArrayList(ThreadLane),

    pub fn init(allocator: Allocator) Profile {
        return .{
            .allocator = allocator,
            .min_time_us = 0,
            .max_time_us = 0,
            .counter_lanes = std.ArrayList(ProfileCounterLane).init(allocator),
            .thread_lanes = std.ArrayList(ThreadLane).init(allocator),
        };
    }

    pub fn getOrCreateCounterLane(self: *Profile, name: []const u8) !*ProfileCounterLane {
        for (self.counter_lanes.items) |*counter_lane| {
            if (std.mem.eql(u8, counter_lane.name, name)) {
                return counter_lane;
            }
        }

        try self.counter_lanes.append(ProfileCounterLane.init(self.allocator, name));
        return &self.counter_lanes.items[self.counter_lanes.items.len - 1];
    }

    pub fn getOrCreateThreadLane(self: *Profile, tid: i64) !*ThreadLane {
        for (self.thread_lanes.items) |*thread_lane| {
            if (thread_lane.tid == tid) {
                return thread_lane;
            }
        }

        try self.thread_lanes.append(ThreadLane.init(self.allocator, tid));
        return &self.thread_lanes.items[self.thread_lanes.items.len - 1];
    }

    pub fn done(self: *Profile) void {
        std.sort.block(ProfileCounterLane, self.counter_lanes.items, {}, profileCounterLaneLessThan);

        for (self.counter_lanes.items) |*counter_lane| {
            counter_lane.done();
        }

        for (self.thread_lanes.items) |*thread_lane| {
            thread_lane.done();
        }
    }

    fn toUpperCase(ch: u8) u8 {
        if (ch >= 'a' and ch <= 'z') {
            return 'A' + ch - 'a';
        }
        return ch;
    }

    fn profileCounterLaneLessThan(_: void, lhs: ProfileCounterLane, rhs: ProfileCounterLane) bool {
        const len = @min(lhs.name.len, rhs.name.len);
        for (0..len) |i| {
            const a = lhs.name[i];
            const b = rhs.name[i];
            if (a != b) {
                return toUpperCase(a) < toUpperCase(b);
            }
        }
        return lhs.name.len < rhs.name.len;
    }
};