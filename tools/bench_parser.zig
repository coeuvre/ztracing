const std = @import("std");
const Allocator = std.mem.Allocator;
const ArenaAllocator = std.heap.ArenaAllocator;

const JsonProfileParser = @import("parser").JsonProfileParser;

pub const Mode = enum {
    baseline,
    parse_typed,
    scan_complete_input,
    scan_streaming,
    parser,
    parser2,
};

pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    var args = try std.process.argsAlloc(allocator);
    defer std.process.argsFree(allocator, args);

    if (args.len != 3) {
        std.log.info("usage: bench_parser <mode> <file>", .{});
        return;
    }

    const mode_str = args[1];
    const maybe_mode = std.meta.stringToEnum(Mode, mode_str);
    if (maybe_mode == null) {
        std.log.info("Unknown mode `{s}`, available modes: ", .{mode_str});
        for (std.meta.fieldNames(Mode)) |name| {
            std.log.info("    {s}", .{name});
        }
        return;
    }
    const mode = maybe_mode.?;

    const path = args[2];

    var timer = try std.time.Timer.start();
    var result = try parse(allocator, mode, path);
    const delta_ns = timer.read();

    const stdout = std.io.getStdOut().writer();
    try stdout.print("{s}\n", .{mode_str});
    try stdout.print("Total Duration: {}\n", .{result.total_dur});
    const delta_s = @as(f64, @floatFromInt(delta_ns)) / 1000000000.0;
    const speed = @as(f64, @floatFromInt(result.total_bytes)) / (1024.0 * 1024.0) / delta_s;
    try stdout.print("Wall time: {d:.1} s\n", .{delta_s});
    try stdout.print("Speed: {d:.1} MiB / s\n", .{speed});
}

fn parse(allocator: Allocator, mode: Mode, path: []const u8) !ParseResult {
    return switch (mode) {
        .baseline => baseline(allocator, path),
        .parse_typed => parseTyped(allocator, path),
        .scan_complete_input => scanCompleteInput(allocator, path),
        .scan_streaming => scanStreaming(allocator, path),
        .parser => parseWithParser(allocator, path),
        .parser2 => parse2(allocator, path),
    };
}

const ParseResult = struct {
    total_dur: i64 = 0,
    total_bytes: usize = 0,
};

fn baseline(allocator: Allocator, path: []const u8) !ParseResult {
    var result = ParseResult{};
    var file = try std.fs.cwd().openFile(path, .{});
    defer file.close();

    const input = try file.readToEndAlloc(allocator, std.math.maxInt(usize));
    defer allocator.free(input);
    result.total_bytes = input.len;

    var parsed = try std.json.parseFromSlice(std.json.Value, allocator, input, .{});
    defer parsed.deinit();

    if (parsed.value.object.get("traceEvents")) |trace_events| {
        for (trace_events.array.items) |trace_event| {
            if (trace_event.object.get("dur")) |dur| {
                result.total_dur += dur.integer;
            }
        }
    }

    return result;
}

const TraceEvent = struct {
    name: ?[]const u8 = null,
    cat: ?[]const u8 = null,
    ph: ?[]const u8 = null,
    ts: ?i64 = null,
    tss: ?i64 = null,
    pid: ?i64 = null,
    tid: ?i64 = null,
    dur: ?i64 = null,
    args: ?std.json.Value = null,
    cname: ?[]const u8 = null,
};

const ObjectFormat = struct {
    traceEvents: []TraceEvent,
};

fn parseTyped(allocator: Allocator, path: []const u8) !ParseResult {
    var result = ParseResult{};
    var file = try std.fs.cwd().openFile(path, .{});
    defer file.close();

    const input = try file.readToEndAlloc(allocator, std.math.maxInt(usize));
    defer allocator.free(input);
    result.total_bytes = input.len;

    var parsed = try std.json.parseFromSlice(ObjectFormat, allocator, input, .{});
    defer parsed.deinit();

    return result;
}

fn scanCompleteInput(allocator: Allocator, path: []const u8) !ParseResult {
    var result = ParseResult{};
    var file = try std.fs.cwd().openFile(path, .{});
    defer file.close();

    const input = try file.readToEndAlloc(allocator, std.math.maxInt(usize));
    defer allocator.free(input);
    result.total_bytes = input.len;

    var scanner = std.json.Scanner.initCompleteInput(allocator, input);
    defer scanner.deinit();

    while (true) {
        const token = try scanner.next();
        if (token == .end_of_document) {
            break;
        }
    }

    return result;
}

