const std = @import("std");
const c = @import("c.zig");

const Allocator = std.mem.Allocator;
const TraceEvent = @import("json_profile_parser.zig").TraceEvent;

const sql = struct {
    const create_spans =
        \\CREATE TABLE spans (
        \\  id INTEGER PRIMARY KEY,
        \\  pid INTEGER,
        \\  tid INTEGER,
        \\  name TEXT,
        \\  category TEXT,
        \\  start_time_us INTEGER,
        \\  duration_us INTEGER,
        \\  end_time_us INTEGER,
        \\  self_duration_us INTEGER
        \\);
        \\
    ;

    const insert_spans =
        \\INSERT INTO spans (pid, tid, name, category, start_time_us, duration_us, end_time_us, self_duration_us) VALUES
        \\ (?, ?, ?, ?, ?, ?, ?, 0);
        \\
    ;
};

pub const ProfileDB = struct {
    const Self = @This();

    allocator: Allocator,
    conn: *c.sqlite3,
    insert_spans_stmt: *c.sqlite3_stmt,

    pub fn init(allocator: Allocator) !ProfileDB {
        var conn: ?*c.sqlite3 = null;
        if (c.sqlite3_open(":memory:", &conn) != c.SQLITE_OK) {
            return error.sqlite3_open;
        }
        errdefer {
            const ret = c.sqlite3_close(conn);
            std.debug.assert(ret == c.SQLITE_OK);
        }

        var errmsg: [*c]u8 = null;
        if (c.sqlite3_exec(conn, sql.create_spans, null, null, &errmsg) != c.SQLITE_OK) {
            std.log.err("{s}", .{errmsg});
            c.sqlite3_free(errmsg);
            return error.sqlite3_exec;
        }

        var insert_spans_stmt: ?*c.sqlite3_stmt = null;
        if (c.sqlite3_prepare_v2(conn, sql.insert_spans, sql.insert_spans.len, &insert_spans_stmt, null) != c.SQLITE_OK) {
            return error.sqlite3_prepare;
        }
        errdefer {
            const ret = c.sqlite3_finalize(insert_spans_stmt);
            std.debug.assert(ret == c.SQLITE_OK);
        }

        return .{
            .allocator = allocator,
            .conn = conn.?,
            .insert_spans_stmt = insert_spans_stmt.?,
        };
    }

    pub fn handle_trace_event(self: *Self, trace_event: *const TraceEvent) !void {
        switch (trace_event.ph orelse 0) {
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

    fn insert_span(self: *Self, pid: i64, tid: i64, name: []const u8, category: []const u8, start_time_us: i64, duration_us: i64) !void {
        defer _ = c.sqlite3_reset(self.insert_spans_stmt);

        const end_time_us = start_time_us + duration_us;

        if (c.sqlite3_bind_int64(self.insert_spans_stmt, 1, pid) != c.SQLITE_OK) {
            return error.sqlite3_bind;
        }
        if (c.sqlite3_bind_int64(self.insert_spans_stmt, 1, tid) != c.SQLITE_OK) {
            return error.sqlite3_bind;
        }
        if (c.sqlite3_bind_text(self.insert_spans_stmt, 3, name.ptr, @intCast(name.len), c.SQLITE_STATIC) != c.SQLITE_OK) {
            return error.sqlite3_bind;
        }
        if (c.sqlite3_bind_text(self.insert_spans_stmt, 4, category.ptr, @intCast(category.len), c.SQLITE_STATIC) != c.SQLITE_OK) {
            return error.sqlite3_bind;
        }
        if (c.sqlite3_bind_int64(self.insert_spans_stmt, 5, start_time_us) != c.SQLITE_OK) {
            return error.sqlite3_bind;
        }
        if (c.sqlite3_bind_int64(self.insert_spans_stmt, 6, duration_us) != c.SQLITE_OK) {
            return error.sqlite3_bind;
        }
        if (c.sqlite3_bind_int64(self.insert_spans_stmt, 7, end_time_us) != c.SQLITE_OK) {
            return error.sqlite3_bind;
        }

        if (c.sqlite3_step(self.insert_spans_stmt) != c.SQLITE_DONE) {
            std.log.err("{s}", .{c.sqlite3_errmsg(self.conn)});
            return error.sqlite3_step;
        }
    }

    fn handle_complete_event(self: *Self, trace_event: *const TraceEvent, pid: i64, tid: i64, ts: i64, dur: i64) !void {
        // var process = try self.get_or_create_process(pid);
        // var thread = try process.get_or_create_thread(tid);

        try self.insert_span(pid, tid, trace_event.name orelse "", trace_event.cat orelse "", ts, dur);
    }
};
