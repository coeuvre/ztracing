const std = @import("std");
const c = @import("c.zig");

const Allocator = std.mem.Allocator;
const TraceEvent = @import("json_profile_parser.zig").TraceEvent;

const sql = struct {
    const create_process =
        \\CREATE TABLE process (
        \\  id INTEGER PRIMARY KEY
        \\);
    ;

    const insert_process =
        \\INSERT INTO process (id) VALUES (?);
    ;

    const select_process =
        \\SELECT id FROM process;
    ;

    const create_counter =
        \\CREATE TABLE counter (
        \\  id INTEGER PRIMARY KEY,
        \\  process_id INTEGER,
        \\  name TEXT,
        \\  max_value REAL
        \\);
        \\CREATE INDEX index_counter_process_id ON counter (process_id);
    ;

    const insert_counter =
        \\INSERT INTO counter (process_id, name) VALUES (?, ?) RETURNING id;
    ;

    const update_counter_max_value =
        \\UPDATE counter SET max_value = ? WHERE id = ?;
    ;

    const select_counter =
        \\SELECT id, name, max_value FROM counter WHERE process_id = ?;
    ;

    const create_series =
        \\CREATE TABLE series (
        \\  id INTEGER PRIMARY KEY,
        \\  counter_id INTEGER,
        \\  name TEXT
        \\);
        \\CREATE INDEX index_series_counter_id ON series (counter_id);
    ;

    const insert_series =
        \\INSERT INTO series (counter_id, name) VALUES(?, ?) RETURNING id;
    ;

    const select_series =
        \\SELECT id, name FROM series WHERE counter_id = ?;
    ;

    const create_series_value =
        \\CREATE TABLE series_value (
        \\  series_id INTEGER,
        \\  time_us INTEGER,
        \\  value REAL
        \\);
        \\CREATE INDEX index_series_value_series_id_time_us ON series_value (series_id, time_us);
    ;

    const insert_series_value =
        \\INSERT INTO series_value (series_id, time_us, value) VALUES(?, ?, ?);
    ;

    const select_series_value_lower_bound =
        \\SELECT time_us, value
        \\FROM series_value
        \\WHERE series_id = ?1 AND time_us < ?2
        \\ORDER BY time_us DESC
        \\LIMIT 1;
    ;

    const select_series_value_upper_bound =
        \\SELECT time_us, value
        \\FROM series_value
        \\WHERE series_id = ?1 AND time_us >= ?2
        \\ORDER BY time_us ASC
        \\LIMIT 1;
    ;

    const create_thread =
        \\CREATE TABLE thread (
        \\  id INTEGER PRIMARY KEY,
        \\  name TEXT,
        \\  sort_index INTEGER
        \\);
        \\CREATE INDEX index_thread_sort_index ON thread (sort_index);
    ;

    const insert_thread =
        \\INSERT INTO thread (id) VALUES (?);
    ;

    const update_thread_name =
        \\UPDATE thread SET name = ? WHERE id = ?;
    ;

    const update_thread_sort_index =
        \\UPDATE thread SET sort_index = ? WHERE id = ?;
    ;

    const create_span =
        \\CREATE TABLE span (
        \\  id INTEGER PRIMARY KEY,
        \\  process_id INTEGER,
        \\  thread_id INTEGER,
        \\  name TEXT,
        \\  category TEXT,
        \\  start_time_us INTEGER,
        \\  duration_us INTEGER,
        \\  end_time_us INTEGER,
        \\  self_duration_us INTEGER
        \\);
    ;

    const insert_span =
        \\INSERT INTO span (process_id, thread_id, name, category, start_time_us, duration_us, end_time_us, self_duration_us) VALUES
        \\ (?, ?, ?, ?, ?, ?, ?, 0);
    ;
};

