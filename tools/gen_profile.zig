const std = @import("std");

const TraceEvent = struct {
    name: []const u8,
    cat: []const u8,
    ph: []const u8,
    ts: i64,
    tss: ?i64,
    pid: i64,
    tid: i64,
    cname: ?[]const u8,
    dur: ?i64,
    tdur: ?i64,
};

pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    const allocator = gpa.allocator();
    const args = try std.process.argsAlloc(allocator);
    if (!(args.len == 2 or args.len == 3)) {
        std.log.err("Usage: gen_profile <num_events> [output]", .{});
        return;
    }
    const num_events = try std.fmt.parseInt(u64, args[1], 10);

    var file = blk: {
        if (args.len == 3) {
            break :blk try std.fs.cwd().createFile(args[2], .{});
        } else {
            break :blk std.io.getStdOut();
        }
    };
    defer file.close();
    var buffered_writer = std.io.bufferedWriter(file.writer());
    const writer = buffered_writer.writer();
    try writer.print("{{\"traceEvents\":[\n", .{});
    var buf: [512]u8 = undefined;
    for (0..num_events) |i| {
        const name = try std.fmt.bufPrint(&buf, "event{}", .{i});
        var trace_event = TraceEvent {
            .name = name,
            .cat = "test",
            .ph = "X",
            .ts = @intCast(i64, i * 10),
            .tss = null,
            .pid = 0,
            .tid = 0,
            .cname = null,
            .dur = 10,
            .tdur = null,
        };
        try writeTraceEvent(writer, &trace_event);
        var maybe_comma: []const u8 = ",";
        if (i + 1 == num_events) {
            maybe_comma = "";
        }
        try writer.print("{s}\n", .{maybe_comma});
    }
    try writer.print("]}}\n", .{});
}

fn writeTraceEvent(writer: anytype, trace_event: *TraceEvent) !void {
    try writer.print("  {{", .{});
    try writer.print("\"name\": \"{s}\", \"cat\": \"{s}\", \"ph\": \"{s}\", \"ts\": {}, \"pid\": {}, \"tid\": {}", .{trace_event.name, trace_event.cat, trace_event.ph, trace_event.ts, trace_event.pid, trace_event.tid});
    if (trace_event.dur) |dur| {
        try writer.print(", \"dur\": {}", .{dur});
    }
    try writer.print("}}", .{});
}