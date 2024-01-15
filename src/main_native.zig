const std = @import("std");
const c = @import("c.zig");
const imgui = @import("imgui.zig");
const tracy = @import("tracy.zig");
const builtin = @import("builtin");
const assets = @import("assets");
const mimalloc = @import("mimalloc.zig");

const is_window = builtin.target.os.tag == .windows;

const Tracing = @import("tracing.zig").Tracing;
const Allocator = std.mem.Allocator;
const JsonProfileParser = @import("json_profile_parser.zig").JsonProfileParser;
const Profile = @import("profile.zig").Profile;
const Arena = @import("arena.zig").Arena;

fn get_seconds_elapsed(from: usize, to: usize, freq: usize) f32 {
    return @floatCast(@as(f64, @floatFromInt(to - from)) / @as(f64, @floatFromInt(freq)));
}

fn zalloc(userdata: ?*anyopaque, items: c_uint, size: c_uint) callconv(.C) ?*anyopaque {
    return imgui.alloc(items * size, userdata);
}

fn zfree(userdata: ?*anyopaque, address: ?*anyopaque) callconv(.C) void {
    imgui.free(address, userdata);
}

const UserEventCode = enum {
    load_progress,
    load_error,
    load_done,
};

fn send_load_error(allocator: Allocator, comptime fmt: []const u8, args: anytype) void {
    const msg = std.fmt.allocPrint(allocator, fmt, args) catch unreachable;
    _ = c.SDL_PushEvent(@constCast(&c.SDL_Event{
        .user = .{
            .type = c.SDL_USEREVENT,
            .code = @intFromEnum(UserEventCode.load_error),
            .data1 = @constCast(msg.ptr),
            .data2 = @ptrFromInt(msg.len),
        },
    }));
}

const HeapArena = struct {
    pub fn init(heap: mimalloc.Heap) Arena {
        return .{
            .ptr = heap.raw,
            .vtable = &.{
                .allocator = allocator,
                .deinit = deinit,
            },
        };
    }

    fn allocator(p: *anyopaque) Allocator {
        const heap: mimalloc.Heap = mimalloc.Heap.from_raw(@ptrCast(@alignCast(p)));
        return heap.allocator();
    }

    fn deinit(p: *anyopaque) void {
        const heap: mimalloc.Heap = mimalloc.Heap.from_raw(@ptrCast(@alignCast(p)));
        heap.destroy();
    }
};

const Event = union(enum) {
    load_file: struct {
        path: []const u8,
    },
};

fn Channel(comptime T: type) type {
    return struct {
        const Self = @This();

        producer_sem: std.Thread.Semaphore,
        consumer_sem: std.Thread.Semaphore,
        item: ?T,

        pub fn init() Self {
            return .{
                .producer_sem = std.Thread.Semaphore{ .permits = 1 },
                .consumer_sem = std.Thread.Semaphore{ .permits = 0 },
                .item = null,
            };
        }

        pub fn put(self: *Self, item: T) void {
            self.producer_sem.wait();
            std.debug.assert(self.item == null);
            self.item = item;
            self.consumer_sem.post();
        }

        pub fn get(self: *Self) T {
            self.consumer_sem.wait();
            std.debug.assert(self.item != null);
            const item = self.item.?;
            self.item = null;
            self.producer_sem.post();
            return item;
        }
    };
}

var load_thread_channel: Channel(Event) = Channel(Event).init();

fn load_thread_main() void {
    var load_file_heap: ?mimalloc.Heap = null;

    tracy.set_thread_name("Load File Thread");

    while (true) {
        const event = load_thread_channel.get();
        switch (event) {
            .load_file => |l| {
                if (load_file_heap) |heap| {
                    heap.destroy();
                }
                load_file_heap = mimalloc.Heap.new();
                load_file(load_file_heap.?, l.path);
            },
        }
    }
}

