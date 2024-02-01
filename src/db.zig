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

    const create_counter =
        \\CREATE TABLE counter (
        \\  id INTEGER PRIMARY KEY,
        \\  process_id INTEGER,
        \\  name TEXT,
        \\  max_value REAL
        \\);
    ;

    const insert_counter =
        \\INSERT INTO counter (process_id, name) VALUES (?, ?) RETURNING id;
    ;

    const create_series =
        \\CREATE TABLE series (
        \\  id INTEGER PRIMARY KEY,
        \\  counter_id INTEGER,
        \\  name TEXT
        \\);
    ;

    const insert_series =
        \\INSERT INTO series (counter_id, name) VALUES(?, ?) RETURNING id;
    ;

    const create_series_value =
        \\CREATE TABLE series_value (
        \\  series_id INTEGER,
        \\  time_us INTEGER,
        \\  value REAL
        \\);
        \\CREATE INDEX series_value_index_series_id ON series_value (series_id);
    ;

    const insert_series_value =
        \\INSERT INTO series_value (series_id, time_us, value) VALUES(?, ?, ?);
    ;

    const create_thread =
        \\CREATE TABLE thread (
        \\  id INTEGER PRIMARY KEY,
        \\  name TEXT,
        \\  sort_index INTEGER
        \\);
    ;

    const insert_thread =
        \\INSERT INTO thread (id) VALUES (?);
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

pub const ProfileDB = struct {
    const Self = @This();

    const Process = struct {
        counters: std.StringHashMap(Counter),
        threads: std.AutoHashMap(i64, void),

        fn init(allocator: Allocator) Process {
            return .{
                .counters = std.StringHashMap(Counter).init(allocator),
                .threads = std.AutoHashMap(i64, void).init(allocator),
            };
        }
    };

    const Counter = struct {
        id: i64,
        series: std.StringHashMap(Series),

        fn init(allocator: Allocator, id: i64) Counter {
            return .{
                .id = id,
                .series = std.StringHashMap(Series).init(allocator),
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

    allocator: Allocator,
    conn: *c.sqlite3,
    insert_process_stmt: *c.sqlite3_stmt,
    insert_counter_stmt: *c.sqlite3_stmt,
    insert_series_stmt: *c.sqlite3_stmt,
    insert_series_value_stmt: *c.sqlite3_stmt,
    insert_thread_stmt: *c.sqlite3_stmt,
    insert_span_stmt: *c.sqlite3_stmt,

    processes: std.AutoHashMap(i64, Process),

    min_time_us: i64,
    max_time_us: i64,

    pub fn init(allocator: Allocator) !ProfileDB {
        const conn = blk: {
            var conn: ?*c.sqlite3 = null;
            if (c.sqlite3_open("test.db", &conn) != c.SQLITE_OK) {
                return error.sqlite3_open;
            }
            break :blk conn.?;
        };
        errdefer {
            const ret = c.sqlite3_close(conn);
            std.debug.assert(ret == c.SQLITE_OK);
        }

        try sqlite3.exec(conn, "BEGIN");
        try sqlite3.exec(conn, sql.create_process);
        try sqlite3.exec(conn, sql.create_counter);
        try sqlite3.exec(conn, sql.create_series);
        try sqlite3.exec(conn, sql.create_series_value);
        try sqlite3.exec(conn, sql.create_thread);
        try sqlite3.exec(conn, sql.create_span);

        const insert_process_stmt = try sqlite3.prepare(conn, sql.insert_process);
        errdefer sqlite3.finalize(insert_process_stmt);

        const insert_counter_stmt = try sqlite3.prepare(conn, sql.insert_counter);
        errdefer sqlite3.finalize(insert_counter_stmt);

        const insert_series_stmt = try sqlite3.prepare(conn, sql.insert_series);
        errdefer sqlite3.finalize(insert_series_stmt);

        const insert_series_value_stmt = try sqlite3.prepare(conn, sql.insert_series_value);
        errdefer sqlite3.finalize(insert_series_value_stmt);

        const insert_thread_stmt = try sqlite3.prepare(conn, sql.insert_thread);
        errdefer sqlite3.finalize(insert_thread_stmt);

        const insert_span_stmt = try sqlite3.prepare(conn, sql.insert_span);
        errdefer sqlite3.finalize(insert_span_stmt);

        return .{
            .allocator = allocator,
            .conn = conn,
            .insert_process_stmt = insert_process_stmt,
            .insert_counter_stmt = insert_counter_stmt,
            .insert_series_stmt = insert_series_stmt,
            .insert_series_value_stmt = insert_series_value_stmt,
            .insert_thread_stmt = insert_thread_stmt,
            .insert_span_stmt = insert_span_stmt,

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
            else => {},
        }
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

    fn maybe_insert_thread(self: *Self, process: *Process, tid: i64) !void {
        const result = try process.threads.getOrPut(tid);
        if (result.found_existing) {
            return;
        }

        const conn = self.conn;
        const stmt = self.insert_thread_stmt;

        defer sqlite3.reset(conn, stmt) catch unreachable;

        try sqlite3.bind_int64(conn, stmt, 1, tid);
        _ = try sqlite3.step(conn, stmt);
    }

    fn handle_complete_event(self: *Self, trace_event: *const TraceEvent, pid: i64, tid: i64, ts: i64, dur: i64) !void {
        const process = try self.maybe_insert_process(pid);
        try self.maybe_insert_thread(process, tid);
        try self.insert_span(pid, tid, trace_event.name orelse "", trace_event.cat orelse "", ts, dur);
    }

    pub fn done(self: *Self) !void {
        try sqlite3.exec(self.conn, "COMMIT");
    }
};

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

    pub inline fn bind_text_static(conn: *c.sqlite3, stmt: *c.sqlite3_stmt, index: c_int, value: []const u8) !void {
        const errcode = c.sqlite3_bind_text(stmt, index, value.ptr, @intCast(value.len), c.SQLITE_STATIC);
        if (errcode != c.SQLITE_OK) {
            std.log.err("{s}: {s}", .{ c.sqlite3_errstr(errcode), c.sqlite3_errmsg(conn) });
            return error.sqlite3_bind;
        }
    }

    pub inline fn bind_int64(conn: *c.sqlite3, stmt: *c.sqlite3_stmt, index: c_int, value: c.sqlite3_int64) !void {
        const errcode = c.sqlite3_bind_int64(stmt, index, value);
        if (errcode != c.SQLITE_OK) {
            std.log.err("{s}: {s}", .{ c.sqlite3_errstr(errcode), c.sqlite3_errmsg(conn) });
            return error.sqlite3_bind;
        }
    }

    pub inline fn bind_double(conn: *c.sqlite3, stmt: *c.sqlite3_stmt, index: c_int, value: f64) !void {
        const errcode = c.sqlite3_bind_double(stmt, index, value);
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
};
