const std = @import("std");
const test_utils = @import("./test_utils.zig");
const json_profile_parser = @import("./json_profile_parser.zig");
const tracy = @import("tracy.zig");

const Allocator = std.mem.Allocator;
const Arena = std.heap.ArenaAllocator;
const ArrayList = std.ArrayList;
const TraceEvent = json_profile_parser.TraceEvent;
const expect_optional = test_utils.expect_optional;

pub const UiState = struct {
    open: bool = true,
};

pub const SeriesValue = struct {
    time_us: i64,
    value: f64,

    fn less_than(_: void, lhs: SeriesValue, rhs: SeriesValue) bool {
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
        std.sort.block(SeriesValue, self.values.items, {}, SeriesValue.less_than);
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

pub const Counter = struct {
    name: []u8,
    max_value: f64,
    series: ArrayList(Series),

    ui: UiState = .{},

    fn init(allocator: Allocator, name: []const u8) Counter {
        return .{
            .name = allocator.dupe(u8, name) catch unreachable,
            .max_value = 0,
            .series = ArrayList(Series).init(allocator),
        };
    }

    fn add_sample(self: *Counter, time_us: i64, name: []const u8, value: f64) !void {
        self.max_value = @max(self.max_value, value);
        var series = try self.get_or_create_series(name);
        try series.values.append(.{ .time_us = time_us, .value = value });
    }

    fn get_or_create_series(self: *Counter, name: []const u8) !*Series {
        for (self.series.items) |*series| {
            if (std.mem.eql(u8, series.name, name)) {
                return series;
            }
        }

        try self.series.append(try Series.init(self.series.allocator, name));
        return &self.series.items[self.series.items.len - 1];
    }

    fn done(self: *Counter) void {
        for (self.series.items) |*series| {
            series.done();
        }
    }

    fn less_than(_: void, lhs: Counter, rhs: Counter) bool {
        return name_less_than(lhs.name, rhs.name);
    }
};

pub const Span = struct {
    name: []u8,
    start_time_us: i64,
    duration_us: i64,
    end_time_us: i64,

    self_duration_us: i64 = 0,
    category: ?[]u8 = null,

    fn less_than(_: void, lhs: Span, rhs: Span) bool {
        if (lhs.start_time_us == rhs.start_time_us) {
            return rhs.duration_us < lhs.duration_us;
        }

        return lhs.start_time_us < rhs.start_time_us;
    }
};

pub const Track = struct {
    spans: ArrayList(*Span),

    pub fn init(allocator: Allocator) Track {
        return .{
            .spans = ArrayList(*Span).init(allocator),
        };
    }

    pub fn create_span(self: *Track, span: *Span) !void {
        try self.spans.append(span);
    }

    pub fn iter(self: *const Track, start_time_us: i64, min_duration_us: i64) SpanIter {
        return .{
            .spans = self.spans.items,
            .prev_index = null,
            .cursor = start_time_us,
            .min_duration_us = min_duration_us,
        };
    }
};

pub const SpanIter = struct {
    spans: []const *Span,
    prev_index: ?usize,
    cursor: i64,
    min_duration_us: i64,

    pub fn next(self: *SpanIter) ?*const Span {
        const offset = if (self.prev_index) |prev| prev + 1 else 0;
        if (offset >= self.spans.len) {
            return null;
        }

        const index = offset + find_first_greater(*const Span, self.spans[offset..], self.cursor, {}, SpanIter.compare);
        self.prev_index = index;

        if (index >= self.spans.len) {
            return null;
        }

        const span = self.spans[index];
        std.debug.assert(self.cursor < span.end_time_us);
        self.cursor = @max(span.end_time_us, self.cursor + self.min_duration_us);
        return span;
    }

    fn compare(_: void, cursor: i64, span: *const Span) std.math.Order {
        if (cursor == span.end_time_us) {
            return .eq;
        } else if (cursor < span.end_time_us) {
            return .lt;
        } else {
            return .gt;
        }
    }
};

// Find the very first index of items where key < items[index]
fn find_first_greater(
    comptime T: type,
    items: []const T,
    key: anytype,
    context: anytype,
    comptime cmp: fn (context: @TypeOf(context), key: @TypeOf(key), mid_item: T) std.math.Order,
) usize {
    var left: usize = 0;
    var right: usize = items.len;

    while (left < right) {
        // Avoid overflowing in the midpoint calculation
        const mid = left + (right - left) / 2;
        // Compare the key with the midpoint element
        switch (cmp(context, key, items[mid])) {
            .lt => right = mid,
            .eq, .gt => left = mid + 1,
        }
    }

    return left;
}

pub const Thread = struct {
    allocator: Allocator,
    tid: i64,
    spans: ArrayList(Span),
    tracks: ArrayList(Track),

    name: ?[]u8 = null,
    sort_index: ?i64 = null,

    ui: UiState = .{},

    pub fn init(allocator: Allocator, tid: i64) Thread {
        return .{
            .allocator = allocator,
            .tid = tid,
            .spans = ArrayList(Span).init(allocator),
            .tracks = ArrayList(Track).init(allocator),
        };
    }

    pub fn set_name(self: *Thread, name: []const u8) !void {
        if (self.name) |n| {
            self.allocator.free(n);
        }
        self.name = try self.allocator.dupe(u8, name);
    }

    pub fn create_span(self: *Thread, name: ?[]const u8, start_time_us: i64, duration_us: i64) !*Span {
        try self.spans.append(.{
            .name = try self.allocator.dupe(u8, name orelse ""),
            .start_time_us = start_time_us,
            .duration_us = duration_us,
            .end_time_us = start_time_us + duration_us,
        });
        return &self.spans.items[self.spans.items.len - 1];
    }

    fn get_or_create_track(self: *Thread, level: usize) !*Track {
        while (level >= self.tracks.items.len) {
            try self.tracks.append(Track.init(self.allocator));
        }
        return &self.tracks.items[level];
    }

    pub fn done(self: *Thread) !void {
        std.sort.block(Span, self.spans.items, {}, Span.less_than);

        if (self.spans.items.len > 0) {
            const first_span = self.spans.items[0];
            const start_time_us = first_span.start_time_us;
            var end_time_us = start_time_us;
            for (self.spans.items) |span| {
                end_time_us = @max(end_time_us, span.start_time_us + span.duration_us);
            }
            _ = try self.merge_spans(0, start_time_us, end_time_us, 0);
        }
    }

    const MergeResult = struct {
        index: usize,
        total_duration_us: i64,
    };

    fn merge_spans(self: *Thread, level: usize, start_time_us: i64, end_time_us: i64, span_start_index: usize) !MergeResult {
        var index = span_start_index;
        var total: i64 = 0;
        while (index < self.spans.items.len) {
            const span = &self.spans.items[index];
            const span_end_time_us = span.start_time_us + span.duration_us;
            if (span.start_time_us >= start_time_us and span_end_time_us <= end_time_us) {
                var track = try self.get_or_create_track(level);
                try track.create_span(span);
                const result = try merge_spans(self, level + 1, span.start_time_us, span_end_time_us, index + 1);
                index = result.index;
                span.self_duration_us = @max(span.duration_us - result.total_duration_us, 0);
                total += span.duration_us;
            } else {
                break;
            }
        }
        return .{ .index = index, .total_duration_us = total };
    }

    fn less_than(_: void, lhs: Thread, rhs: Thread) bool {
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
                return name_less_than(lhs.name.?, rhs.name.?);
            }
        }

        return lhs.tid < rhs.tid;
    }
};

