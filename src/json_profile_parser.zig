const std = @import("std");
const Allocator = std.mem.Allocator;
const Token = std.json.Token;
const ArenaAllocator = std.heap.ArenaAllocator;

const Buffer = std.ArrayList(u8);

pub const TraceEvent = struct {
    name: ?[]const u8 = null,
    id: ?[]const u8 = null,
    cat: ?[]const u8 = null,
    ph: u8 = 0,
    ts: ?i64 = null,
    tss: ?i64 = null,
    pid: ?i64 = null,
    tid: ?i64 = null,
    dur: ?i64 = null,
    args: ?std.json.Value = null,
    cname: ?[]const u8 = null,
};

pub const ParseError = anyerror;
pub const ParseResult = union(enum) {
    none,
    trace_event: *TraceEvent,
};

pub const JsonProfileParser = struct {
    arena: std.heap.ArenaAllocator,
    buf: std.ArrayList(u8),
    trace_event: TraceEvent,
    state: union(enum) {
        init: struct {
            has_object_format: bool,
        },
        object_format,
        array_format: struct {
            has_object_format: bool,
        },
        array_format_wait_more: struct {
            has_object_format: bool,
            stack: i32,
        },
        done,
    },
    scanner: std.json.Scanner,

    pub fn init(allocator: Allocator) JsonProfileParser {
        return .{
            .arena = std.heap.ArenaAllocator.init(allocator),
            .buf = std.ArrayList(u8).init(allocator),
            .state = .{ .init = .{ .has_object_format = false } },
            .trace_event = .{},
            .scanner = std.json.Scanner.initStreaming(allocator),
        };
    }

    pub fn feedInput(self: *JsonProfileParser, input: []const u8) void {
        self.scanner.feedInput(input);
    }

    pub fn endInput(self: *JsonProfileParser) void {
        self.scanner.endInput();
    }

    pub fn done(self: *JsonProfileParser) bool {
        return self.state == .done;
    }

    pub fn next(self: *JsonProfileParser) !ParseResult {
        const scanner = &self.scanner;
        loop: while (true) {
            switch (self.state) {
                .init => |s| {
                    const token = scanner.next() catch |err| switch (err) {
                        error.BufferUnderrun => return .none,
                        else => return err,
                    };

                    switch (token) {
                        .object_begin => {
                            if (s.has_object_format) {
                                return error.unexpected_token;
                            }
                            self.state = .object_format;
                        },
                        .array_begin => {
                            self.state = .{ .array_format = .{ .has_object_format = s.has_object_format } };
                        },
                        else => return error.unexpected_token,
                    }
                },

                .object_format => {
                    const token = scanner.next() catch |err| switch (err) {
                        error.BufferUnderrun => return .none,
                        else => return err,
                    };

                    switch (token) {
                        .object_end => {
                            self.state = .done;
                            return .none;
                        },

                        .partial_string_escaped_1 => |str| {
                            try self.buf.appendSlice(&str);
                        },
                        .partial_string_escaped_2 => |str| {
                            try self.buf.appendSlice(&str);
                        },
                        .partial_string_escaped_3 => |str| {
                            try self.buf.appendSlice(&str);
                        },
                        .partial_string_escaped_4 => |str| {
                            try self.buf.appendSlice(&str);
                        },
                        .partial_string => |str| {
                            try self.buf.appendSlice(str);
                        },
                        .string => |str| {
                            const key = blk: {
                                if (self.buf.items.len > 0) {
                                    try self.buf.appendSlice(str);
                                    break :blk self.buf.items;
                                } else {
                                    break :blk str;
                                }
                            };

                            if (std.mem.eql(u8, key, "traceEvents")) {
                                self.state = .{ .init = .{ .has_object_format = true } };
                            } else {
                                try skipJsonValue(&self.scanner, null);
                            }

                            self.buf.clearRetainingCapacity();
                        },
                        else => return error.unexpected_token,
                    }
                },

                .array_format => |s| {
                    var found_object_begin = false;
                    var found_end_array = false;
                    for (scanner.input[scanner.cursor..], scanner.cursor..) |ch, i| {
                        if (ch == '{') {
                            found_object_begin = true;
                            scanner.cursor = i;
                            break;
                        } else if (ch == ']') {
                            found_end_array = true;
                            scanner.cursor = i;
                            break;
                        }
                    }

                    if (found_end_array) {
                        // Skip array_end
                        _ = try scanner.next();
                        if (s.has_object_format) {
                            self.state = .object_format;
                        } else {
                            self.state = .done;
                        }
                        continue :loop;
                    }

                    if (!found_object_begin) {
                        scanner.cursor = scanner.input.len;
                        return .none;
                    }

                    const begin = scanner.cursor;
                    var end: usize = 0;
                    var stack: i32 = 1;
                    for (scanner.input[begin + 1 ..], begin + 1..) |b, i| {
                        if (b == '{') {
                            stack += 1;
                        } else if (b == '}') {
                            stack -= 1;
                        }
                        if (stack == 0) {
                            end = i + 1;
                            break;
                        }
                    }
                    if (stack != 0) {
                        // Missing end_object
                        try self.buf.appendSlice(scanner.input[begin..]);
                        scanner.cursor = scanner.input.len;
                        self.state = .{
                            .array_format_wait_more = .{
                                .has_object_format = s.has_object_format,
                                .stack = stack,
                            },
                        };
                        return .none;
                    } else {
                        const input = scanner.input[begin..end];
                        scanner.cursor = end;

                        try parseTraceEvent(&self.arena, input, &self.trace_event);
                        return .{ .trace_event = &self.trace_event };
                    }
                },

                .array_format_wait_more => |*s| {
                    var end: usize = 0;
                    for (scanner.input[0..], 0..) |ch, i| {
                        if (ch == '{') {
                            s.stack += 1;
                        } else if (ch == '}') {
                            s.stack -= 1;
                        }
                        if (s.stack == 0) {
                            end = i + 1;
                            break;
                        }
                    }
                    if (s.stack != 0) {
                        try self.buf.appendSlice(scanner.input);
                        scanner.cursor = scanner.input.len;
                        return .none;
                    } else {
                        try self.buf.appendSlice(scanner.input[0..end]);
                        scanner.cursor = end;
                        const input = self.buf.items;
                        try parseTraceEvent(&self.arena, input, &self.trace_event);
                        self.buf.clearRetainingCapacity();
                        self.state = .{ .array_format = .{ .has_object_format = s.has_object_format } };
                        return .{ .trace_event = &self.trace_event };
                    }
                },

                .done => {
                    unreachable;
                },
            }
        }
    }

    pub fn deinit(self: *JsonProfileParser) void {
        self.arena.deinit();
        self.buf.deinit();
        self.scanner.deinit();
        self.state = .done;
    }
};