fn scanStreaming(allocator: Allocator, path: []const u8) !ParseResult {
    var result = ParseResult{};

    var file = try std.fs.cwd().openFile(path, .{});
    defer file.close();

    var scanner = std.json.Scanner.initStreaming(allocator);
    defer scanner.deinit();

    var buffer: [1024 * 1024]u8 = undefined;

    var eof = false;
    while (!eof) {
        const nread = try file.read(&buffer);
        scanner.feedInput(buffer[0..nread]);
        result.total_bytes += nread;
        if (nread < buffer.len) {
            eof = true;
            scanner.endInput();
        }

        while (true) {
            const token = scanner.next() catch |err| switch (err) {
                error.BufferUnderrun => {
                    break;
                },
                else => return err,
            };
            if (token == .end_of_document) {
                eof = true;
                break;
            }
        }
    }

    return result;
}

fn parseWithParser(allocator: Allocator, path: []const u8) !ParseResult {
    var result = ParseResult{};

    var file = try std.fs.cwd().openFile(path, .{});
    defer file.close();

    var parser = JsonProfileParser.init(allocator);
    defer parser.deinit();

    var buf = try allocator.alloc(u8, 1024 * 1024);
    defer allocator.free(buf);

    var eof = false;
    while (!eof) {
        const nread = try file.read(buf);
        parser.feedInput(buf[0..nread]);
        result.total_bytes += nread;
        if (nread < buf.len) {
            eof = true;
            parser.endInput();
        }

        while (true) {
            const event = try parser.next();
            if (event == .none) {
                break;
            }
        }
    }

    return result;
}

const Parser2 = struct {
    arena: std.heap.ArenaAllocator,
    buf: std.ArrayList(u8),
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

    fn init(allocator: Allocator) Parser2 {
        return .{
            .arena = std.heap.ArenaAllocator.init(allocator),
            .buf = std.ArrayList(u8).init(allocator),
            .state = .{ .init = .{ .has_object_format = false } },
        };
    }

    fn process(self: *Parser2, scanner: *std.json.Scanner) !void {
        loop: while (true) {
            switch (self.state) {
                .init => |s| {
                    const token = scanner.next() catch |err| switch (err) {
                        error.BufferUnderrun => break,
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
                        error.BufferUnderrun => break,
                        else => return err,
                    };

                    switch (token) {
                        .object_end => {
                            self.state = .done;
                            return;
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
                                // TODO
                                unreachable;
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
                        return;
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
                        return;
                    } else {
                        const input = scanner.input[begin..end];

                        // std.log.info("{s}", .{input});
                        try handleTraceEvent(&self.arena, input);

                        scanner.cursor = end;
                        // std.log.info("Next: {s}", .{scanner.input[scanner.cursor..]});
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
                        return;
                    } else {
                        try self.buf.appendSlice(scanner.input[0..end]);
                        // std.log.info("{s}", .{self.buf.items});

                        const input = self.buf.items;
                        try handleTraceEvent(&self.arena, input);

                        scanner.cursor = end;

                        self.buf.clearRetainingCapacity();
                        self.state = .{ .array_format = .{ .has_object_format = s.has_object_format } };
                    }
                },

                .done => {
                    unreachable;
                },

                // .trace_event => |s| {
                //     if (s.key) |key| {
                //         _ = key;
                //         switch (token) {
                //             // TODO: Partial String
                //             .string => |str| {
                //                 if (std.mem.eql(u8, str, "name")) {} else {
                //                     // ignore
                //                 }
                //             },

                //             // TODO: Partial Number
                //             .number => |str| {
                //                 _ = str;
                //             },
                //             else => error.unexpected_token,
                //         }
                //     } else {
                //         switch (token) {
                //             // TODO: Partial String
                //             .string => |str| {
                //                 s.key = str;
                //             },
                //             else => error.unexpected_token,
                //         }
                //     }
                // },
            }
        }
    }

    fn deinit(self: *Parser2) void {
        self.arena.deinit();
        self.buf.deinit();
    }

    fn handleTraceEvent(arena: *ArenaAllocator, complete_input: []const u8) !void {
        var trace_event = TraceEvent{};
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
                    } else if (std.mem.eql(u8, key, "cat")) {
                        trace_event.cat = try parseString(&scanner);
                    } else if (std.mem.eql(u8, key, "ph")) {
                        trace_event.ph = try parseString(&scanner);
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
                    } else {
                        try skipJsonValue(&scanner, null);
                    }
                },
                else => return error.unexpected_token,
            }
        }
        _ = arena.reset(.retain_capacity);
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
};

fn parse2(allocator: Allocator, path: []const u8) !ParseResult {
    var result = ParseResult{};

    var file = try std.fs.cwd().openFile(path, .{});
    defer file.close();

    var buf = try allocator.alloc(u8, 1024 * 1024);
    defer allocator.free(buf);

    var scanner = std.json.Scanner.initStreaming(allocator);
    defer scanner.deinit();

    var parser = Parser2.init(allocator);
    defer parser.deinit();

    var eof = false;
    while (!eof and parser.state != .done) {
        const nread = try file.read(buf);
        scanner.feedInput(buf[0..nread]);
        result.total_bytes += nread;
        if (nread < buf.len) {
            eof = true;
            scanner.endInput();
        }
        try parser.process(&scanner);
    }

    return result;
}
