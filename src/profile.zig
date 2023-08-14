const std = @import("std");
const test_utils = @import("./test_utils.zig");
const json_profile_parser = @import("./json_profile_parser.zig");

const Allocator = std.mem.Allocator;
const ArrayList = std.ArrayList;
const TraceEvent = json_profile_parser.TraceEvent;
const expectOptional = test_utils.expectOptional;

pub const UiState = struct {
    open: bool = true,
};

pub const SeriesValue = struct {
    time_us: i64,
    value: f64,

    fn lessThan(_: void, lhs: SeriesValue, rhs: SeriesValue) bool {
        return lhs.time_us < rhs.time_us;
    }
};

pub const Series = struct {
    name: []u8,
    values: ArrayList(SeriesValue),

    pub fn init(allocator: Allocator, name: []const u8) !Series {
        return .{
            .name = try allocator.dupe(u8, name),
            .values = ArrayList(SeriesValue).init(allocator),
        };
    }

    pub fn done(self: *Series) void {
        std.sort.block(SeriesValue, self.values.items, {}, SeriesValue.lessThan);
    }

    pub fn deinit(self: *Series) void {
        self.values.allocator.free(self.name);
        self.values.deinit();
    }

    pub fn iter(self: *const Series, start_time_us: i64, min_duration_us: i64) SeriesIter {
        return .{
            .values = self.values.items,
            .cursor = start_time_us,
            .min_duration_us = min_duration_us,
        };
    }
};

pub const SeriesIter = struct {
    values: []const SeriesValue,
    cursor: i64,
    min_duration_us: i64,

    prev_index: ?usize = null,

    pub fn next(self: *SeriesIter) ?*const SeriesValue {
        if (self.prev_index) |prev| {
            var index = prev + 1;
            while (index < self.values.len) {
                const value = self.values[index];
                if (value.time_us >= self.cursor) {
                    self.cursor = @max(value.time_us, self.cursor + self.min_duration_us);
                    break;
                }
                index += 1;
            }
            self.prev_index = index;
        } else {
            // Find the largest value that is less than the cursor.

            // TODO: Optimize with binary search.
            var index_larger_or_equal = self.values.len;
            for (self.values, 0..) |value, index| {
                if (value.time_us >= self.cursor) {
                    index_larger_or_equal = index;
                    break;
                }
            }
            if (index_larger_or_equal > 0) {
                self.prev_index = index_larger_or_equal - 1;
            } else {
                self.prev_index = 0;
            }
        }

        if (self.prev_index.? < self.values.len) {
            return &self.values[self.prev_index.?];
        } else {
            return null;
        }
    }
};

const ProfileCounterLane = struct {
    name: []u8,
    max_value: f64,
    series: ArrayList(Series),

    ui: UiState = .{},

    fn init(allocator: Allocator, name: []const u8) ProfileCounterLane {
        return .{
            .name = allocator.dupe(u8, name) catch unreachable,
            .max_value = 0,
            .series = ArrayList(Series).init(allocator),
        };
    }

    fn deinit(self: *ProfileCounterLane) void {
        for (self.series.items) |*series| {
            series.deinit();
        }
        self.series.deinit();
    }

    pub fn addCounter(self: *ProfileCounterLane, time_us: i64, name: []const u8, value: f64) !void {
        self.max_value = @max(self.max_value, value);
        var series = try self.getOrCreateSeries(name);
        try series.values.append(.{ .time_us = time_us, .value = value });
    }

    fn getOrCreateSeries(self: *ProfileCounterLane, name: []const u8) !*Series {
        for (self.series.items) |*series| {
            if (std.mem.eql(u8, series.name, name)) {
                return series;
            }
        }

        try self.series.append(try Series.init(self.series.allocator, name));
        return &self.series.items[self.series.items.len - 1];
    }

    pub fn done(self: *ProfileCounterLane) void {
        for (self.series.items) |*series| {
            series.done();
        }
    }
};