fn parseTraceEvent(arena: *ArenaAllocator, complete_input: []const u8, trace_event: *TraceEvent) !void {
    trace_event.* = .{};
    _ = arena.reset(.retain_capacity);

    var scanner = std.json.Scanner.initCompleteInput(arena.allocator(), complete_input);
    try expectToken(&scanner, .object_begin);
    while (true) {
        const token = try scanner.next();
        if (token == .object_end) {
            break;
        }

        switch (token) {
            .string => |key| {
                if (std.mem.eql(u8, key, "name")) {
                    trace_event.name = try parseString(&scanner);
                } else if (std.mem.eql(u8, key, "id")) {
                    trace_event.id = try parseString(&scanner);
                } else if (std.mem.eql(u8, key, "cat")) {
                    trace_event.cat = try parseString(&scanner);
                } else if (std.mem.eql(u8, key, "ph")) {
                    trace_event.ph = (try parseString(&scanner))[0];
                } else if (std.mem.eql(u8, key, "ts")) {
                    trace_event.ts = try parseInt(&scanner);
                } else if (std.mem.eql(u8, key, "tss")) {
                    trace_event.tss = try parseInt(&scanner);
                } else if (std.mem.eql(u8, key, "pid")) {
                    trace_event.pid = try parseInt(&scanner);
                } else if (std.mem.eql(u8, key, "tid")) {
                    trace_event.tid = try parseInt(&scanner);
                } else if (std.mem.eql(u8, key, "dur")) {
                    trace_event.dur = try parseInt(&scanner);
                } else if (std.mem.eql(u8, key, "cname")) {
                    trace_event.cname = try parseString(&scanner);
                } else if (std.mem.eql(u8, key, "args")) {
                    trace_event.args = try parseObjectValue(&scanner, arena);
                } else {
                    try skipJsonValue(&scanner, null);
                }
            },
            else => return error.unexpected_token,
        }
    }
}