fn to_upper_case(ch: u8) u8 {
    if (ch >= 'a' and ch <= 'z') {
        return 'A' + ch - 'a';
    }
    return ch;
}

fn name_less_than(lhs_name: []const u8, rhs_name: []const u8) bool {
    const len = @min(lhs_name.len, rhs_name.len);
    for (0..len) |i| {
        const a = lhs_name[i];
        const b = rhs_name[i];
        if (a != b) {
            return to_upper_case(a) < to_upper_case(b);
        }
    }
    return lhs_name.len < rhs_name.len;
}

pub const Process = struct {
    allocator: Allocator,
    pid: i64,
    counters: ArrayList(Counter),
    threads: ArrayList(Thread),

    fn init(allocator: Allocator, pid: i64) Process {
        return .{
            .allocator = allocator,
            .pid = pid,
            .counters = ArrayList(Counter).init(allocator),
            .threads = ArrayList(Thread).init(allocator),
        };
    }

    fn done(self: *Process) !void {
        std.sort.block(Counter, self.counters.items, {}, Counter.less_than);
        for (self.counters.items) |*counter| {
            counter.done();
        }

        std.sort.block(Thread, self.threads.items, {}, Thread.less_than);
        for (self.threads.items) |*thread| {
            try thread.done();
        }
    }

    fn get_or_create_counter(self: *Process, name: []const u8) !*Counter {
        for (self.counters.items) |*counter| {
            if (std.mem.eql(u8, counter.name, name)) {
                return counter;
            }
        }

        try self.counters.append(Counter.init(self.allocator, name));
        return &self.counters.items[self.counters.items.len - 1];
    }

    pub fn get_or_create_thread(self: *Process, tid: i64) !*Thread {
        for (self.threads.items) |*thread| {
            if (thread.tid == tid) {
                return thread;
            }
        }

        try self.threads.append(Thread.init(self.allocator, tid));
        return &self.threads.items[self.threads.items.len - 1];
    }
};

