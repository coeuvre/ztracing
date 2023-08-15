const std = @import("std");
const test_utils = @import("./test_utils.zig");

const Allocator = std.mem.Allocator;
const Token = std.json.Token;
const ArenaAllocator = std.heap.ArenaAllocator;
const Buffer = std.ArrayList(u8);
const expectOptional = test_utils.expectOptional;

pub const TraceEvent = struct {
    name: ?[]const u8 = null,
    id: ?[]const u8 = null,
    cat: ?[]const u8 = null,
    ph: ?u8 = null,
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

const Diagnostic = struct {
    stack: std.ArrayList([]const u8),
    last_token: ?Token = null,
    input: ?[]const u8 = null,

    pub fn init(allocator: Allocator) Diagnostic {
        return .{
            .stack = std.ArrayList([]const u8).init(allocator),
        };
    }

    pub fn format(self: *const Diagnostic, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        _ = options;
        _ = fmt;
        for (self.stack.items) |item| {
            try writer.print("{s}", .{item});
        }
        try writer.print(" {?}", .{self.last_token});

        try writer.print("\n{?s}", .{self.input});
    }

    pub fn deinit(self: *Diagnostic) void {
        self.stack.deinit();
    }
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
    diagnostic: Diagnostic,

    pub fn init(allocator: Allocator) JsonProfileParser {
        return .{
            .arena = std.heap.ArenaAllocator.init(allocator),
            .buf = std.ArrayList(u8).init(allocator),
            .state = .{ .init = .{ .has_object_format = false } },
            .trace_event = .{},
            .scanner = std.json.Scanner.initStreaming(allocator),
            .diagnostic = Diagnostic.init(allocator),
        };
    }

    pub fn feedInput(self: *JsonProfileParser, input: []const u8) void {
        self.scanner.feedInput(input);
    }

    pub fn cursor(self: *JsonProfileParser) usize {
        return self.scanner.cursor;
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
                                self.diagnostic.last_token = token;
                                return error.unexpected_token;
                            }
                            self.state = .object_format;
                            try self.diagnostic.stack.append(".");
                        },
                        .array_begin => {
                            self.state = .{ .array_format = .{ .has_object_format = s.has_object_format } };
                            try self.diagnostic.stack.append("[]");
                        },
                        else => {
                            self.diagnostic.last_token = token;
                            return error.unexpected_token;
                        },
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
                            _ = self.diagnostic.stack.pop();
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
                                try self.diagnostic.stack.append("traceEvents");
                            } else {
                                try self.diagnostic.stack.append(key);
                                try skipJsonValue(&self.scanner, null, &self.diagnostic);
                                _ = self.diagnostic.stack.pop();
                            }

                            self.buf.clearRetainingCapacity();
                        },
                        else => {
                            self.diagnostic.last_token = token;
                            return error.unexpected_token;
                        },
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
                        _ = self.diagnostic.stack.pop();
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

                        try parseTraceEvent(&self.arena, input, &self.trace_event, &self.diagnostic);
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
                        try parseTraceEvent(&self.arena, input, &self.trace_event, &self.diagnostic);
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
        self.diagnostic.deinit();
    }
};

