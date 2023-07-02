const std = @import("std");
const Allocator = std.mem.Allocator;

const JsonProfileParser = @import("parser").JsonProfileParser;

pub const Mode = enum {
    baseline,
    parse_typed,
    scan_complete_input,
    scan_streaming,
    parser,
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
    name: ?[]u8 = null,
    cat: ?[]u8 = null,
    ph: ?[]u8 = null,
    ts: ?i64 = null,
    tss: ?i64 = null,
    pid: ?i64 = null,
    tid: ?i64 = null,
    dur: ?i64 = null,
    args: ?std.json.Value = null,
    cname: ?[]u8 = null,
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
