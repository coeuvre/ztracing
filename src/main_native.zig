const std = @import("std");
const c = @import("c.zig");
const imgui = @import("imgui.zig");
const tracy = @import("tracy.zig");

const Tracing = @import("tracing.zig").Tracing;
const Allocator = std.mem.Allocator;
const CountAllocator = @import("count_alloc.zig").CountAllocator;
const JsonProfileParser = @import("json_profile_parser.zig").JsonProfileParser;
const Profile = @import("profile.zig").Profile;

fn get_seconds_elapsed(from: usize, to: usize, freq: usize) f32 {
    return @floatCast(@as(f64, @floatFromInt(to - from)) / @as(f64, @floatFromInt(freq)));
}

fn zalloc(userdata: ?*anyopaque, items: c_uint, size: c_uint) callconv(.C) ?*anyopaque {
    return imgui.alloc(items * size, userdata);
}

fn zfree(userdata: ?*anyopaque, address: ?*anyopaque) callconv(.C) void {
    imgui.free(address, userdata);
}

const user_event = struct {
    var load_started: u32 = 0;
    var load_progress: u32 = 0;
    var load_error: u32 = 0;
    var load_done: u32 = 0;
};

fn send_load_error(allocator: Allocator, comptime fmt: []const u8, args: anytype) void {
    const alloc_error_msg = "AllocPrintError";
    var own_msg: i32 = 1;
    const msg = std.fmt.allocPrint(allocator, fmt, args) catch blk: {
        own_msg = 0;
        break :blk alloc_error_msg;
    };
    _ = c.SDL_PushEvent(@constCast(&c.SDL_Event{
        .user = .{
            .type = user_event.load_error,
            .code = own_msg,
            .data1 = @constCast(msg.ptr),
            .data2 = @ptrFromInt(msg.len),
        },
    }));
}