pub const Span = struct {
    name: []u8,
    start_time_us: i64,
    duration_us: i64,

    self_duration_us: i64 = 0,

    fn lessThan(_: void, lhs: Span, rhs: Span) bool {
        if (lhs.start_time_us == rhs.start_time_us) {
            return rhs.duration_us < lhs.duration_us;
        }

        return lhs.start_time_us < rhs.start_time_us;
    }
};

pub const ThreadSubLane = struct {
    spans: ArrayList(*Span),

    pub fn init(allocator: Allocator) ThreadSubLane {
        return .{
            .spans = ArrayList(*Span).init(allocator),
        };
    }

    pub fn deinit(self: *ThreadSubLane) void {
        self.spans.deinit();
    }

    pub fn addSpan(self: *ThreadSubLane, span: *Span) !void {
        try self.spans.append(span);
    }

    pub fn iter(self: *const ThreadSubLane, start_time_us: i64, min_duration_us: i64) ThreadSubLaneIter {
        return .{
            .spans = self.spans.items,
            .prev_index = null,
            .cursor = start_time_us,
            .min_duration_us = min_duration_us,
        };
    }
};

pub const ThreadSubLaneIter = struct {
    spans: []const *Span,
    prev_index: ?usize,
    cursor: i64,
    min_duration_us: i64,

    pub fn next(self: *ThreadSubLaneIter) ?*const Span {
        var index = if (self.prev_index) |prev| prev + 1 else 0;
        while (index < self.spans.len) {
            const span = self.spans[index];
            const end_time_us = span.start_time_us + span.duration_us;
            if (self.cursor < end_time_us) {
                self.cursor = @max(end_time_us, self.cursor + self.min_duration_us);
                break;
            }
            index += 1;
        }
        self.prev_index = index;

        if (index < self.spans.len) {
            return self.spans[index];
        } else {
            return null;
        }
    }
};

const ThreadLane = struct {
    allocator: Allocator,
    tid: i64,
    spans: ArrayList(Span),
    sub_lanes: ArrayList(ThreadSubLane),

    name: ?[]u8 = null,
    sort_index: ?i64 = null,

    ui: UiState = .{},

    pub fn init(allocator: Allocator, tid: i64) ThreadLane {
        return .{
            .allocator = allocator,
            .tid = tid,
            .spans = ArrayList(Span).init(allocator),
            .sub_lanes = ArrayList(ThreadSubLane).init(allocator),
        };
    }

    pub fn deinit(self: *ThreadLane) void {
        for (self.sub_lanes.items) |*sub_lane| {
            sub_lane.deinit();
        }
        self.sub_lanes.deinit();

        for (self.spans.items) |*span| {
            self.allocator.free(span.name);
        }
        self.spans.deinit();

        if (self.name) |name| {
            self.allocator.free(name);
        }
    }

    pub fn setName(self: *ThreadLane, name: []const u8) !void {
        if (self.name) |n| {
            self.allocator.free(n);
        }
        self.name = try self.allocator.dupe(u8, name);
    }

    pub fn addSpan(self: *ThreadLane, name: ?[]const u8, start_time_us: i64, duration_us: i64) !void {
        try self.spans.append(.{
            .name = try self.allocator.dupe(u8, name orelse ""),
            .start_time_us = start_time_us,
            .duration_us = duration_us,
        });
    }

    fn getOrCreateSubLane(self: *ThreadLane, level: usize) !*ThreadSubLane {
        while (level >= self.sub_lanes.items.len) {
            try self.sub_lanes.append(ThreadSubLane.init(self.allocator));
        }
        return &self.sub_lanes.items[level];
    }

    pub fn done(self: *ThreadLane) !void {
        std.sort.block(Span, self.spans.items, {}, Span.lessThan);

        if (self.spans.items.len > 0) {
            const first_span = self.spans.items[0];
            const start_time_us = first_span.start_time_us;
            var end_time_us = start_time_us;
            for (self.spans.items) |span| {
                end_time_us = @max(end_time_us, span.start_time_us + span.duration_us);
            }
            _ = try self.mergeSpans(0, start_time_us, end_time_us, 0);
        }
    }

    const MergeResult = struct {
        index: usize,
        total_duration_us: i64,
    };

    fn mergeSpans(self: *ThreadLane, level: usize, start_time_us: i64, end_time_us: i64, span_start_index: usize) !MergeResult {
        var index = span_start_index;
        var total: i64 = 0;
        while (index < self.spans.items.len) {
            const span = &self.spans.items[index];
            const span_end_time_us = span.start_time_us + span.duration_us;
            if (span.start_time_us >= start_time_us and span_end_time_us <= end_time_us) {
                var sub_lane = try self.getOrCreateSubLane(level);
                try sub_lane.addSpan(span);
                const result = try mergeSpans(self, level + 1, span.start_time_us, span_end_time_us, index + 1);
                index = result.index;
                span.self_duration_us = @max(span.duration_us - result.total_duration_us, 0);
                total += span.duration_us;
            } else {
                break;
            }
        }
        return .{ .index = index, .total_duration_us = total };
    }

    fn lessThan(_: void, lhs: ThreadLane, rhs: ThreadLane) bool {
        if (lhs.sort_index != null or rhs.sort_index != null) {
            if (lhs.sort_index != null and rhs.sort_index == null) {
                return true;
            } else if (lhs.sort_index == null and rhs.sort_index != null) {
                return false;
            } else if (lhs.sort_index.? != rhs.sort_index.?) {
                return lhs.sort_index.? < rhs.sort_index.?;
            }
        }

        if (lhs.name != null or rhs.name != null) {
            if (lhs.name != null and rhs.name == null) {
                return true;
            } else if (lhs.name == null and rhs.name != null) {
                return false;
            } else if (!std.mem.eql(u8, lhs.name.?, rhs.name.?)) {
                return nameLessThan(lhs.name.?, rhs.name.?);
            }
        }

        return lhs.tid < rhs.tid;
    }
};