fn expectToken(scanner: *std.json.Scanner, expected: std.json.Token) !void {
    const token = try scanner.next();
    if (@intFromEnum(token) != @intFromEnum(expected)) {
        return error.unexpected_token;
    }
}

fn parseString(scanner: *std.json.Scanner) ![]const u8 {
    const token = try scanner.next();
    switch (token) {
        .string => |str| {
            return str;
        },
        else => return error.unexpected_token,
    }
}

fn parseInt(scanner: *std.json.Scanner) !i64 {
    const token = try scanner.next();
    switch (token) {
        .number => |str| {
            return try std.fmt.parseInt(i64, str, 10);
        },
        else => return error.unexpected_token,
    }
}

fn parseObjectValue(scanner: *std.json.Scanner, arena: *ArenaAllocator) !std.json.Value {
    var start = scanner.cursor;
    // TODO: Properly skip the whitespace.
    while (start < scanner.input.len and (scanner.input[start] == ':' or scanner.input[start] == ' ')) {
        start += 1;
    }
    try skipJsonValue(scanner, null);
    const end = scanner.cursor;
    return try std.json.parseFromSliceLeaky(std.json.Value, arena.allocator(), scanner.input[start..end], .{});
}

fn skipJsonValue(scanner: *std.json.Scanner, maybe_array_end: ?*bool) !void {
    switch (try scanner.next()) {
        .object_begin => {
            while (true) {
                switch (try scanner.next()) {
                    .object_end => break,
                    .string => try skipJsonValue(scanner, null),
                    else => return error.unexpected_token,
                }
            }
        },

        .array_begin => {
            var array_end = false;
            while (true) {
                try skipJsonValue(scanner, &array_end);
                if (array_end) {
                    break;
                }
            }
        },

        .array_end => {
            if (maybe_array_end) |array_end| {
                array_end.* = true;
            } else {
                return error.unexpected_token;
            }
        },

        .true, .false, .null, .number, .string => {},
        else => return error.unexpected_token,
    }
}

fn parseAll(input: []const u8) ParseError![]ParseResult {
    var allocator = std.testing.allocator;
    var parser = JsonProfileParser.init(allocator);
    parser.feedInput(input);
    parser.endInput();
    defer parser.deinit();

    var results = std.ArrayList(ParseResult).init(std.testing.allocator);
    errdefer results.deinit();

    while (true) {
        const result = try parser.next();
        if (result == .none) {
            break;
        }
        try results.append(result);
    }

    return try results.toOwnedSlice();
}

test "invalid profile" {
    try std.testing.expectError(error.SyntaxError, parseAll("}"));
}

test "invalid trace events" {
    const results = try parseAll(
        \\{
        \\    "traceEvents": [
        \\        "a"
        \\    ]
        \\}
        \\
    );
    try std.testing.expect(results.len == 0);
}

test "empty profile" {
    const results = try parseAll("{}");
    try std.testing.expect(results.len == 0);
}

test "empty traceEvents profile" {
    const results = try parseAll("{\"traceEvents\":[]}");
    try std.testing.expect(results.len == 0);
}

test "simple traceEvents profile" {
    const results = try parseAll("{\"traceEvents\":[{\"a\":\"b\"}]}");
    defer std.testing.allocator.free(results);

    try std.testing.expect(results.len == 1);
}
