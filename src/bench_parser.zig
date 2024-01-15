const std = @import("std");
const json_profile_parser = @import("./json_profile_parser.zig");
const count_alloc = @import("./count_alloc.zig");

const Allocator = std.mem.Allocator;
const ArenaAllocator = std.heap.ArenaAllocator;
const JsonProfileParser = json_profile_parser.JsonProfileParser;
const TraceEvent = json_profile_parser.TraceEvent;
const CountAllocator = count_alloc.CountAllocator;

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
    var count_allocator = CountAllocator.init(gpa.allocator());
    const allocator = count_allocator.allocator();

    const args = try std.process.argsAlloc(allocator);
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
    const result = try parse(allocator, mode, path);
    const delta_ns = timer.read();

    const stdout = std.io.getStdOut().writer();
    try stdout.print("{s}\n", .{mode_str});
    try stdout.print("Total Duration: {}\n", .{result.total_dur});
    const delta_ms = @as(f64, @floatFromInt(delta_ns)) / 1000000.0;
    const total_mb = @as(f64, @floatFromInt(result.total_bytes)) / (1024.0 * 1024.0);
    const speed = total_mb / delta_ms * 1000.0;
    try stdout.print("Wall time: {d:.2} ms\n", .{delta_ms});
    try stdout.print("Peek Memory: {d:.1} MiB\n", .{@as(f64, @floatFromInt(count_allocator.get_peek_allocated_bytes())) / (1024.0 * 1024.0)});
    try stdout.print("Size: {d:.1} MiB\n", .{total_mb});
    try stdout.print("Speed: {d:.1} MiB / s\n", .{speed});
}

fn parse(allocator: Allocator, mode: Mode, path: []const u8) !ParseResult {
    return switch (mode) {
        .baseline => baseline(allocator, path),
        .parse_typed => parseTyped(allocator, path),
        .scan_complete_input => scanCompleteInput(allocator, path),
        .scan_streaming => scanStreaming(allocator, path),
        .parser => parse2(allocator, path),
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

fn parse2(allocator: Allocator, path: []const u8) !ParseResult {
    var result = ParseResult{};

    var file = try std.fs.cwd().openFile(path, .{});
    defer file.close();

    var buf = try allocator.alloc(u8, 1024 * 1024);
    defer allocator.free(buf);

    var parser = JsonProfileParser.init(allocator);
    defer parser.deinit();

    var eof = false;
    while (!eof and parser.state != .done) {
        const nread = try file.read(buf);
        parser.feedInput(buf[0..nread]);
        result.total_bytes += nread;
        if (nread == 0) {
            eof = true;
            parser.endInput();
        }
        while (true) {
            switch (try parser.next()) {
                .none => break,
                .trace_event => |trace_event| {
                    if (trace_event.dur) |dur| {
                        result.total_dur += dur;
                    }
                },
            }
        }
    }

    return result;
}