fn load_file(heap: mimalloc.Heap, path: []const u8) void {
    const allocator = heap.allocator();

    const trace = tracy.traceNamed(@src(), "load_file");
    defer trace.end();

    const start_counter = c.SDL_GetPerformanceCounter();

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
    profile.* = Profile.init(HeapArena.init(heap));

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
                        const trace2 = tracy.traceNamed(@src(), "profile.handle_trace_event()");
                        defer trace2.end();
                        profile.handle_trace_event(trace_event) catch |err| {
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
                .type = c.SDL_USEREVENT,
                .code = @intFromEnum(UserEventCode.load_progress),
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
            .type = c.SDL_USEREVENT,
            .code = @intFromEnum(UserEventCode.load_done),
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

fn get_dpi_scale(window: *c.SDL_Window) f32 {
    if (comptime is_window) {
        var ddpi: f32 = 0;
        if (c.SDL_GetDisplayDPI(c.SDL_GetWindowDisplayIndex(window), &ddpi, null, null) == 0) {
            return ddpi / 96.0;
        }
    }

    return 1.0;
}

fn ig_alloc(size: usize, _: ?*anyopaque) callconv(.C) ?*anyopaque {
    const ptr = c.mi_malloc(size);
    if (ptr) |p| {
        tracy.allocNamed(@ptrCast(p), c.mi_malloc_size(p), "imgui");
    }
    return ptr;
}

fn ig_free(ptr: ?*anyopaque, _: ?*anyopaque) callconv(.C) void {
    if (ptr) |p| {
        tracy.freeNamed(@ptrCast(p), "imgui");
    }
    c.mi_free(ptr);
}

fn sdl_malloc(size: usize) callconv(.C) ?*anyopaque {
    const ptr = c.mi_malloc(size);
    if (ptr) |p| {
        tracy.allocNamed(@ptrCast(p), size, "SDL");
    }
    return ptr;
}

fn sdl_calloc(count: usize, size: usize) callconv(.C) ?*anyopaque {
    const ptr = c.mi_calloc(count, size);
    if (ptr) |p| {
        tracy.allocNamed(@ptrCast(p), count * size, "SDL");
    }
    return ptr;
}

fn sdl_realloc(mem: ?*anyopaque, size: usize) callconv(.C) ?*anyopaque {
    if (mem) |p| {
        tracy.freeNamed(@ptrCast(p), "SDL");
    }

    const ptr = c.mi_realloc(mem, size);
    if (ptr) |p| {
        tracy.allocNamed(@ptrCast(p), size, "SDL");
    }
    return ptr;
}

fn sdl_free(ptr: ?*anyopaque) callconv(.C) void {
    if (ptr) |p| {
        tracy.freeNamed(@ptrCast(p), "SDL");
    }
    c.mi_free(ptr);
}

fn get_memory_usages() usize {
    var current_rss: usize = 0;
    c.mi_process_info(null, null, null, &current_rss, null, null, null, null);
    return current_rss;
}

pub fn main() !void {
    var tracy_allocator = tracy.tracyAllocator(mimalloc.allocator());
    var allocator = tracy_allocator.allocator();

    _ = try std.Thread.spawn(.{}, load_thread_main, .{});

    _ = c.SDL_SetMemoryFunctions(
        sdl_malloc,
        sdl_calloc,
        sdl_realloc,
        sdl_free,
    );

    if (comptime is_window) {
        // Allow Windows to scale up window, but still let SDL uses coordinate in pixels.
        _ = c.SDL_SetHint(c.SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    }

    _ = c.SDL_Init(c.SDL_INIT_EVERYTHING);

    const window_w: c_int = 1280;
    const window_h: c_int = 720;
    const window = c.SDL_CreateWindow(
        "ztracing",
        c.SDL_WINDOWPOS_CENTERED,
        c.SDL_WINDOWPOS_CENTERED,
        window_w,
        window_h,
        c.SDL_WINDOW_HIDDEN | c.SDL_WINDOW_RESIZABLE,
    ).?;

    // TODO: detect DPI change
    const dpi = get_dpi_scale(window);

    // Manually scale the window if dpi is > 1 when window size is in pixels (e.g. on Windows),
    if (comptime is_window) {
        if (dpi > 1) {
            const new_window_w: c_int = @intFromFloat(@round(@as(f32, @floatFromInt(window_w)) * dpi));
            const new_window_h: c_int = @intFromFloat(@round(@as(f32, @floatFromInt(window_h)) * dpi));
            c.SDL_SetWindowSize(window, new_window_w, new_window_h);
        }
    }

    const renderer = c.SDL_CreateRenderer(
        window,
        0,
        c.SDL_RENDERER_ACCELERATED | c.SDL_RENDERER_PRESENTVSYNC,
    ).?;

    c.igSetAllocatorFunctions(ig_alloc, ig_free, null);

    _ = c.igCreateContext(null);

    const io = c.igGetIO();

    {
        const font = c.ImFontAtlas_AddFontFromMemoryTTF(
            io.*.Fonts,
            @constCast(assets.font.ptr),
            assets.font.len,
            @round(15.0 * dpi),
            null,
            null,
        );
        std.debug.assert(font != null);
    }

    {
        const style = c.igGetStyle();
        c.igStyleColorsLight(style);
        c.ImGuiStyle_ScaleAllSizes(style, dpi);

        style.*.ScrollbarRounding = 0.0;
        style.*.ScrollbarSize = 18.0;

        style.*.SeparatorTextBorderSize = 1.0;
    }
    io.*.ConfigFlags |= c.ImGuiConfigFlags_DockingEnable;

    _ = c.ig_ImplSDL2_InitForSDLRenderer(window, renderer);
    _ = c.ig_ImplSDLRenderer2_Init(renderer);

    var tracing = Tracing.init(allocator, .{
        .show_open_file_picker = null,
        .get_memory_usages = get_memory_usages,
    });
    var load_path: ?[]u8 = null;

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
                    defer c.SDL_free(event.drop.file);
                    if (tracing.should_load_file()) {
                        tracing.on_load_file_start();

                        const len = std.mem.len(event.drop.file);
                        load_path = try allocator.dupe(u8, event.drop.file[0..len]);
                        load_thread_channel.put(.{ .load_file = .{ .path = load_path.? } });
                    }
                },
                c.SDL_USEREVENT => {
                    const code: UserEventCode = @enumFromInt(event.user.code);
                    switch (code) {
                        .load_progress => {
                            tracing.on_load_file_progress(@intFromPtr(event.user.data1), @intFromPtr(event.user.data2));
                        },
                        .load_done => {
                            tracing.on_load_file_done(@ptrCast(@alignCast(event.user.data1)));

                            allocator.free(load_path.?);
                            load_path = null;
                        },
                        .load_error => {
                            const msg_ptr: [*c]u8 = @ptrCast(event.user.data1);
                            const msg_len: usize = @intFromPtr(event.user.data2);
                            const msg = msg_ptr[0..msg_len];
                            defer allocator.free(msg);
                            tracing.on_load_file_error(msg);

                            allocator.free(load_path.?);
                            load_path = null;
                        },
                    }
                },
                else => {},
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