pub const ProfileBuilder = struct {
    const Self = @This();

    const Process = struct {
        counters: std.StringHashMap(Counter),
        threads: std.AutoHashMap(i64, Thread),

        fn init(allocator: Allocator) Process {
            return .{
                .counters = std.StringHashMap(Counter).init(allocator),
                .threads = std.AutoHashMap(i64, Thread).init(allocator),
            };
        }
    };

    const Counter = struct {
        id: i64,
        series: std.StringHashMap(Series),
        max_value: f64,

        fn init(allocator: Allocator, id: i64) Counter {
            return .{
                .id = id,
                .series = std.StringHashMap(Series).init(allocator),
                .max_value = 0,
            };
        }
    };

    const Series = struct {
        id: i64,

        fn init(id: i64) Series {
            return .{
                .id = id,
            };
        }
    };

    const Thread = struct {
        id: i64,

        fn init(id: i64) Thread {
            return .{
                .id = id,
            };
        }
    };

    allocator: Allocator,
    conn: *c.sqlite3,

    insert_process_stmt: *c.sqlite3_stmt,
    insert_counter_stmt: *c.sqlite3_stmt,
    update_counter_max_value_stmt: *c.sqlite3_stmt,
    insert_series_stmt: *c.sqlite3_stmt,
    insert_series_value_stmt: *c.sqlite3_stmt,
    insert_thread_stmt: *c.sqlite3_stmt,
    update_thread_name_stmt: *c.sqlite3_stmt,
    update_thread_sort_index_stmt: *c.sqlite3_stmt,
    insert_span_stmt: *c.sqlite3_stmt,

    processes: std.AutoHashMap(i64, Process),

    min_time_us: i64,
    max_time_us: i64,

    pub fn init(allocator: Allocator) !Self {
        const conn = blk: {
            var conn: ?*c.sqlite3 = null;
            if (c.sqlite3_open("test.db", &conn) != c.SQLITE_OK) {
                return error.sqlite3_open;
            }
            break :blk conn.?;
        };

        sqlite3.exec(conn, "BEGIN") catch unreachable;
        sqlite3.exec(conn, sql.create_process) catch unreachable;
        sqlite3.exec(conn, sql.create_counter) catch unreachable;
        sqlite3.exec(conn, sql.create_series) catch unreachable;
        sqlite3.exec(conn, sql.create_series_value) catch unreachable;
        sqlite3.exec(conn, sql.create_thread) catch unreachable;
        sqlite3.exec(conn, sql.create_span) catch unreachable;

        return .{
            .allocator = allocator,
            .conn = conn,

            .insert_process_stmt = sqlite3.prepare(conn, sql.insert_process) catch unreachable,
            .insert_counter_stmt = sqlite3.prepare(conn, sql.insert_counter) catch unreachable,
            .update_counter_max_value_stmt = sqlite3.prepare(conn, sql.update_counter_max_value) catch unreachable,
            .insert_series_stmt = sqlite3.prepare(conn, sql.insert_series) catch unreachable,
            .insert_series_value_stmt = sqlite3.prepare(conn, sql.insert_series_value) catch unreachable,
            .insert_thread_stmt = sqlite3.prepare(conn, sql.insert_thread) catch unreachable,
            .update_thread_name_stmt = sqlite3.prepare(conn, sql.update_thread_name) catch unreachable,
            .update_thread_sort_index_stmt = sqlite3.prepare(conn, sql.update_thread_sort_index) catch unreachable,
            .insert_span_stmt = sqlite3.prepare(conn, sql.insert_span) catch unreachable,

            .processes = std.AutoHashMap(i64, Process).init(allocator),

            .min_time_us = std.math.maxInt(i64),
            .max_time_us = 0,
        };
    }

    pub fn handle_trace_event(self: *Self, trace_event: *const TraceEvent) !void {
        switch (trace_event.ph orelse 0) {
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

    fn update_thread_name(self: *Self, thread: *Thread, name: []const u8) !void {
        const conn = self.conn;
        const stmt = self.update_thread_name_stmt;

        defer sqlite3.reset(conn, stmt) catch unreachable;

        try sqlite3.bind_text_static(conn, stmt, 1, name);
        try sqlite3.bind_int64(conn, stmt, 2, thread.id);

        _ = try sqlite3.step(conn, stmt);
    }

    fn update_thread_sort_index(self: *Self, thread: *Thread, sort_index: i64) !void {
        const conn = self.conn;
        const stmt = self.update_thread_sort_index_stmt;

        defer sqlite3.reset(conn, stmt) catch unreachable;

        try sqlite3.bind_int64(conn, stmt, 1, sort_index);
        try sqlite3.bind_int64(conn, stmt, 2, thread.id);

        _ = try sqlite3.step(conn, stmt);
    }

    fn handle_thread_name(self: *Self, pid: i64, tid: i64, name: []const u8) !void {
        const process = try self.maybe_insert_process(pid);
        const thread = try self.maybe_insert_thread(process, tid);

        try self.update_thread_name(thread, name);
    }

    fn handle_thread_sort_index(self: *Self, pid: i64, tid: i64, sort_index: i64) !void {
        const process = try self.maybe_insert_process(pid);
        const thread = try self.maybe_insert_thread(process, tid);

        try self.update_thread_sort_index(thread, sort_index);
    }

    fn update_min_max_time(self: *Self, trace_event: *const TraceEvent) void {
        if (trace_event.ts) |ts| {
            self.min_time_us = @min(self.min_time_us, ts);
            var end = ts;
            if (trace_event.dur) |dur| {
                end += dur;
            }
            self.max_time_us = @max(self.max_time_us, end);
        }
    }

    fn maybe_insert_counter(self: *Self, pid: i64, name: []const u8, process: *Process) !*Counter {
        if (process.counters.getPtr(name)) |v| {
            return v;
        }

        const conn = self.conn;
        const stmt = self.insert_counter_stmt;

        defer sqlite3.reset(conn, stmt) catch unreachable;

        try sqlite3.bind_int64(conn, stmt, 1, pid);
        try sqlite3.bind_text_static(conn, stmt, 2, name);

        _ = try sqlite3.step(conn, stmt);

        const id = c.sqlite3_column_int64(stmt, 0);

        const entry = try process.counters.getOrPut(try self.allocator.dupe(u8, name));
        std.debug.assert(!entry.found_existing);
        entry.value_ptr.* = Counter.init(self.allocator, id);
        return entry.value_ptr;
    }

    fn maybe_create_series(self: *Self, counter: *Counter, name: []const u8) !*Series {
        if (counter.series.getPtr(name)) |v| {
            return v;
        }

        const conn = self.conn;
        const stmt = self.insert_series_stmt;

        defer sqlite3.reset(conn, stmt) catch unreachable;

        try sqlite3.bind_int64(conn, stmt, 1, counter.id);
        try sqlite3.bind_text_static(conn, stmt, 2, name);

        _ = try sqlite3.step(conn, stmt);

        const id = c.sqlite3_column_int64(stmt, 0);

        const entry = try counter.series.getOrPut(try self.allocator.dupe(u8, name));
        std.debug.assert(!entry.found_existing);
        entry.value_ptr.* = Series.init(id);
        return entry.value_ptr;
    }

    fn insert_series_value(self: *Self, counter: *Counter, ts: i64, name: []const u8, value: f64) !void {
        const series = try self.maybe_create_series(counter, name);

        const conn = self.conn;
        const stmt = self.insert_series_value_stmt;

        defer sqlite3.reset(conn, stmt) catch unreachable;

        try sqlite3.bind_int64(conn, stmt, 1, series.id);
        try sqlite3.bind_int64(conn, stmt, 2, ts);
        try sqlite3.bind_double(conn, stmt, 3, value);

        _ = try sqlite3.step(conn, stmt);

        counter.max_value = @max(counter.max_value, value);
    }

    fn handle_counter_event(self: *Self, trace_event: *const TraceEvent, pid: i64, name: []const u8, ts: i64, args: std.json.ObjectMap) !void {
        const process = try self.maybe_insert_process(pid);
        const counter = try self.maybe_insert_counter(pid, name, process);

        var iter = args.iterator();
        while (iter.next()) |entry| {
            const value_name = entry.key_ptr.*;
            switch (entry.value_ptr.*) {
                .string, .number_string => |num| {
                    const val = try std.fmt.parseFloat(f64, num);
                    try self.insert_series_value(counter, ts, value_name, val);
                    self.update_min_max_time(trace_event);
                },
                .float => |val| {
                    try self.insert_series_value(counter, ts, value_name, val);
                    self.update_min_max_time(trace_event);
                },
                .integer => |val| {
                    try self.insert_series_value(counter, ts, value_name, @floatFromInt(val));
                    self.update_min_max_time(trace_event);
                },
                else => {},
            }
        }
    }

    fn insert_span(self: *Self, pid: i64, tid: i64, name: []const u8, category: []const u8, start_time_us: i64, duration_us: i64) !void {
        const conn = self.conn;
        const stmt = self.insert_span_stmt;

        defer sqlite3.reset(conn, stmt) catch unreachable;

        try sqlite3.bind_int64(conn, stmt, 1, pid);
        try sqlite3.bind_int64(conn, stmt, 2, tid);
        try sqlite3.bind_text_static(conn, stmt, 3, name);
        try sqlite3.bind_text_static(conn, stmt, 4, category);
        try sqlite3.bind_int64(conn, stmt, 5, start_time_us);
        try sqlite3.bind_int64(conn, stmt, 6, duration_us);
        const end_time_us = start_time_us + duration_us;
        try sqlite3.bind_int64(conn, stmt, 7, end_time_us);

        _ = try sqlite3.step(conn, stmt);
    }

    fn maybe_insert_process(self: *Self, pid: i64) !*Process {
        const result = try self.processes.getOrPut(pid);
        if (result.found_existing) {
            return result.value_ptr;
        }

        const conn = self.conn;
        const stmt = self.insert_process_stmt;

        defer sqlite3.reset(conn, stmt) catch unreachable;

        try sqlite3.bind_int64(conn, stmt, 1, pid);
        _ = try sqlite3.step(conn, stmt);

        result.value_ptr.* = Process.init(self.allocator);
        return result.value_ptr;
    }

    fn maybe_insert_thread(self: *Self, process: *Process, tid: i64) !*Thread {
        const result = try process.threads.getOrPut(tid);
        if (result.found_existing) {
            return result.value_ptr;
        }

        const conn = self.conn;
        const stmt = self.insert_thread_stmt;

        defer sqlite3.reset(conn, stmt) catch unreachable;

        try sqlite3.bind_int64(conn, stmt, 1, tid);
        _ = try sqlite3.step(conn, stmt);

        result.value_ptr.* = Thread.init(tid);

        return result.value_ptr;
    }

    fn handle_complete_event(self: *Self, trace_event: *const TraceEvent, pid: i64, tid: i64, ts: i64, dur: i64) !void {
        const process = try self.maybe_insert_process(pid);
        _ = try self.maybe_insert_thread(process, tid);
        try self.insert_span(pid, tid, trace_event.name orelse "", trace_event.cat orelse "", ts, dur);
    }

    fn update_counter_max_value(self: *Self, counter: *Counter) !void {
        const conn = self.conn;
        const stmt = self.update_counter_max_value_stmt;

        defer sqlite3.reset(conn, stmt) catch unreachable;

        try sqlite3.bind_double(conn, stmt, 1, counter.max_value);
        try sqlite3.bind_int64(conn, stmt, 2, counter.id);

        _ = try sqlite3.step(conn, stmt);
    }

    pub fn build(self: *Self) !Profile {
        var process_iter = self.processes.valueIterator();
        while (process_iter.next()) |process| {
            var counter_iter = process.counters.valueIterator();
            while (counter_iter.next()) |counter| {
                try self.update_counter_max_value(counter);
            }
        }

        try sqlite3.exec(self.conn, "COMMIT");

        sqlite3.finalize(self.insert_process_stmt);
        sqlite3.finalize(self.insert_counter_stmt);
        sqlite3.finalize(self.update_counter_max_value_stmt);
        sqlite3.finalize(self.insert_series_stmt);
        sqlite3.finalize(self.insert_series_value_stmt);
        sqlite3.finalize(self.insert_thread_stmt);
        sqlite3.finalize(self.update_thread_name_stmt);
        sqlite3.finalize(self.update_thread_sort_index_stmt);
        sqlite3.finalize(self.insert_span_stmt);

        return try Profile.init(self);
    }
};

pub const Profile = struct {
    const Self = @This();

    allocator: Allocator,
    conn: *c.sqlite3,

    select_process_stmt: *c.sqlite3_stmt,
    select_counter_stmt: *c.sqlite3_stmt,
    select_series_stmt: *c.sqlite3_stmt,
    select_series_value_lower_bound_stmt: *c.sqlite3_stmt,
    select_series_value_upper_bound_stmt: *c.sqlite3_stmt,

    min_time_us: i64,
    max_time_us: i64,

    fn init(builder: *ProfileBuilder) !Profile {
        const conn = builder.conn;

        return .{
            .allocator = builder.allocator,
            .conn = builder.conn,

            .select_process_stmt = sqlite3.prepare(conn, sql.select_process) catch unreachable,
            .select_counter_stmt = sqlite3.prepare(conn, sql.select_counter) catch unreachable,
            .select_series_stmt = sqlite3.prepare(conn, sql.select_series) catch unreachable,
            .select_series_value_lower_bound_stmt = sqlite3.prepare(conn, sql.select_series_value_lower_bound) catch unreachable,
            .select_series_value_upper_bound_stmt = sqlite3.prepare(conn, sql.select_series_value_upper_bound) catch unreachable,

            .min_time_us = builder.min_time_us,
            .max_time_us = builder.max_time_us,
        };
    }

    pub const Process = struct {
        id: i64,
    };

    const ProcessIter = struct {
        conn: *c.sqlite3,
        stmt: *c.sqlite3_stmt,

        fn init(conn: *c.sqlite3, stmt: *c.sqlite3_stmt) ProcessIter {
            return .{
                .conn = conn,
                .stmt = stmt,
            };
        }

        pub fn deinit(self: *ProcessIter) void {
            sqlite3.reset(self.conn, self.stmt) catch unreachable;
        }

        pub fn next(self: *ProcessIter) ?Process {
            if (sqlite3.step(self.conn, self.stmt) catch unreachable == c.SQLITE_DONE) {
                return null;
            }

            const id = c.sqlite3_column_int64(self.stmt, 0);
            return .{
                .id = id,
            };
        }
    };

    pub fn iter_process(self: *Self) ProcessIter {
        return ProcessIter.init(self.conn, self.select_process_stmt);
    }

    pub const Counter = struct {
        id: i64,
        name: [:0]const u8,
        max_value: f64,
    };

    const CounterIter = struct {
        conn: *c.sqlite3,
        stmt: *c.sqlite3_stmt,

        fn init(conn: *c.sqlite3, stmt: *c.sqlite3_stmt, process_id: i64) CounterIter {
            sqlite3.bind_int64(conn, stmt, 1, process_id) catch unreachable;
            return .{
                .conn = conn,
                .stmt = stmt,
            };
        }

        pub fn deinit(self: *CounterIter) void {
            sqlite3.reset(self.conn, self.stmt) catch unreachable;
        }

        pub fn next(self: *CounterIter) ?Counter {
            if (sqlite3.step(self.conn, self.stmt) catch unreachable == c.SQLITE_DONE) {
                return null;
            }

            return .{
                .id = c.sqlite3_column_int64(self.stmt, 0),
                .name = sqlite3.column_text(self.stmt, 1),
                .max_value = c.sqlite3_column_double(self.stmt, 2),
            };
        }
    };

    pub fn iter_counter(self: *Self, process_id: i64) CounterIter {
        return CounterIter.init(self.conn, self.select_counter_stmt, process_id);
    }

    pub const Series = struct {
        id: i64,
        name: [:0]const u8,
    };

    const SeriesIter = struct {
        conn: *c.sqlite3,
        stmt: *c.sqlite3_stmt,

        fn init(conn: *c.sqlite3, stmt: *c.sqlite3_stmt, counter_id: i64) SeriesIter {
            sqlite3.bind_int64(conn, stmt, 1, counter_id) catch unreachable;
            return .{
                .conn = conn,
                .stmt = stmt,
            };
        }

        pub fn deinit(self: *SeriesIter) void {
            sqlite3.reset(self.conn, self.stmt) catch unreachable;
        }

        pub fn next(self: *SeriesIter) ?Series {
            if (sqlite3.step(self.conn, self.stmt) catch unreachable == c.SQLITE_DONE) {
                return null;
            }

            return .{
                .id = c.sqlite3_column_int64(self.stmt, 0),
                .name = sqlite3.column_text(self.stmt, 1),
            };
        }
    };

    pub fn iter_series(self: *Self, counter_id: i64) SeriesIter {
        return SeriesIter.init(self.conn, self.select_series_stmt, counter_id);
    }

    pub const SeriesValue = struct {
        time_us: i64,
        value: f64,
    };

    const SeriesValueIter = struct {
        conn: *c.sqlite3,
        lower_bound_stmt: *c.sqlite3_stmt,
        upper_bound_stmt: *c.sqlite3_stmt,
        series_id: i64,
        cursor: i64,
        is_first: bool,
        end_time_us: i64,
        resolution: i64,

        fn init(
            conn: *c.sqlite3,
            lower_bound_stmt: *c.sqlite3_stmt,
            upper_bound_stmt: *c.sqlite3_stmt,
            series_id: i64,
            start_time_us: i64,
            end_time_us: i64,
            resolution: i64,
        ) SeriesValueIter {
            return .{
                .conn = conn,
                .lower_bound_stmt = lower_bound_stmt,
                .upper_bound_stmt = upper_bound_stmt,
                .series_id = series_id,
                .cursor = start_time_us,
                .is_first = false,
                .end_time_us = end_time_us,
                .resolution = resolution,
            };
        }

        pub fn deinit(self: *SeriesValueIter) void {
            _ = self;
        }

        pub fn next(self: *SeriesValueIter) ?SeriesValue {
            if (self.cursor > self.end_time_us) {
                return null;
            }

            if (!self.is_first) {
                self.is_first = true;
                if (SeriesValueIter.query(self.conn, self.lower_bound_stmt, self.series_id, self.cursor)) |v| {
                    return v;
                }
            }

            if (SeriesValueIter.query(self.conn, self.upper_bound_stmt, self.series_id, self.cursor)) |v| {
                self.cursor = v.time_us - @rem(v.time_us, self.resolution) + self.resolution;
                return v;
            }

            return null;
        }

        fn query(conn: *c.sqlite3, stmt: *c.sqlite3_stmt, series_id: i64, time_us: i64) ?SeriesValue {
            defer sqlite3.reset(conn, stmt) catch unreachable;
            sqlite3.bind_int64(conn, stmt, 1, series_id) catch unreachable;
            sqlite3.bind_int64(conn, stmt, 2, time_us) catch unreachable;
            if (sqlite3.step(conn, stmt) catch unreachable == c.SQLITE_DONE) {
                return null;
            }
            return .{
                .time_us = c.sqlite3_column_int64(stmt, 0),
                .value = c.sqlite3_column_double(stmt, 1),
            };
        }
    };

    pub fn iter_series_value(self: *Self, series_id: i64, start_time_us: i64, end_time_us: i64, min_duration_us: i64) SeriesValueIter {
        const resolution: i64 = @intCast(highest_power_of_two_less_or_equal(@intCast(min_duration_us)));
        const start = start_time_us - @rem(start_time_us, resolution);
        const end = end_time_us - @rem(end_time_us, resolution) + resolution;
        return SeriesValueIter.init(
            self.conn,
            self.select_series_value_lower_bound_stmt,
            self.select_series_value_upper_bound_stmt,
            series_id,
            start,
            end,
            resolution,
        );
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

pub const sqlite3 = struct {
    pub fn exec(conn: *c.sqlite3, stmt: [:0]const u8) !void {
        var errmsg: [*c]u8 = null;
        const errcode = c.sqlite3_exec(conn, stmt, null, null, &errmsg);
        if (errcode != c.SQLITE_OK) {
            std.log.err("{s}: {s}", .{ c.sqlite3_errstr(errcode), errmsg });
            c.sqlite3_free(errmsg);
            return error.sqlite3_exec;
        }
    }

    pub fn prepare(conn: *c.sqlite3, stmt: [:0]const u8) !*c.sqlite3_stmt {
        var r: ?*c.sqlite3_stmt = null;
        const errcode = c.sqlite3_prepare_v2(conn, stmt, @intCast(stmt.len), &r, null);
        if (errcode != c.SQLITE_OK) {
            std.log.err("{s}: {s}", .{ c.sqlite3_errstr(errcode), c.sqlite3_errmsg(conn) });
            return error.sqlite3_prepare;
        }
        return r.?;
    }

    pub fn finalize(stmt: *c.sqlite3_stmt) void {
        const errcode = c.sqlite3_finalize(stmt);
        std.debug.assert(errcode == c.SQLITE_OK);
    }

    pub fn reset(conn: *c.sqlite3, stmt: *c.sqlite3_stmt) !void {
        const errcode = c.sqlite3_reset(stmt);
        if (errcode != c.SQLITE_OK) {
            std.log.err("{s}: {s}", .{ c.sqlite3_errstr(errcode), c.sqlite3_errmsg(conn) });
            return error.sqlite3_reset;
        }
    }

    pub inline fn bind_text_static(conn: *c.sqlite3, stmt: *c.sqlite3_stmt, col: c_int, value: []const u8) !void {
        const errcode = c.sqlite3_bind_text(stmt, col, value.ptr, @intCast(value.len), c.SQLITE_STATIC);
        if (errcode != c.SQLITE_OK) {
            std.log.err("{s}: {s}", .{ c.sqlite3_errstr(errcode), c.sqlite3_errmsg(conn) });
            return error.sqlite3_bind;
        }
    }

    pub inline fn bind_int64(conn: *c.sqlite3, stmt: *c.sqlite3_stmt, col: c_int, value: c.sqlite3_int64) !void {
        const errcode = c.sqlite3_bind_int64(stmt, col, value);
        if (errcode != c.SQLITE_OK) {
            std.log.err("{s}: {s}", .{ c.sqlite3_errstr(errcode), c.sqlite3_errmsg(conn) });
            return error.sqlite3_bind;
        }
    }

    pub inline fn bind_double(conn: *c.sqlite3, stmt: *c.sqlite3_stmt, col: c_int, value: f64) !void {
        const errcode = c.sqlite3_bind_double(stmt, col, value);
        if (errcode != c.SQLITE_OK) {
            std.log.err("{s}: {s}", .{ c.sqlite3_errstr(errcode), c.sqlite3_errmsg(conn) });
            return error.sqlite3_bind;
        }
    }

    pub inline fn step(conn: *c.sqlite3, stmt: *c.sqlite3_stmt) !c_int {
        const errcode = c.sqlite3_step(stmt);
        switch (errcode) {
            c.SQLITE_DONE,
            c.SQLITE_ROW,
            => return errcode,

            else => {
                std.log.err("{s}: {s}", .{ c.sqlite3_errstr(errcode), c.sqlite3_errmsg(conn) });
                return error.sqlite3_step;
            },
        }
    }

    pub inline fn column_text(stmt: *c.sqlite3_stmt, col: c_int) [:0]const u8 {
        const len = c.sqlite3_column_bytes(stmt, col);
        const ptr = c.sqlite3_column_text(stmt, col);
        return @ptrCast(ptr[0..@intCast(len + 1)]);
    }
};
