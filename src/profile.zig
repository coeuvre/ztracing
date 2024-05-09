const std = @import("std");
const test_utils = @import("test_utils.zig");
const json_profile_parser = @import("json_profile_parser.zig");
const tracy = @import("tracy.zig");

const Allocator = std.mem.Allocator;
const ArrayList = std.ArrayList;
const TraceEvent = json_profile_parser.TraceEvent;
const expect_optional = test_utils.expect_optional;
const Arena = std.heap.ArenaAllocator;
const AutoHashMap = std.AutoHashMap;
const SpanArgs = std.StringHashMap([:0]const u8);

const StringPool = struct {
    allocator: Allocator,
    pool: std.StringHashMap([:0]const u8),

    pub fn init(allocator: Allocator) StringPool {
        return .{
            .allocator = allocator,
            .pool = std.StringHashMap([:0]const u8).init(allocator),
        };
    }

    pub fn intern(self: *StringPool, str: []const u8) ![:0]const u8 {
        if (self.pool.get(str)) |s| {
            return s;
        }
        const new_str = try self.allocator.dupeZ(u8, str);
        try self.pool.put(new_str, new_str);
        return new_str;
    }
};

pub const UiState = struct {
    open: bool = true,
};

pub const SeriesValue = struct {
    time_us: i64,
    value: f64,

    fn less_than(_: void, lhs: SeriesValue, rhs: SeriesValue) bool {
        return lhs.time_us < rhs.time_us;
    }

    fn compare(_: void, cursor: i64, value: SeriesValue) std.math.Order {
        if (cursor == value.time_us) {
            return .eq;
        } else if (cursor < value.time_us) {
            return .lt;
        } else {
            return .gt;
        }
    }

    fn get_time_us(self: SeriesValue) i64 {
        return self.time_us;
    }
};

pub const Series = struct {
    name: [:0]const u8,
    /// Store different level of detail for the values in one series.
    /// mipmap[i] is at resolution 2^i us.
    /// mipmap[0] is at 1 us resolution which is the raw data.
    mipmap: []const []const SeriesValue,

    values: ArrayList(SeriesValue),

    fn init(allocator: Allocator, name: [:0]const u8) !Series {
        return .{
            .name = name,
            .mipmap = &.{},
            .values = ArrayList(SeriesValue).init(allocator),
        };
    }

    fn done(self: *Series) !void {
        std.sort.block(SeriesValue, self.values.items, {}, SeriesValue.less_than);

        self.mipmap = try generate_mipmap(
            SeriesValue,
            self.values.allocator,
            try self.values.toOwnedSlice(),
            SeriesValue.get_time_us,
        );
    }

    pub fn get_values(self: *const Series, start_time_us: i64, end_time_us: i64, min_duration_us: i64) []const SeriesValue {
        const lod: usize = @intCast(std.math.log2(highest_power_of_two_less_or_equal(@intCast(min_duration_us))));
        const values = self.mipmap[@min(lod, self.mipmap.len - 1)];
        const i = find_last_less_or_equal(SeriesValue, values, start_time_us, {}, SeriesValue.compare) orelse 0;
        if (i >= values.len) {
            return &.{};
        }
        var j = find_first_greater(SeriesValue, values, end_time_us, {}, SeriesValue.compare);
        if (j < values.len) {
            j += 1;
        }
        return values[i..j];
    }
};