fn toUpperCase(ch: u8) u8 {
    if (ch >= 'a' and ch <= 'z') {
        return 'A' + ch - 'a';
    }
    return ch;
}

fn nameLessThan(lhs_name: []const u8, rhs_name: []const u8) bool {
    const len = @min(lhs_name.len, rhs_name.len);
    for (0..len) |i| {
        const a = lhs_name[i];
        const b = rhs_name[i];
        if (a != b) {
            return toUpperCase(a) < toUpperCase(b);
        }
    }
    return lhs_name.len < rhs_name.len;
}

pub const Profile = struct {
    allocator: Allocator,
    min_time_us: i64,
    max_time_us: i64,
    counter_lanes: ArrayList(ProfileCounterLane),
    thread_lanes: ArrayList(ThreadLane),

    pub fn init(allocator: Allocator) Profile {
        return .{
            .allocator = allocator,
            .min_time_us = 0,
            .max_time_us = 0,
            .counter_lanes = ArrayList(ProfileCounterLane).init(allocator),
            .thread_lanes = ArrayList(ThreadLane).init(allocator),
        };
    }

    pub fn deinit(self: *Profile) void {
        for (self.counter_lanes.items) |*counter_lane| {
            counter_lane.deinit();
        }
        self.counter_lanes.deinit();

        for (self.thread_lanes.items) |*thread_lane| {
            thread_lane.deinit();
        }
        self.thread_lanes.deinit();
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

    pub fn handleTraceEvent(self: *Profile, trace_event: *const TraceEvent) !void {
        if (trace_event.ts) |ts| {
            self.min_time_us = @min(self.min_time_us, ts);
            var end = ts;
            if (trace_event.dur) |dur| {
                end += dur;
            }
            self.max_time_us = @max(self.max_time_us, end);
        }

        if (trace_event.ph) |ph| {
            switch (ph) {
                // Counter event
                'C' => {
                    // TODO: handle trace_event.id
                    if (trace_event.name) |name| {
                        var counter_lane = try self.getOrCreateCounterLane(name);
                        if (trace_event.ts) |ts| {
                            if (trace_event.args) |args| {
                                switch (args) {
                                    .object => |obj| {
                                        var iter = obj.iterator();
                                        while (iter.next()) |entry| {
                                            const value_name = entry.key_ptr.*;
                                            switch (entry.value_ptr.*) {
                                                .string, .number_string => |num| {
                                                    const val = try std.fmt.parseFloat(f64, num);
                                                    try counter_lane.addCounter(ts, value_name, val);
                                                },
                                                .float => |val| {
                                                    try counter_lane.addCounter(ts, value_name, val);
                                                },
                                                .integer => |val| {
                                                    try counter_lane.addCounter(ts, value_name, @floatFromInt(val));
                                                },
                                                else => {},
                                            }
                                        }
                                    },
                                    else => {},
                                }
                            }
                        }
                    }
                },
                // Complete event
                'X' => {
                    // TODO: handle pid
                    if (trace_event.tid) |tid| {
                        if (trace_event.ts) |ts| {
                            if (trace_event.dur) |dur| {
                                var thread_lane = try self.getOrCreateThreadLane(tid);
                                try thread_lane.addSpan(trace_event.name, ts, dur);
                            }
                        }
                    }
                },
                // Metadata event
                'M' => {
                    if (trace_event.name) |name| {
                        if (std.mem.eql(u8, name, "thread_name")) {
                            if (trace_event.tid) |tid| {
                                if (trace_event.args) |args| {
                                    switch (args) {
                                        .object => |obj| {
                                            if (obj.get("name")) |val| {
                                                switch (val) {
                                                    .string => |str| {
                                                        var thread_lane = try self.getOrCreateThreadLane(tid);
                                                        try thread_lane.setName(str);
                                                    },
                                                    else => {},
                                                }
                                            }
                                        },
                                        else => {},
                                    }
                                }
                            }
                        } else if (std.mem.eql(u8, name, "thread_sort_index")) {
                            if (trace_event.tid) |tid| {
                                if (trace_event.args) |args| {
                                    switch (args) {
                                        .object => |obj| {
                                            if (obj.get("sort_index")) |val| {
                                                switch (val) {
                                                    .number_string, .string => |str| {
                                                        const sort_index = try std.fmt.parseInt(i64, str, 10);
                                                        try self.handleThreadSortIndex(tid, sort_index);
                                                    },
                                                    .integer => |sort_index| {
                                                        try self.handleThreadSortIndex(tid, sort_index);
                                                    },
                                                    else => {},
                                                }
                                            }
                                        },
                                        else => {},
                                    }
                                }
                            }
                        }
                    }
                },
                else => {},
            }
        }
    }

    fn handleThreadSortIndex(self: *Profile, tid: i64, sort_index: i64) !void {
        var thread_lane = try self.getOrCreateThreadLane(tid);
        thread_lane.sort_index = sort_index;
    }

    pub fn done(self: *Profile) !void {
        std.sort.block(ProfileCounterLane, self.counter_lanes.items, {}, profileCounterLaneLessThan);
        for (self.counter_lanes.items) |*counter_lane| {
            counter_lane.done();
        }

        std.sort.block(ThreadLane, self.thread_lanes.items, {}, ThreadLane.lessThan);
        for (self.thread_lanes.items) |*thread_lane| {
            try thread_lane.done();
        }
    }

    fn profileCounterLaneLessThan(_: void, lhs: ProfileCounterLane, rhs: ProfileCounterLane) bool {
        return nameLessThan(lhs.name, rhs.name);
    }
};

const ExpectedProfile = struct {
    thread_lanes: ?[]const ExpectedThreadLane = null,
};

const ExpectedThreadLane = struct {
    tid: i64,
    name: ?[]const u8 = null,
    sort_index: ?i64 = null,
    sub_lanes: ?[]const ExpectedSubLane = null,
};

const ExpectedSubLane = struct {
    spans: []const ExpectedSpan,
};

const ExpectedSpan = struct {
    name: []const u8,
    start_time_us: i64,
    duration_us: i64,
};

fn testParse(trace_events: []const TraceEvent, expected_profile: ExpectedProfile) !void {
    var profile = Profile.init(std.testing.allocator);
    defer profile.deinit();

    for (trace_events) |*trace_event| {
        try profile.handleTraceEvent(trace_event);
    }

    try profile.done();

    if (expected_profile.thread_lanes) |expected_thread_lanes| {
        try std.testing.expectEqual(expected_thread_lanes.len, profile.thread_lanes.items.len);

        for (expected_thread_lanes, profile.thread_lanes.items) |expected_thread_lane, actual_thread_lane| {
            try expectEqualThreadLanes(expected_thread_lane, actual_thread_lane);
        }
    }
}

fn expectEqualThreadLanes(expected: ExpectedThreadLane, actual: ThreadLane) !void {
    try std.testing.expectEqual(expected.tid, actual.tid);

    if (try expectOptional(expected.name, actual.name)) {
        try std.testing.expectEqualStrings(expected.name.?, actual.name.?);
    }

    if (try expectOptional(expected.sort_index, actual.sort_index)) {
        try std.testing.expectEqual(expected.sort_index.?, actual.sort_index.?);
    }

    if (expected.sub_lanes) |expected_sub_lanes| {
        try std.testing.expectEqual(expected_sub_lanes.len, actual.sub_lanes.items.len);

        for (expected_sub_lanes, actual.sub_lanes.items) |expected_sub_lane, actual_sub_lane| {
            try expectEqualSubLanes(expected_sub_lane, actual_sub_lane);
        }
    } else {
        try std.testing.expectEqual(@as(usize, 0), actual.sub_lanes.items.len);
    }
}

fn expectEqualSubLanes(expected: ExpectedSubLane, actual: ThreadSubLane) !void {
    try std.testing.expectEqual(expected.spans.len, actual.spans.items.len);

    for (expected.spans, actual.spans.items) |expected_span, actual_span| {
        try expectEqualSpans(expected_span, actual_span);
    }
}

fn expectEqualSpans(expected: ExpectedSpan, actual: *Span) !void {
    try std.testing.expectEqualStrings(expected.name, actual.name);
    try std.testing.expectEqual(expected.start_time_us, actual.start_time_us);
    try std.testing.expectEqual(expected.duration_us, actual.duration_us);
}

fn initArgsSortIndex(sort_index: i64) !std.json.Value {
    var object = std.json.ObjectMap.init(std.testing.allocator);
    try object.put("sort_index", .{ .integer = sort_index });
    return .{ .object = object };
}

fn deinitArgs(sort_index: *std.json.Value) void {
    sort_index.object.deinit();
}

fn createArgsName(name: []const u8) !std.json.Value {
    var object = std.json.ObjectMap.init(std.testing.allocator);
    try object.put("name", .{ .string = name });
    return .{ .object = object };
}

test "parse, spans overlap, place at same level" {
    try testParse(&[_]TraceEvent{
        .{ .ph = 'X', .name = "a", .tid = 1, .ts = 0, .dur = 3 },
        .{ .ph = 'X', .name = "b", .tid = 1, .ts = 1, .dur = 4 },
    }, .{
        .thread_lanes = &[_]ExpectedThreadLane{
            .{
                .tid = 1,
                .sub_lanes = &[_]ExpectedSubLane{
                    .{ .spans = &[_]ExpectedSpan{
                        .{ .name = "a", .start_time_us = 0, .duration_us = 3 },
                        .{ .name = "b", .start_time_us = 1, .duration_us = 4 },
                    } },
                },
            },
        },
    });
}

test "parse, spans have equal start time, sort by duration descending" {
    try testParse(&[_]TraceEvent{
        .{ .ph = 'X', .name = "a", .tid = 1, .ts = 0, .dur = 1 },
        .{ .ph = 'X', .name = "b", .tid = 1, .ts = 0, .dur = 2 },
        .{ .ph = 'X', .name = "c", .tid = 1, .ts = 0, .dur = 3 },
    }, .{
        .thread_lanes = &[_]ExpectedThreadLane{
            .{
                .tid = 1,
                .sub_lanes = &[_]ExpectedSubLane{
                    .{ .spans = &[_]ExpectedSpan{.{ .name = "c", .start_time_us = 0, .duration_us = 3 }} },
                    .{ .spans = &[_]ExpectedSpan{.{ .name = "b", .start_time_us = 0, .duration_us = 2 }} },
                    .{ .spans = &[_]ExpectedSpan{.{ .name = "a", .start_time_us = 0, .duration_us = 1 }} },
                },
            },
        },
    });
}

test "parse, thread_sort_index, sorted" {
    var sort_index_1 = try initArgsSortIndex(1);
    defer deinitArgs(&sort_index_1);
    var sort_index_3 = try initArgsSortIndex(3);
    defer deinitArgs(&sort_index_3);

    try testParse(&[_]TraceEvent{
        .{ .ph = 'M', .name = "thread_sort_index", .tid = 1, .args = sort_index_3 },
        .{ .ph = 'M', .name = "thread_sort_index", .tid = 3, .args = sort_index_1 },
        .{ .ph = 'X', .name = "a", .tid = 1, .ts = 0, .dur = 1 },
        .{ .ph = 'X', .name = "b", .tid = 2, .ts = 0, .dur = 2 },
        .{ .ph = 'X', .name = "c", .tid = 3, .ts = 0, .dur = 3 },
    }, .{
        .thread_lanes = &[_]ExpectedThreadLane{
            .{
                .tid = 3,
                .sort_index = 1,
                .sub_lanes = &[_]ExpectedSubLane{
                    .{ .spans = &[_]ExpectedSpan{.{ .name = "c", .start_time_us = 0, .duration_us = 3 }} },
                },
            },
            .{
                .tid = 1,
                .sort_index = 3,
                .sub_lanes = &[_]ExpectedSubLane{
                    .{ .spans = &[_]ExpectedSpan{.{ .name = "a", .start_time_us = 0, .duration_us = 1 }} },
                },
            },
            .{
                .tid = 2,
                .sub_lanes = &[_]ExpectedSubLane{
                    .{ .spans = &[_]ExpectedSpan{.{ .name = "b", .start_time_us = 0, .duration_us = 2 }} },
                },
            },
        },
    });
}

test "parse, thread_sort_index equal, sort by name" {
    var sort_index_1 = try initArgsSortIndex(1);
    defer deinitArgs(&sort_index_1);
    var name_a = try createArgsName("thread a");
    defer deinitArgs(&name_a);
    var name_b = try createArgsName("thread b");
    defer deinitArgs(&name_b);
    var name_c = try createArgsName("thread c");
    defer deinitArgs(&name_c);

    try testParse(&[_]TraceEvent{
        .{ .ph = 'M', .name = "thread_name", .tid = 1, .args = name_c },
        .{ .ph = 'M', .name = "thread_sort_index", .tid = 1, .args = sort_index_1 },
        .{ .ph = 'M', .name = "thread_name", .tid = 2, .args = name_b },
        .{ .ph = 'M', .name = "thread_sort_index", .tid = 2, .args = sort_index_1 },
        .{ .ph = 'M', .name = "thread_name", .tid = 3, .args = name_a },
        .{ .ph = 'M', .name = "thread_sort_index", .tid = 3, .args = sort_index_1 },
        .{ .ph = 'X', .name = "c", .tid = 1, .ts = 0, .dur = 1 },
        .{ .ph = 'X', .name = "b", .tid = 2, .ts = 0, .dur = 2 },
        .{ .ph = 'X', .name = "a", .tid = 3, .ts = 0, .dur = 3 },
    }, .{
        .thread_lanes = &[_]ExpectedThreadLane{
            .{
                .tid = 3,
                .name = "thread a",
                .sort_index = 1,
                .sub_lanes = &[_]ExpectedSubLane{
                    .{ .spans = &[_]ExpectedSpan{.{ .name = "a", .start_time_us = 0, .duration_us = 3 }} },
                },
            },
            .{
                .tid = 2,
                .name = "thread b",
                .sort_index = 1,
                .sub_lanes = &[_]ExpectedSubLane{
                    .{ .spans = &[_]ExpectedSpan{.{ .name = "b", .start_time_us = 0, .duration_us = 2 }} },
                },
            },
            .{
                .tid = 1,
                .name = "thread c",
                .sort_index = 1,
                .sub_lanes = &[_]ExpectedSubLane{
                    .{ .spans = &[_]ExpectedSpan{.{ .name = "c", .start_time_us = 0, .duration_us = 1 }} },
                },
            },
        },
    });
}