pub const Profile = struct {
    arena: Arena,
    min_time_us: i64,
    max_time_us: i64,
    processes: ArrayList(Process),

    pub fn init(allocator: Allocator) Profile {
        return .{
            .arena = Arena.init(allocator),
            .min_time_us = std.math.maxInt(i64),
            .max_time_us = 0,
            // The allocator will be replaced with arena when appending items.
            .processes = ArrayList(Process).init(allocator),
        };
    }

    pub fn deinit(self: *Profile) void {
        const trace = tracy.traceNamed(@src(), "Profile.deinit");
        defer trace.end();

        self.arena.deinit();
    }

    fn get_or_create_process(self: *Profile, pid: i64) !*Process {
        for (self.processes.items) |*process| {
            if (process.pid == pid) {
                return process;
            }
        }

        if (self.processes.items.len == 0) {
            self.processes = ArrayList(Process).init(self.arena.allocator());
        }

        try self.processes.append(Process.init(self.arena.allocator(), pid));
        return &self.processes.items[self.processes.items.len - 1];
    }

    pub fn handle_trace_event(self: *Profile, trace_event: *const TraceEvent) !void {
        if (trace_event.ph) |ph| {
            switch (ph) {
                // Counter event
                'C' => {
                    // TODO: handle trace_event.id
                    if (trace_event.pid) |pid| {
                        if (trace_event.name) |name| {
                            if (trace_event.ts) |ts| {
                                if (trace_event.args) |args| {
                                    switch (args) {
                                        .object => |obj| {
                                            try self.handle_counter_event(trace_event, pid, name, ts, obj);
                                        },
                                        else => {},
                                    }
                                }
                            }
                        }
                    }
                },
                // Complete event
                'X' => {
                    // TODO: handle pid
                    if (trace_event.pid) |pid| {
                        if (trace_event.tid) |tid| {
                            if (trace_event.ts) |ts| {
                                if (trace_event.dur) |dur| {
                                    try self.handle_complete_event(trace_event, pid, tid, ts, dur);
                                }
                            }
                        }
                    }
                },
                // Metadata event
                'M' => {
                    if (trace_event.name) |name| {
                        if (std.mem.eql(u8, name, "thread_name")) {
                            if (trace_event.pid) |pid| {
                                if (trace_event.tid) |tid| {
                                    if (trace_event.args) |args| {
                                        switch (args) {
                                            .object => |obj| {
                                                if (obj.get("name")) |val| {
                                                    switch (val) {
                                                        .string => |str| {
                                                            try self.handle_thread_name(pid, tid, str);
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
                        } else if (std.mem.eql(u8, name, "thread_sort_index")) {
                            if (trace_event.pid) |pid| {
                                if (trace_event.tid) |tid| {
                                    if (trace_event.args) |args| {
                                        switch (args) {
                                            .object => |obj| {
                                                if (obj.get("sort_index")) |val| {
                                                    switch (val) {
                                                        .number_string, .string => |str| {
                                                            const sort_index = try std.fmt.parseInt(i64, str, 10);
                                                            try self.handle_thread_sort_index(pid, tid, sort_index);
                                                        },
                                                        .integer => |sort_index| {
                                                            try self.handle_thread_sort_index(pid, tid, sort_index);
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
                    }
                },
                else => {},
            }
        }
    }

    fn handle_counter_event(self: *Profile, trace_event: *const TraceEvent, pid: i64, name: []const u8, ts: i64, args: std.json.ObjectMap) !void {
        var process = try self.get_or_create_process(pid);
        var counter = try process.get_or_create_counter(name);
        var iter = args.iterator();
        while (iter.next()) |entry| {
            const value_name = entry.key_ptr.*;
            switch (entry.value_ptr.*) {
                .string, .number_string => |num| {
                    const val = try std.fmt.parseFloat(f64, num);
                    try counter.add_sample(ts, value_name, val);
                    self.maybe_update_min_max(trace_event);
                },
                .float => |val| {
                    try counter.add_sample(ts, value_name, val);
                    self.maybe_update_min_max(trace_event);
                },
                .integer => |val| {
                    try counter.add_sample(ts, value_name, @floatFromInt(val));
                    self.maybe_update_min_max(trace_event);
                },
                else => {},
            }
        }
    }

    fn handle_complete_event(self: *Profile, trace_event: *const TraceEvent, pid: i64, tid: i64, ts: i64, dur: i64) !void {
        var process = try self.get_or_create_process(pid);
        var thread = try process.get_or_create_thread(tid);

        var span = try thread.create_span(trace_event.name, ts, dur);

        if (trace_event.cat) |cat| {
            span.category = try self.arena.allocator().dupe(u8, cat);
        }

        self.maybe_update_min_max(trace_event);
    }

    fn handle_thread_name(self: *Profile, pid: i64, tid: i64, name: []const u8) !void {
        var process = try self.get_or_create_process(pid);
        var thread = try process.get_or_create_thread(tid);
        try thread.set_name(name);
    }

    fn handle_thread_sort_index(self: *Profile, pid: i64, tid: i64, sort_index: i64) !void {
        var process = try self.get_or_create_process(pid);
        var thread = try process.get_or_create_thread(tid);
        thread.sort_index = sort_index;
    }

    fn maybe_update_min_max(self: *Profile, trace_event: *const TraceEvent) void {
        if (trace_event.ts) |ts| {
            self.min_time_us = @min(self.min_time_us, ts);
            var end = ts;
            if (trace_event.dur) |dur| {
                end += dur;
            }
            self.max_time_us = @max(self.max_time_us, end);
        }
    }

    pub fn done(self: *Profile) !void {
        for (self.processes.items) |*process| {
            try process.done();
        }
        self.min_time_us = @min(self.min_time_us, self.max_time_us);
    }
};

const ExpectedProfile = struct {
    processes: ?[]const ExpectedProcess = null,
};

const ExpectedProcess = struct {
    pid: i64,
    threads: ?[]const ExpectedThread = null,
};

const ExpectedThread = struct {
    tid: i64,
    name: ?[]const u8 = null,
    sort_index: ?i64 = null,
    tracks: ?[]const ExpectedTrack = null,
};

const ExpectedTrack = struct {
    spans: []const ExpectedSpan,
};

const ExpectedSpan = struct {
    name: []const u8,
    start_time_us: i64,
    duration_us: i64,
};

fn parse(trace_events: []const TraceEvent) !Profile {
    var profile = Profile.init(std.testing.allocator);

    for (trace_events) |*trace_event| {
        try profile.handle_trace_event(trace_event);
    }

    try profile.done();

    return profile;
}

fn test_parse(trace_events: []const TraceEvent, expected_profile: ExpectedProfile) !void {
    var profile = try parse(trace_events);
    defer profile.deinit();
    try expect_equal_profiles(expected_profile, profile);
}

fn expect_equal_profiles(expected: ExpectedProfile, actual: Profile) !void {
    if (expected.processes) |expected_processes| {
        try std.testing.expectEqual(expected_processes.len, actual.processes.items.len);

        for (expected_processes, actual.processes.items) |expected_process, actual_process| {
            try expect_equal_processes(expected_process, actual_process);
        }
    } else {
        try std.testing.expectEqual(@as(usize, 0), actual.processes.items.len);
    }
}

fn expect_equal_processes(expected: ExpectedProcess, actual: Process) !void {
    try std.testing.expectEqual(expected.pid, actual.pid);

    if (expected.threads) |expected_threads| {
        try std.testing.expectEqual(expected_threads.len, actual.threads.items.len);

        for (expected_threads, actual.threads.items) |expected_thread, actual_thread| {
            try expect_equal_threads(expected_thread, actual_thread);
        }
    } else {
        try std.testing.expectEqual(@as(usize, 0), actual.threads.items.len);
    }
}

fn expect_equal_threads(expected: ExpectedThread, actual: Thread) !void {
    try std.testing.expectEqual(expected.tid, actual.tid);

    if (try expect_optional(expected.name, actual.name)) {
        try std.testing.expectEqualStrings(expected.name.?, actual.name.?);
    }

    if (try expect_optional(expected.sort_index, actual.sort_index)) {
        try std.testing.expectEqual(expected.sort_index.?, actual.sort_index.?);
    }

    if (expected.tracks) |expected_tracks| {
        try std.testing.expectEqual(expected_tracks.len, actual.tracks.items.len);

        for (expected_tracks, actual.tracks.items) |expected_track, actual_track| {
            try expect_equal_tracks(expected_track, actual_track);
        }
    } else {
        try std.testing.expectEqual(@as(usize, 0), actual.tracks.items.len);
    }
}

fn expect_equal_tracks(expected: ExpectedTrack, actual: Track) !void {
    try std.testing.expectEqual(expected.spans.len, actual.spans.items.len);

    for (expected.spans, actual.spans.items) |expected_span, actual_span| {
        try expect_equal_span(expected_span, actual_span);
    }
}

fn expect_equal_span(expected: ExpectedSpan, actual: *Span) !void {
    try std.testing.expectEqualStrings(expected.name, actual.name);
    try std.testing.expectEqual(expected.start_time_us, actual.start_time_us);
    try std.testing.expectEqual(expected.duration_us, actual.duration_us);
}

fn init_args_sort_index(sort_index: i64) !std.json.Value {
    var object = std.json.ObjectMap.init(std.testing.allocator);
    try object.put("sort_index", .{ .integer = sort_index });
    return .{ .object = object };
}

fn deinit_args(sort_index: *std.json.Value) void {
    sort_index.object.deinit();
}

fn init_args_name(name: []const u8) !std.json.Value {
    var object = std.json.ObjectMap.init(std.testing.allocator);
    try object.put("name", .{ .string = name });
    return .{ .object = object };
}

test "parse, spans overlap, place at same level" {
    try test_parse(&[_]TraceEvent{
        .{ .ph = 'X', .name = "a", .pid = 1, .tid = 1, .ts = 0, .dur = 3 },
        .{ .ph = 'X', .name = "b", .pid = 1, .tid = 1, .ts = 1, .dur = 4 },
    }, .{
        .processes = &[_]ExpectedProcess{
            .{
                .pid = 1,
                .threads = &[_]ExpectedThread{
                    .{
                        .tid = 1,
                        .tracks = &[_]ExpectedTrack{
                            .{ .spans = &[_]ExpectedSpan{
                                .{ .name = "a", .start_time_us = 0, .duration_us = 3 },
                                .{ .name = "b", .start_time_us = 1, .duration_us = 4 },
                            } },
                        },
                    },
                },
            },
        },
    });
}

test "parse, spans have equal start time, sort by duration descending" {
    try test_parse(&[_]TraceEvent{
        .{ .ph = 'X', .name = "a", .pid = 1, .tid = 1, .ts = 0, .dur = 1 },
        .{ .ph = 'X', .name = "b", .pid = 1, .tid = 1, .ts = 0, .dur = 2 },
        .{ .ph = 'X', .name = "c", .pid = 1, .tid = 1, .ts = 0, .dur = 3 },
    }, .{
        .processes = &[_]ExpectedProcess{
            .{
                .pid = 1,
                .threads = &[_]ExpectedThread{
                    .{
                        .tid = 1,
                        .tracks = &[_]ExpectedTrack{
                            .{ .spans = &[_]ExpectedSpan{.{ .name = "c", .start_time_us = 0, .duration_us = 3 }} },
                            .{ .spans = &[_]ExpectedSpan{.{ .name = "b", .start_time_us = 0, .duration_us = 2 }} },
                            .{ .spans = &[_]ExpectedSpan{.{ .name = "a", .start_time_us = 0, .duration_us = 1 }} },
                        },
                    },
                },
            },
        },
    });
}

test "parse, thread_sort_index, sorted" {
    var sort_index_1 = try init_args_sort_index(1);
    defer deinit_args(&sort_index_1);
    var sort_index_3 = try init_args_sort_index(3);
    defer deinit_args(&sort_index_3);

    try test_parse(&[_]TraceEvent{
        .{ .ph = 'M', .name = "thread_sort_index", .pid = 1, .tid = 1, .args = sort_index_3 },
        .{ .ph = 'M', .name = "thread_sort_index", .pid = 1, .tid = 3, .args = sort_index_1 },
        .{ .ph = 'X', .name = "a", .pid = 1, .tid = 1, .ts = 0, .dur = 1 },
        .{ .ph = 'X', .name = "b", .pid = 1, .tid = 2, .ts = 0, .dur = 2 },
        .{ .ph = 'X', .name = "c", .pid = 1, .tid = 3, .ts = 0, .dur = 3 },
    }, .{
        .processes = &[_]ExpectedProcess{
            .{
                .pid = 1,
                .threads = &[_]ExpectedThread{
                    .{
                        .tid = 3,
                        .sort_index = 1,
                        .tracks = &[_]ExpectedTrack{
                            .{ .spans = &[_]ExpectedSpan{.{ .name = "c", .start_time_us = 0, .duration_us = 3 }} },
                        },
                    },
                    .{
                        .tid = 1,
                        .sort_index = 3,
                        .tracks = &[_]ExpectedTrack{
                            .{ .spans = &[_]ExpectedSpan{.{ .name = "a", .start_time_us = 0, .duration_us = 1 }} },
                        },
                    },
                    .{
                        .tid = 2,
                        .tracks = &[_]ExpectedTrack{
                            .{ .spans = &[_]ExpectedSpan{.{ .name = "b", .start_time_us = 0, .duration_us = 2 }} },
                        },
                    },
                },
            },
        },
    });
}

test "parse, thread_sort_index equal, sort by name" {
    var sort_index_1 = try init_args_sort_index(1);
    defer deinit_args(&sort_index_1);
    var name_a = try init_args_name("thread a");
    defer deinit_args(&name_a);
    var name_b = try init_args_name("thread b");
    defer deinit_args(&name_b);
    var name_c = try init_args_name("thread c");
    defer deinit_args(&name_c);

    try test_parse(&[_]TraceEvent{
        .{ .ph = 'M', .name = "thread_name", .pid = 1, .tid = 1, .args = name_c },
        .{ .ph = 'M', .name = "thread_sort_index", .pid = 1, .tid = 1, .args = sort_index_1 },
        .{ .ph = 'M', .name = "thread_name", .pid = 1, .tid = 2, .args = name_b },
        .{ .ph = 'M', .name = "thread_sort_index", .pid = 1, .tid = 2, .args = sort_index_1 },
        .{ .ph = 'M', .name = "thread_name", .pid = 1, .tid = 3, .args = name_a },
        .{ .ph = 'M', .name = "thread_sort_index", .pid = 1, .tid = 3, .args = sort_index_1 },
        .{ .ph = 'X', .name = "c", .pid = 1, .tid = 1, .ts = 0, .dur = 1 },
        .{ .ph = 'X', .name = "b", .pid = 1, .tid = 2, .ts = 0, .dur = 2 },
        .{ .ph = 'X', .name = "a", .pid = 1, .tid = 3, .ts = 0, .dur = 3 },
    }, .{
        .processes = &[_]ExpectedProcess{
            .{
                .pid = 1,
                .threads = &[_]ExpectedThread{
                    .{
                        .tid = 3,
                        .name = "thread a",
                        .sort_index = 1,
                        .tracks = &[_]ExpectedTrack{
                            .{ .spans = &[_]ExpectedSpan{.{ .name = "a", .start_time_us = 0, .duration_us = 3 }} },
                        },
                    },
                    .{
                        .tid = 2,
                        .name = "thread b",
                        .sort_index = 1,
                        .tracks = &[_]ExpectedTrack{
                            .{ .spans = &[_]ExpectedSpan{.{ .name = "b", .start_time_us = 0, .duration_us = 2 }} },
                        },
                    },
                    .{
                        .tid = 1,
                        .name = "thread c",
                        .sort_index = 1,
                        .tracks = &[_]ExpectedTrack{
                            .{ .spans = &[_]ExpectedSpan{.{ .name = "c", .start_time_us = 0, .duration_us = 1 }} },
                        },
                    },
                },
            },
        },
    });
}

test "parse, set min/max" {
    var profile = try parse(&[_]TraceEvent{
        .{ .ph = 'X', .name = "a", .pid = 1, .tid = 3, .ts = 8, .dur = 5 },
    });
    defer profile.deinit();

    try std.testing.expectEqual(@as(i64, 8), profile.min_time_us);
    try std.testing.expectEqual(@as(i64, 13), profile.max_time_us);
}

test "parse, incomplete event, ignore for min/max" {
    var profile = try parse(&[_]TraceEvent{
        .{ .ph = 'X', .name = "a", .ts = 1, .dur = 20 },
        .{ .ph = 'X', .name = "a", .pid = 1, .tid = 3, .ts = 8, .dur = 5 },
    });
    defer profile.deinit();

    try std.testing.expectEqual(@as(i64, 8), profile.min_time_us);
    try std.testing.expectEqual(@as(i64, 13), profile.max_time_us);
}