fn parseTraceEvent(arena: *ArenaAllocator, complete_input: []const u8, trace_event: *TraceEvent, diagnostic: *Diagnostic) !void {
    trace_event.* = .{};
    _ = arena.reset(.retain_capacity);

    diagnostic.input = complete_input;

    var scanner = std.json.Scanner.initCompleteInput(arena.allocator(), complete_input);
    try expectToken(&scanner, .object_begin, diagnostic);
    while (true) {
        const token = try scanner.next();
        if (token == .object_end) {
            break;
        }

        switch (token) {
            .string => |key| {
                if (std.mem.eql(u8, key, "name")) {
                    try diagnostic.stack.append(".name");
                    trace_event.name = try parseString(arena, &scanner, diagnostic);
                    _ = diagnostic.stack.pop();
                } else if (std.mem.eql(u8, key, "id")) {
                    try diagnostic.stack.append(".id");
                    trace_event.id = try parseString(arena, &scanner, diagnostic);
                    _ = diagnostic.stack.pop();
                } else if (std.mem.eql(u8, key, "cat")) {
                    try diagnostic.stack.append(".cat");
                    trace_event.cat = try parseString(arena, &scanner, diagnostic);
                    _ = diagnostic.stack.pop();
                } else if (std.mem.eql(u8, key, "ph")) {
                    try diagnostic.stack.append(".ph");
                    trace_event.ph = (try parseString(arena, &scanner, diagnostic))[0];
                    _ = diagnostic.stack.pop();
                } else if (std.mem.eql(u8, key, "ts")) {
                    try diagnostic.stack.append(".ts");
                    trace_event.ts = try parseInt(arena, &scanner, diagnostic);
                    _ = diagnostic.stack.pop();
                } else if (std.mem.eql(u8, key, "tss")) {
                    try diagnostic.stack.append(".tss");
                    trace_event.tss = try parseInt(arena, &scanner, diagnostic);
                    _ = diagnostic.stack.pop();
                } else if (std.mem.eql(u8, key, "pid")) {
                    try diagnostic.stack.append(".pid");
                    trace_event.pid = try parseInt(arena, &scanner, diagnostic);
                    _ = diagnostic.stack.pop();
                } else if (std.mem.eql(u8, key, "tid")) {
                    try diagnostic.stack.append(".tid");
                    trace_event.tid = try parseInt(arena, &scanner, diagnostic);
                    _ = diagnostic.stack.pop();
                } else if (std.mem.eql(u8, key, "dur")) {
                    try diagnostic.stack.append(".dur");
                    trace_event.dur = try parseInt(arena, &scanner, diagnostic);
                    _ = diagnostic.stack.pop();
                } else if (std.mem.eql(u8, key, "cname")) {
                    try diagnostic.stack.append(".cname");
                    trace_event.cname = try parseString(arena, &scanner, diagnostic);
                    _ = diagnostic.stack.pop();
                } else if (std.mem.eql(u8, key, "args")) {
                    try diagnostic.stack.append(".args");
                    trace_event.args = try parseObjectValue(arena, &scanner, diagnostic);
                    _ = diagnostic.stack.pop();
                } else {
                    try diagnostic.stack.append(key);
                    try skipJsonValue(&scanner, null, diagnostic);
                    _ = diagnostic.stack.pop();
                }
            },
            else => {
                diagnostic.last_token = token;
                return error.unexpected_token;
            },
        }
    }

    diagnostic.input = null;
}

fn expectToken(scanner: *std.json.Scanner, expected: std.json.Token, diagnostic: *Diagnostic) !void {
    const token = try scanner.next();
    if (@intFromEnum(token) != @intFromEnum(expected)) {
        diagnostic.last_token = token;
        return error.unexpected_token;
    }
}

fn parseString(arena: *ArenaAllocator, scanner: *std.json.Scanner, diagnostic: *Diagnostic) ![]const u8 {
    while (true) {
        const token = try scanner.nextAlloc(arena.allocator(), .alloc_if_needed);
        switch (token) {
            .allocated_string => |str| {
                return str;
            },
            .string => |str| {
                return str;
            },

            .allocated_number => |str| {
                return str;
            },

            .number => |str| {
                return str;
            },

            else => {
                diagnostic.last_token = token;
                return error.unexpected_token;
            },
        }
    }
}

fn parseInt(arena: *ArenaAllocator, scanner: *std.json.Scanner, diagnostic: *Diagnostic) !i64 {
    const token = try scanner.nextAlloc(arena.allocator(), .alloc_if_needed);
    switch (token) {
        .allocated_number, .number => |str| {
            return try std.fmt.parseInt(i64, str, 10);
        },
        else => {
            diagnostic.last_token = token;
            return error.unexpected_token;
        },
    }
}

fn parseObjectValue(arena: *ArenaAllocator, scanner: *std.json.Scanner, diagnostic: *Diagnostic) !std.json.Value {
    var start = scanner.cursor;
    // TODO: Properly skip the whitespace.
    while (start < scanner.input.len and (scanner.input[start] == ':' or scanner.input[start] == ' ')) {
        start += 1;
    }
    try skipJsonValue(scanner, null, diagnostic);
    const end = scanner.cursor;
    diagnostic.input = scanner.input[start..end];
    return try std.json.parseFromSliceLeaky(std.json.Value, arena.allocator(), scanner.input[start..end], .{});
}

