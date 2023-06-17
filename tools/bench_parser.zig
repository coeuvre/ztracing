const std = @import("std");

pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    var args = try std.process.argsAlloc(allocator);
    defer std.process.argsFree(allocator, args);

    if (args.len != 2) {
        std.log.info("usage: bench_parser <file>", .{});
        return;
    }

    const path = args[1];

    var timer = try std.time.Timer.start();
    var total_bytes: usize = 0;
    var total_dur: i64 = 0;
    {
        var file = try std.fs.cwd().openFile(path, .{});
        defer file.close();

        const input = try file.readToEndAlloc(allocator, std.math.maxInt(usize));
        defer allocator.free(input);

        var parser = std.json.Parser.init(allocator, .alloc_if_needed);
        defer parser.deinit();

        var value = try parser.parse(input);
        defer value.deinit();

        if (value.root.object.get("traceEvents")) |trace_events| {
            for (trace_events.array.items) |trace_event| {
                if (trace_event.object.get("dur")) |dur| {
                    total_dur += dur.integer;
                }
            }
        }

        total_bytes = input.len;
    }
    const delta_ns = timer.read();
    const stdout = std.io.getStdOut().writer();

    const speed = @intToFloat(f64, total_bytes) * (1000000000.0 / 1024.0 / 1024.0) / @intToFloat(f64, delta_ns);
    try stdout.print("Total Duration: {}\n", .{total_dur});
    try stdout.print("{d:.0} MiB / s\n", .{speed});
}