fn generate_mipmap(
    comptime T: type,
    allocator: Allocator,
    lod0: []T,
    comptime get_value: fn (_: T) i64,
) ![]const []const T {
    const zone = tracy.trace(@src());
    defer zone.end();

    var mipmap = ArrayList([]const T).init(allocator);
    try mipmap.append(lod0);
    var min_duration_us: i64 = 1;
    var lod: usize = 0;
    while (true) {
        const current_values = mipmap.items[lod];
        if (current_values.len <= 1) {
            break;
        }

        min_duration_us <<= 1;
        // overflowing
        if (min_duration_us < 0) {
            break;
        }

        // first pass to check whether this level can merge values.
        // Otherwise, use the same slice to avoid memory allocations.
        var len: usize = 0;
        var last_bucket: i64 = 0;
        for (current_values) |value| {
            const current_bucket = @divTrunc(get_value(value), min_duration_us);
            if (len == 0 or current_bucket != last_bucket) {
                len += 1;
                last_bucket = current_bucket;
            }
        }

        var next_values: []T = @constCast(current_values);
        if (len != current_values.len) {
            std.debug.assert(len < current_values.len);
            next_values = try allocator.alloc(T, len);
            var index: usize = 0;
            for (current_values) |value| {
                if (index == 0) {
                    next_values[index] = value;
                    index += 1;
                    continue;
                }

                const bucket_for_current = @divTrunc(get_value(value), min_duration_us);
                const next_value = next_values[index - 1];
                const bucket_for_next = @divTrunc(get_value(next_value), min_duration_us);
                if (bucket_for_current != bucket_for_next) {
                    next_values[index] = value;
                    index += 1;
                } else {
                    if (get_value(value) > get_value(next_value)) {
                        // Use the larger value
                        next_values[index - 1] = value;
                    }
                }
            }
        }

        try mipmap.append(next_values);
        lod += 1;
    }

    return try mipmap.toOwnedSlice();
}

pub const Counter = struct {
    name: [:0]const u8,
    max_value: f64,
    series: ArrayList(Series),

    ui: UiState = .{},

    fn init(allocator: Allocator, name: [:0]const u8) Counter {
        return .{
            .name = name,
            .max_value = 0,
            .series = ArrayList(Series).init(allocator),
        };
    }

    fn add_sample(self: *Counter, time_us: i64, name: [:0]const u8, value: f64) !void {
        self.max_value = @max(self.max_value, value);
        var series = try self.get_or_create_series(name);
        try series.values.append(.{ .time_us = time_us, .value = value });
    }

    fn get_or_create_series(self: *Counter, name: [:0]const u8) !*Series {
        for (self.series.items) |*series| {
            if (std.mem.eql(u8, series.name, name)) {
                return series;
            }
        }

        try self.series.append(try Series.init(self.series.allocator, name));
        return &self.series.items[self.series.items.len - 1];
    }

    fn done(self: *Counter) !void {
        for (self.series.items) |*series| {
            try series.done();
        }
    }

    fn less_than(_: void, lhs: Counter, rhs: Counter) bool {
        return name_less_than(lhs.name, rhs.name);
    }
};

pub const Span = struct {
    name: [:0]const u8,
    start_time_us: i64,
    duration_us: i64,
    end_time_us: i64,
    args: SpanArgs,

    self_duration_us: i64 = 0,
    category: ?[:0]const u8 = null,

    fn less_than(_: void, lhs: Span, rhs: Span) bool {
        if (lhs.start_time_us == rhs.start_time_us) {
            return rhs.duration_us < lhs.duration_us;
        }

        return lhs.start_time_us < rhs.start_time_us;
    }

    fn get_start_time_us(self: *const Span) i64 {
        return self.start_time_us;
    }
};

pub const Track = struct {
    /// Store different level of detail for the spans in one track.
    /// mipmap[i] is at resolution 2^i us.
    /// mipmap[0] is at 1 us resolution which is the raw data.
    mipmap: []const []const *Span,

    spans: ArrayList(*Span),

    fn init(allocator: Allocator) !Track {
        return .{
            .mipmap = &[0][]const *Span{},
            .spans = ArrayList(*Span).init(allocator),
        };
    }

    fn append_span_raw(self: *Track, span: *Span) !void {
        try self.spans.append(span);
    }

    fn done(self: *Track) !void {
        self.mipmap = try generate_mipmap(
            *Span,
            self.spans.allocator,
            try self.spans.toOwnedSlice(),
            Span.get_start_time_us,
        );
    }

    pub fn get_spans(self: *const Track, start_time_us: i64, end_time_us: i64, min_duration_us: i64) []const *Span {
        const lod: usize = @intCast(std.math.log2(highest_power_of_two_less_or_equal(@intCast(min_duration_us))));
        const spans = self.mipmap[@min(lod, self.mipmap.len - 1)];
        const i = find_first_greater(*const Span, spans, start_time_us, {}, Track.compare_end_time_us);
        if (i >= spans.len) {
            return &.{};
        }
        var j = find_last_less_or_equal(*const Span, spans, end_time_us, {}, Track.compare_start_time_us) orelse i;
        if (j < i) {
            j = i;
        }
        return spans[i .. j + 1];
    }

    fn compare_start_time_us(_: void, cursor: i64, span: *const Span) std.math.Order {
        if (cursor == span.start_time_us) {
            return .eq;
        } else if (cursor < span.start_time_us) {
            return .lt;
        } else {
            return .gt;
        }
    }

    fn compare_end_time_us(_: void, cursor: i64, span: *const Span) std.math.Order {
        if (cursor == span.end_time_us) {
            return .eq;
        } else if (cursor < span.end_time_us) {
            return .lt;
        } else {
            return .gt;
        }
    }
};