fn load_thread_main(allocator: Allocator, path: []u8) void {
    tracy.set_thread_name("Load File Thread");

    defer allocator.free(path);

    const trace = tracy.trace(@src());
    defer trace.end();

    const start_counter = c.SDL_GetPerformanceCounter();

    _ = c.SDL_PushEvent(@constCast(&c.SDL_Event{
        .type = user_event.load_started,
    }));

    const file = std.fs.openFileAbsolute(path, .{}) catch |err| {
        send_load_error(allocator, "Failed to open file {s}: {}", .{ path, err });
        return;
    };
    defer file.close();
    const stat = file.stat() catch |err| {
        send_load_error(allocator, "Failed to stat file {s}: {}", .{ path, err });
        return;
    };
    const file_size = stat.size;

    var processed_bytes: usize = 0;
    var offset: usize = 0;

    var header = std.mem.zeroes([2]u8);
    const is_gz = blk: {
        const bytes_read = file.read(&header) catch |err| {
            send_load_error(allocator, "Failed to read file: {}", .{err});
            return;
        };
        file.seekTo(0) catch |err| {
            send_load_error(allocator, "Failed to seek file: {}", .{err});
            return;
        };
        break :blk bytes_read == 2 and (header[0] == 0x1F and header[1] == 0x8B);
    };

    var stream = c.z_stream{
        .zalloc = zalloc,
        .zfree = zfree,
        .@"opaque" = @constCast(&allocator),
    };
    var is_stream_end = false;
    defer if (is_gz) {
        _ = c.inflateEnd(&stream);
    };
    if (is_gz) {
        // Automatically detect header
        const ret = c.inflateInit2(&stream, c.MAX_WBITS | 32);
        if (ret != c.Z_OK) {
            send_load_error(allocator, "Failed to init inflate: {}", .{ret});
            return;
        }
    }

    var parser = JsonProfileParser.init(allocator);
    defer parser.deinit();
    var profile = allocator.create(Profile) catch |err| {
        send_load_error(allocator, "Failed to allocate profile: {}", .{err});
        return;
    };
    profile.* = Profile.init(allocator);

    var file_buf = allocator.alloc(u8, 4096) catch |err| {
        send_load_error(allocator, "Failed to allocate file_buf: {}", .{err});
        return;
    };
    defer allocator.free(file_buf);
    var inflate_buf: []u8 = blk: {
        if (is_gz) {
            break :blk allocator.alloc(u8, 4096) catch |err| {
                send_load_error(allocator, "Failed to allocate inflate_buf: {}", .{err});
                return;
            };
        } else {
            break :blk &[0]u8{};
        }
    };
    defer if (is_gz) {
        allocator.free(inflate_buf);
    };

    while (true) {
        const trace1 = tracy.traceNamed(@src(), "load chunk");
        defer trace1.end();

        const file_bytes_read = file.read(file_buf) catch |err| {
            send_load_error(allocator, "Failed to read file {s}: {}", .{ path, err });
            return;
        };

        if (is_gz) {
            stream.avail_in = @intCast(file_bytes_read);
            stream.next_in = file_buf.ptr;
        }

        while (true) {
            if (is_gz) {
                stream.avail_out = @intCast(inflate_buf.len);
                stream.next_out = inflate_buf.ptr;

                const ret = blk: {
                    const trace2 = tracy.traceNamed(@src(), "inflate");
                    defer trace2.end();
                    break :blk c.inflate(&stream, c.Z_NO_FLUSH);
                };

                if (ret != c.Z_OK and ret != c.Z_STREAM_END) {
                    send_load_error(allocator, "Failed to inflate: {}", .{ret});
                    return;
                }
                const have = inflate_buf.len - stream.avail_out;
                if (ret == c.Z_STREAM_END) {
                    parser.endInput();
                    is_stream_end = true;
                } else {
                    parser.feedInput(inflate_buf[0..have]);
                    processed_bytes += have;
                }
            } else {
                if (file_bytes_read == 0) {
                    parser.endInput();
                } else {
                    parser.feedInput(file_buf[0..file_bytes_read]);
                    processed_bytes += file_bytes_read;
                }
            }

            while (!parser.done()) {
                const event = blk: {
                    const trace2 = tracy.traceNamed(@src(), "parser.next()");
                    defer trace2.end();
                    break :blk parser.next() catch |err| {
                        send_load_error(allocator, "Failed to parse file: {}\n{}", .{
                            err,
                            parser.diagnostic,
                        });
                        return;
                    };
                };

                switch (event) {
                    .trace_event => |trace_event| {
                        const trace2 = tracy.traceNamed(@src(), "profile.handleTraceEvent()");
                        defer trace2.end();
                        profile.handleTraceEvent(trace_event) catch |err| {
                            send_load_error(allocator, "Failed to handle trace event: {}\n{}", .{
                                err,
                                parser.diagnostic,
                            });
                            return;
                        };
                    },
                    .none => break,
                }
            }

            if (is_gz) {
                if (stream.avail_out != 0) {
                    break;
                }
            } else {
                break;
            }
        }

        offset += file_bytes_read;

        _ = c.SDL_PushEvent(@constCast(&c.SDL_Event{
            .user = .{
                .type = user_event.load_progress,
                .data1 = @ptrFromInt(offset),
                .data2 = @ptrFromInt(file_size),
            },
        }));

        if (is_gz) {
            if (is_stream_end) {
                break;
            }
        } else {
            if (file_bytes_read == 0) {
                break;
            }
        }
    }

    {
        const trace1 = tracy.traceNamed(@src(), "profile.done()");
        defer trace1.end();
        profile.done() catch |err| {
            send_load_error(allocator, "Failed to finalize profile: {}", .{err});
            return;
        };
    }

    _ = c.SDL_PushEvent(@constCast(&c.SDL_Event{
        .user = .{
            .type = user_event.load_done,
            .data1 = profile,
        },
    }));

    {
        const seconds = get_seconds_elapsed(start_counter, c.SDL_GetPerformanceCounter(), c.SDL_GetPerformanceFrequency());
        const processed_mb = @as(f32, @floatFromInt(processed_bytes)) / 1000.0 / 1000.0;
        const speed = processed_mb / seconds;
        std.log.info("Loaded {d:.2}MB in {d:.2} seconds. {d:.2} MB/s", .{ processed_mb, seconds, speed });
    }
}

pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    var tracy_allocator = tracy.tracyAllocator(gpa.allocator());
    var count_allocator = CountAllocator.init(tracy_allocator.allocator());
    var allocator = count_allocator.allocator();

    _ = c.SDL_Init(c.SDL_INIT_EVERYTHING);

    user_event.load_started = c.SDL_RegisterEvents(1);
    user_event.load_progress = c.SDL_RegisterEvents(1);
    user_event.load_error = c.SDL_RegisterEvents(1);
    user_event.load_done = c.SDL_RegisterEvents(1);

    const window = c.SDL_CreateWindow(
        "ztracing",
        c.SDL_WINDOWPOS_CENTERED,
        c.SDL_WINDOWPOS_CENTERED,
        1280,
        720,
        c.SDL_WINDOW_ALLOW_HIGHDPI | c.SDL_WINDOW_HIDDEN | c.SDL_WINDOW_RESIZABLE | c.SDL_WINDOW_MAXIMIZED,
    ).?;

    const renderer = c.SDL_CreateRenderer(
        window,
        0,
        c.SDL_RENDERER_ACCELERATED | c.SDL_RENDERER_PRESENTVSYNC,
    ).?;

    c.igSetAllocatorFunctions(imgui.alloc, imgui.free, &allocator);

    _ = c.igCreateContext(null);

    const io = c.igGetIO();
    {
        const style = c.igGetStyle();
        c.igStyleColorsLight(style);

        style.*.ScrollbarRounding = 0.0;
        style.*.ScrollbarSize = 18.0;

        style.*.SeparatorTextBorderSize = 1.0;
    }
    io.*.ConfigFlags |= c.ImGuiConfigFlags_DockingEnable;

    _ = c.ig_ImplSDL2_InitForSDLRenderer(window, renderer);
    _ = c.ig_ImplSDLRenderer2_Init(renderer);

    var tracing = Tracing.init(&count_allocator, null);
    var load_thread: ?std.Thread = null;

    c.SDL_ShowWindow(window);

    const perf_frequency = c.SDL_GetPerformanceFrequency();
    var last_counter = c.SDL_GetPerformanceCounter();
    var running = true;
    while (running) {
        var event: c.SDL_Event = undefined;
        while (c.SDL_PollEvent(&event) == 1) {
            if (c.ig_ImplSDL2_ProcessEvent(&event)) {
                continue;
            }

            switch (event.type) {
                c.SDL_QUIT => {
                    running = false;
                    break;
                },
                c.SDL_DROPFILE => {
                    if (tracing.should_load_file() and load_thread == null) {
                        const len = std.mem.len(event.drop.file);
                        const path = try allocator.dupe(u8, event.drop.file[0..len]);
                        load_thread = try std.Thread.spawn(.{}, load_thread_main, .{ allocator, path });
                    }
                },
                else => {
                    if (event.type == user_event.load_started) {
                        tracing.on_load_file_start();
                    } else if (event.type == user_event.load_progress) {
                        tracing.on_load_file_progress(@intFromPtr(event.user.data1), @intFromPtr(event.user.data2));
                    } else if (event.type == user_event.load_done) {
                        tracing.on_load_file_done(@ptrCast(@alignCast(event.user.data1)));

                        load_thread.?.join();
                        load_thread = null;
                    } else if (event.type == user_event.load_error) {
                        const msg_ptr: [*c]u8 = @ptrCast(event.user.data1);
                        const msg_len: usize = @intFromPtr(event.user.data2);
                        const msg = msg_ptr[0..msg_len];
                        defer if (event.user.code != 0) {
                            allocator.free(msg);
                        };
                        tracing.on_load_file_error(msg);

                        load_thread.?.join();
                        load_thread = null;
                    }
                },
            }
        }

        c.ig_ImplSDLRenderer2_NewFrame();
        c.ig_ImplSDL2_NewFrame();
        c.igNewFrame();

        const current_counter = c.SDL_GetPerformanceCounter();
        const dt = get_seconds_elapsed(last_counter, current_counter, perf_frequency);
        tracing.update(dt);
        last_counter = current_counter;

        c.igRender();

        c.ig_ImplSDLRenderer2_RenderDrawData(c.igGetDrawData());
        c.SDL_RenderPresent(renderer);

        tracy.frameMark();
    }
}