fn skipJsonValue(scanner: *std.json.Scanner, maybe_array_end: ?*bool, diagnostic: *Diagnostic) !void {
    const token = try scanner.next();
    switch (token) {
        .object_begin => {
            while (true) {
                const token2 = try scanner.next();
                switch (token2) {
                    .object_end => break,

                    .partial_string,
                    .partial_string_escaped_1,
                    .partial_string_escaped_2,
                    .partial_string_escaped_3,
                    .partial_string_escaped_4,
                    => {
                        try skipString(scanner);
                        try skipJsonValue(scanner, null, diagnostic);
                    },

                    .string => try skipJsonValue(scanner, null, diagnostic),

                    else => {
                        diagnostic.last_token = token2;
                        return error.unexpected_token;
                    },
                }
            }
        },

        .array_begin => {
            var array_end = false;
            while (true) {
                try skipJsonValue(scanner, &array_end, diagnostic);
                if (array_end) {
                    break;
                }
            }
        },

        .array_end => {
            if (maybe_array_end) |array_end| {
                array_end.* = true;
            } else {
                diagnostic.last_token = token;
                return error.unexpected_token;
            }
        },

        .partial_string,
        .partial_string_escaped_1,
        .partial_string_escaped_2,
        .partial_string_escaped_3,
        .partial_string_escaped_4,
        => {
            try skipString(scanner);
        },

        .partial_number => {
            try skipNumber(scanner);
        },

        .true,
        .false,
        .null,
        .number,
        .string,
        => {},

        else => {
            diagnostic.last_token = token;
            return error.unexpected_token;
        },
    }
}

fn skipString(scanner: *std.json.Scanner) !void {
    while (true) {
        const token = try scanner.next();
        switch (token) {
            .partial_string,
            .partial_string_escaped_1,
            .partial_string_escaped_2,
            .partial_string_escaped_3,
            .partial_string_escaped_4,
            => {},
            .string => break,
            else => return error.unexpected_token,
        }
    }
}

fn skipNumber(scanner: *std.json.Scanner) !void {
    while (true) {
        const token = try scanner.next();
        switch (token) {
            .partial_number => {},
            .number => break,
            else => return error.unexpected_token,
        }
    }
}

fn expectEqualTraceEvents(expected: *const TraceEvent, actual: *const TraceEvent) !void {
    if (try expectOptional(expected.name, actual.name)) {
        try std.testing.expectEqualStrings(expected.name.?, actual.name.?);
    }
}

const ExpectedParseResult = struct {
    trace_events: []const TraceEvent = &[_]TraceEvent{},
};

fn testParser(input: []const u8, expected: ExpectedParseResult) ParseError!void {
    var allocator = std.testing.allocator;
    var parser = JsonProfileParser.init(allocator);
    parser.feedInput(input);
    parser.endInput();
    defer parser.deinit();

    var trace_event_index: usize = 0;

    while (true) {
        const result = try parser.next();
        switch (result) {
            .trace_event => |trace_event| {
                try std.testing.expect(trace_event_index < expected.trace_events.len);
                const expected_trace_event = &expected.trace_events[trace_event_index];
                try expectEqualTraceEvents(expected_trace_event, trace_event);
                trace_event_index += 1;
            },
            .none => break,
        }
    }
}

test "invalid profile" {
    try std.testing.expectError(error.SyntaxError, testParser(
        \\}
    , .{
        .trace_events = &[_]TraceEvent{},
    }));
}

test "unexpected eof" {
    try std.testing.expectError(error.UnexpectedEndOfInput, testParser(
        \\{
    , .{
        .trace_events = &[_]TraceEvent{},
    }));
}

test "invalid trace events" {
    try testParser(
        \\{
        \\  "traceEvents": [
        \\    "a"
        \\  ]
        \\}
    , .{
        .trace_events = &[_]TraceEvent{},
    });
}

test "empty profile" {
    try testParser(
        \\{
        \\}
    , .{
        .trace_events = &[_]TraceEvent{},
    });
}

test "empty traceEvents profile" {
    try testParser(
        \\{
        \\  "traceEvents": []
        \\}
    , .{
        .trace_events = &[_]TraceEvent{},
    });
}

test "simple traceEvents profile" {
    try testParser(
        \\{
        \\  "traceEvents": [
        \\    { "name": "test" }
        \\  ]
        \\}
    , .{
        .trace_events = &[_]TraceEvent{
            .{ .name = "test" },
        },
    });
}

test "escaped string" {
    try testParser(
        \\{
        \\  "traceEvents": [
        \\    { "name": "\"test\"" }
        \\  ]
        \\}
    , .{
        .trace_events = &[_]TraceEvent{
            .{ .name = "\"test\"" },
        },
    });
}

test "skip escaped object key" {
    try testParser(
        \\{
        \\  "@\u1234": "b"
        \\}
    , .{
        .trace_events = &[_]TraceEvent{},
    });
}

test "skip escaped object value" {
    try testParser(
        \\{
        \\  "data": "@\u1234"
        \\}
    , .{
        .trace_events = &[_]TraceEvent{},
    });
}