fn highest_power_of_two_less_or_equal(val: u64) u64 {
    std.debug.assert(val >= 1);
    var result: u64 = 1;
    while (result <= val) {
        result <<= 1;
    }
    return result >> 1;
}

// Find the very first index of items where items[index] > key
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

// Find the very last index of items where items[index] <= key
fn find_last_less_or_equal(
    comptime T: type,
    items: []const T,
    key: anytype,
    context: anytype,
    comptime cmp: fn (context: @TypeOf(context), key: @TypeOf(key), mid_item: T) std.math.Order,
) ?usize {
    const index = find_first_greater(T, items, key, context, cmp);
    if (index > 0) {
        return index - 1;
    }
    return null;
}

pub const Thread = struct {
    allocator: Allocator,
    tid: i64,
    spans: ArrayList(Span),
    tracks: ArrayList(Track),

    name: ?[:0]const u8 = null,
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

    pub fn set_name(self: *Thread, name: [:0]const u8) !void {
        self.name = name;
    }

    pub fn create_span(self: *Thread, name: ?[:0]const u8, start_time_us: i64, duration_us: i64) !*Span {
        try self.spans.append(.{
            .name = name orelse "",
            .start_time_us = start_time_us,
            .duration_us = duration_us,
            .end_time_us = start_time_us + duration_us,
            .args = SpanArgs.init(self.allocator),
        });
        return &self.spans.items[self.spans.items.len - 1];
    }

    fn get_or_create_track(self: *Thread, level: usize) !*Track {
        while (level >= self.tracks.items.len) {
            try self.tracks.append(try Track.init(self.allocator));
        }
        return &self.tracks.items[level];
    }

    pub fn done(self: *Thread) !void {
        const zone = tracy.trace(@src());
        defer zone.end();

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

        for (self.tracks.items) |*track| {
            try track.done();
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
                try track.append_span_raw(span);
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
    thread_map: AutoHashMap(i64, usize),
    threads: ArrayList(Thread),

    fn init(allocator: Allocator, pid: i64) Process {
        return .{
            .allocator = allocator,
            .pid = pid,
            .counters = ArrayList(Counter).init(allocator),
            .thread_map = AutoHashMap(i64, usize).init(allocator),
            .threads = ArrayList(Thread).init(allocator),
        };
    }

    fn done(self: *Process) !void {
        std.sort.block(Counter, self.counters.items, {}, Counter.less_than);
        for (self.counters.items) |*counter| {
            try counter.done();
        }

        self.thread_map.clearAndFree();
        std.sort.block(Thread, self.threads.items, {}, Thread.less_than);
        for (self.threads.items) |*thread| {
            try thread.done();
        }
    }

    fn get_or_create_counter(self: *Process, name: [:0]const u8) !*Counter {
        for (self.counters.items) |*counter| {
            if (std.mem.eql(u8, counter.name, name)) {
                return counter;
            }
        }

        try self.counters.append(Counter.init(self.allocator, name));
        return &self.counters.items[self.counters.items.len - 1];
    }

    pub fn get_or_create_thread(self: *Process, tid: i64) !*Thread {
        if (self.thread_map.get(tid)) |index| {
            return &self.threads.items[index];
        }
        try self.threads.append(Thread.init(self.allocator, tid));
        const index = self.threads.items.len - 1;
        try self.thread_map.put(tid, index);
        return &self.threads.items[index];
    }
};

pub const Profile = struct {
    allocator: Allocator,
    string_pool: StringPool,
    min_time_us: i64,
    max_time_us: i64,
    processes: ArrayList(Process),

    pub fn init(allocator: Allocator) Profile {
        return .{
            .allocator = allocator,
            .string_pool = StringPool.init(allocator),
            .min_time_us = std.math.maxInt(i64),
            .max_time_us = 0,
            .processes = ArrayList(Process).init(allocator),
        };
    }

    fn get_or_create_process(self: *Profile, pid: i64) !*Process {
        for (self.processes.items) |*process| {
            if (process.pid == pid) {
                return process;
            }
        }

        try self.processes.append(Process.init(self.allocator, pid));
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
        var counter = try process.get_or_create_counter(try self.string_pool.intern(name));
        var iter = args.iterator();
        while (iter.next()) |entry| {
            const value_name = try self.string_pool.intern(entry.key_ptr.*);
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

        const name_ref = blk: {
            if (trace_event.name) |name| {
                break :blk try self.string_pool.intern(name);
            } else {
                break :blk null;
            }
        };
        var span = try thread.create_span(name_ref, ts, dur);

        if (trace_event.cat) |cat| {
            span.category = try self.string_pool.intern(cat);
        }

        if (trace_event.args) |args| switch (args) {
            .object => |obj| {
                var iter = obj.iterator();
                while (iter.next()) |entry| {
                    switch (entry.value_ptr.*) {
                        .string => |value| {
                            const key = try self.string_pool.intern(entry.key_ptr.*);
                            try span.args.put(key, try self.string_pool.intern(value));
                        },
                        else => {},
                    }
                }
            },
            else => {},
        };

        self.maybe_update_min_max(trace_event);
    }

    fn handle_thread_name(self: *Profile, pid: i64, tid: i64, name: []const u8) !void {
        var process = try self.get_or_create_process(pid);
        var thread = try process.get_or_create_thread(tid);
        try thread.set_name(try self.string_pool.intern(name));
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

fn parse(arena: *Arena, trace_events: []const TraceEvent) !Profile {
    var profile = Profile.init(arena.allocator());

    for (trace_events) |*trace_event| {
        try profile.handle_trace_event(trace_event);
    }

    try profile.done();

    return profile;
}

fn test_parse(trace_events: []const TraceEvent, expected_profile: ExpectedProfile) !void {
    var testing_arena = Arena.init(std.testing.allocator);
    const profile = try parse(&testing_arena, trace_events);
    defer testing_arena.deinit();
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
    try std.testing.expectEqual(expected.spans.len, actual.mipmap[0].len);

    for (expected.spans, actual.mipmap[0]) |expected_span, actual_span| {
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
    var testing_arena = Arena.init(std.testing.allocator);
    defer testing_arena.deinit();
    const profile = try parse(&testing_arena, &[_]TraceEvent{
        .{ .ph = 'X', .name = "a", .pid = 1, .tid = 3, .ts = 8, .dur = 5 },
    });

    try std.testing.expectEqual(@as(i64, 8), profile.min_time_us);
    try std.testing.expectEqual(@as(i64, 13), profile.max_time_us);
}

test "parse, incomplete event, ignore for min/max" {
    var testing_arena = Arena.init(std.testing.allocator);
    defer testing_arena.deinit();
    const profile = try parse(&testing_arena, &[_]TraceEvent{
        .{ .ph = 'X', .name = "a", .ts = 1, .dur = 20 },
        .{ .ph = 'X', .name = "a", .pid = 1, .tid = 3, .ts = 8, .dur = 5 },
    });

    try std.testing.expectEqual(@as(i64, 8), profile.min_time_us);
    try std.testing.expectEqual(@as(i64, 13), profile.max_time_us);
}
