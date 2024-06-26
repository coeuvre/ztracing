const std = @import("std");
const c = @import("c.zig");
const ig = @import("imgui.zig");
const tracy = @import("tracy.zig");
const builtin = @import("builtin");
const assets = @import("assets");

const is_windows = builtin.target.os.tag == .windows;

const Tracing = @import("tracing.zig").Tracing;
const Allocator = std.mem.Allocator;
const JsonProfileParser = @import("json_profile_parser.zig").JsonProfileParser;
const Profile = @import("profile.zig").Profile;
const Arena = @import("arena.zig").SimpleArena;
const CountAllocator = @import("count_alloc.zig").CountAllocator;

var gpa = std.heap.GeneralPurposeAllocator(.{}){};
var count_allocator = CountAllocator.init(gpa.allocator());
const root_allocator = count_allocator.allocator();

var imgui_tracy_allocator = tracy.TracyAllocator("ImGui").init(root_allocator);
var imgui_allocator = imgui_tracy_allocator.allocator();

var sdl_tracy_allocator = tracy.TracyAllocator("SDL").init(root_allocator);
var sdl_allocator = sdl_tracy_allocator.allocator();

fn get_seconds_elapsed(from: usize, to: usize, freq: usize) f32 {
    return @floatCast(@as(f64, @floatFromInt(to - from)) / @as(f64, @floatFromInt(freq)));
}

fn zalloc(userdata: ?*anyopaque, items: c_uint, size: c_uint) callconv(.C) ?*anyopaque {
    const allocator: *Allocator = @ptrCast(@alignCast(userdata));
    return c.memory.malloc(allocator.*, items * size);
}

fn zfree(userdata: ?*anyopaque, address: ?*anyopaque) callconv(.C) void {
    const allocator: *Allocator = @ptrCast(@alignCast(userdata));
    c.memory.free(allocator.*, address);
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
    var profile_tracy_allocator = tracy.TracyAllocator("Profile").init(root_allocator);
    var profile_arena: ?Arena = null;

    tracy.set_thread_name("Load File Thread");

    while (true) {
        const event = load_thread_channel.get();
        switch (event) {
            .load_file => |l| {
                if (profile_arena) |arena| {
                    arena.deinit();
                }
                const parent_allocator = profile_tracy_allocator.allocator();
                profile_arena = Arena.init(parent_allocator);
                load_file(parent_allocator, &profile_arena.?, l.path);
            },
        }
    }
}

fn InflateStream(comptime ReaderType: type) type {
    return struct {
        const Self = @This();
        const ReadError = ReaderType.Error || error{
            inflate_init_error,
            inflate_error,
        };
        pub const Reader = std.io.Reader(*Self, ReadError, read);

        input_reader: ReaderType,
        allocator: Allocator,

        input_buf: [4096]u8 = undefined,
        input_processed: usize = 0,
        stream: ?c.z_stream = null,

        pub fn init(allocator: Allocator, reader: ReaderType) Self {
            return .{
                .allocator = allocator,
                .input_reader = reader,
            };
        }

        pub fn read(self: *Self, buf: []u8) ReadError!usize {
            if (self.stream == null) {
                self.stream = c.z_stream{
                    .zalloc = zalloc,
                    .zfree = zfree,
                    .@"opaque" = @constCast(&self.allocator),
                };

                // Automatically detect header
                const ret = c.inflateInit2(&self.stream.?, c.MAX_WBITS | 32);
                if (ret != c.Z_OK) {
                    return error.inflate_init_error;
                }
            }

            const stream = &self.stream.?;
            if (stream.avail_in == 0) {
                stream.avail_in = @intCast(try self.input_reader.read(&self.input_buf));
                stream.next_in = &self.input_buf;
                self.input_processed += stream.avail_in;
            }
            if (stream.avail_in == 0) {
                return 0;
            }

            stream.avail_out = @intCast(buf.len);
            stream.next_out = buf.ptr;

            switch (c.inflate(stream, c.Z_NO_FLUSH)) {
                c.Z_OK => {
                    return buf.len - stream.avail_out;
                },
                c.Z_STREAM_END => return 0,
                else => |ret| {
                    std.log.err("inflate returned {}", .{ret});
                    return error.inflate_error;
                },
            }
        }
    };
}

fn load_file(parent_allocator: Allocator, profile_arena: *Arena, path: []const u8) void {
    const trace = tracy.traceNamed(@src(), "load_file");
    defer trace.end();

    var temp_arena = Arena.init(parent_allocator);
    defer {
        temp_arena.deinit();
    }

    const profile_allocator = profile_arena.allocator();
    const temp_allocator = temp_arena.allocator();

    const start_counter = c.SDL_GetPerformanceCounter();

    const file = std.fs.cwd().openFile(path, .{}) catch |err| {
        send_load_error(profile_allocator, "Failed to open file {s}: {}", .{ path, err });
        return;
    };
    defer file.close();
    const stat = file.stat() catch |err| {
        send_load_error(profile_allocator, "Failed to stat file {s}: {}", .{ path, err });
        return;
    };
    const file_size = stat.size;

    var processed_bytes: usize = 0;
    var offset: usize = 0;

    var header = std.mem.zeroes([2]u8);
    const is_gz = blk: {
        const bytes_read = file.read(&header) catch |err| {
            send_load_error(profile_allocator, "Failed to read file: {}", .{err});
            return;
        };
        file.seekTo(0) catch |err| {
            send_load_error(profile_allocator, "Failed to seek file: {}", .{err});
            return;
        };
        break :blk bytes_read == 2 and (header[0] == 0x1F and header[1] == 0x8B);
    };

    var inflate_reader = blk: {
        if (is_gz) {
            break :blk InflateStream(std.fs.File.Reader).init(temp_allocator, file.reader());
        } else {
            break :blk null;
        }
    };

    var parser = JsonProfileParser.init(temp_allocator);
    var profile = profile_allocator.create(Profile) catch |err| {
        send_load_error(profile_allocator, "Failed to allocate profile: {}", .{err});
        return;
    };
    profile.* = Profile.init(profile_arena.allocator());

    var buf = temp_allocator.alloc(u8, 4096) catch |err| {
        send_load_error(profile_allocator, "Failed to allocate file_buf: {}", .{err});
        return;
    };
    while (true) {
        const trace1 = tracy.traceNamed(@src(), "load chunk");
        defer trace1.end();

        const bytes_to_process = blk: {
            if (inflate_reader) |*reader| {
                offset = reader.input_processed;
                break :blk reader.read(buf) catch |err| {
                    send_load_error(profile_allocator, "Failed to read file {s}: {}", .{ path, err });
                    return;
                };
            } else {
                const nread = file.read(buf) catch |err| {
                    send_load_error(profile_allocator, "Failed to read file {s}: {}", .{ path, err });
                    return;
                };
                offset += nread;
                break :blk nread;
            }
        };

        if (bytes_to_process == 0) {
            parser.endInput();
        } else {
            parser.feedInput(buf[0..bytes_to_process]);
            processed_bytes += bytes_to_process;
        }

        while (!parser.done()) {
            const event = blk: {
                const trace2 = tracy.traceNamed(@src(), "parser.next()");
                defer trace2.end();
                break :blk parser.next() catch |err| {
                    send_load_error(profile_allocator, "Failed to parse file: {}\n{}", .{
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
                        send_load_error(profile_allocator, "Failed to handle trace event: {}\n{}", .{
                            err,
                            parser.diagnostic,
                        });
                        return;
                    };
                },
                .none => break,
            }
        }

        _ = c.SDL_PushEvent(@constCast(&c.SDL_Event{
            .user = .{
                .type = c.SDL_USEREVENT,
                .code = @intFromEnum(UserEventCode.load_progress),
                .data1 = @ptrFromInt(offset),
                .data2 = @ptrFromInt(file_size),
            },
        }));

        if (bytes_to_process == 0) {
            break;
        }
    }

    {
        const seconds = get_seconds_elapsed(start_counter, c.SDL_GetPerformanceCounter(), c.SDL_GetPerformanceFrequency());
        const processed_mb = @as(f32, @floatFromInt(processed_bytes)) / 1000.0 / 1000.0;
        const speed = processed_mb / seconds;
        std.log.info("Loaded {d:.2}MB in {d:.2} seconds. {d:.2} MB/s", .{ processed_mb, seconds, speed });
    }

    {
        const trace1 = tracy.traceNamed(@src(), "profile.done()");
        defer trace1.end();

        const counter = c.SDL_GetPerformanceCounter();
        const bytes_before = get_memory_usages();
        profile.done() catch |err| {
            send_load_error(profile_allocator, "Failed to finalize profile: {}", .{err});
            return;
        };
        const seconds = get_seconds_elapsed(counter, c.SDL_GetPerformanceCounter(), c.SDL_GetPerformanceFrequency());
        const allocated_mb = @as(f32, @floatFromInt(get_memory_usages() - bytes_before)) / 1000.0 / 1000.0;
        std.log.info("Prepared view in {d:.2} seconds, allocated {d:.2}MB.", .{ seconds, allocated_mb });
    }

    _ = c.SDL_PushEvent(@constCast(&c.SDL_Event{
        .user = .{
            .type = c.SDL_USEREVENT,
            .code = @intFromEnum(UserEventCode.load_done),
            .data1 = profile,
        },
    }));
}

fn get_dpi_scale(window: *c.SDL_Window) f32 {
    var result: f32 = 1.0;

    if (c.SDL_GetDisplayDPI(c.SDL_GetWindowDisplayIndex(window), &result, null, null) == 0) {
        result /= 96.0;
    }

    return result;
}

fn sdl_malloc(size: usize) callconv(.C) ?*anyopaque {
    return c.memory.malloc(sdl_allocator, size);
}

fn sdl_calloc(count: usize, size: usize) callconv(.C) ?*anyopaque {
    return c.memory.calloc(sdl_allocator, count, size);
}

fn sdl_realloc(mem: ?*anyopaque, size: usize) callconv(.C) ?*anyopaque {
    return c.memory.realloc(sdl_allocator, mem, size);
}

fn sdl_free(ptr: ?*anyopaque) callconv(.C) void {
    c.memory.free(sdl_allocator, ptr);
}

fn get_memory_usages() usize {
    return count_allocator.get_allocated_bytes();
}

pub fn main() !void {
    var tracy_allocator = tracy.tracyAllocator(root_allocator);
    var allocator = tracy_allocator.allocator();

    _ = c.SDL_SetMemoryFunctions(
        sdl_malloc,
        sdl_calloc,
        sdl_realloc,
        sdl_free,
    );

    c.igSetAllocatorFunctions(ig.alloc, ig.free, @ptrCast(&imgui_allocator));

    _ = try std.Thread.spawn(.{}, load_thread_main, .{});

    if (comptime is_windows) {
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
        c.SDL_WINDOW_HIDDEN | c.SDL_WINDOW_RESIZABLE | c.SDL_WINDOW_MAXIMIZED,
    ).?;

    // TODO: detect DPI change
    const dpi = get_dpi_scale(window);

    // Manually scale the window if dpi is > 1 when window size is in pixels (e.g. on Windows),
    if (dpi > 1) {
        const new_window_w: c_int = @intFromFloat(@round(@as(f32, @floatFromInt(window_w)) * dpi));
        const new_window_h: c_int = @intFromFloat(@round(@as(f32, @floatFromInt(window_h)) * dpi));
        c.SDL_SetWindowSize(window, new_window_w, new_window_h);
    }

    const renderer = c.SDL_CreateRenderer(
        window,
        0,
        c.SDL_RENDERER_ACCELERATED | c.SDL_RENDERER_PRESENTVSYNC,
    ).?;

    _ = c.igCreateContext(null);

    const io = c.igGetIO();
    io.*.IniFilename = null;

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

        style.*.ScrollbarRounding = 0.0;
        style.*.ScrollbarSize = 18.0;

        style.*.SeparatorTextBorderSize = 1.0;

        c.ImGuiStyle_ScaleAllSizes(style, dpi);
    }
    io.*.ConfigFlags |= c.ImGuiConfigFlags_DockingEnable;

    _ = c.ig_ImplSDL2_InitForSDLRenderer(window, renderer);
    _ = c.ig_ImplSDLRenderer2_Init(renderer);

    var tracing = Tracing.init(allocator, .{
        .show_open_file_picker = null,
        .get_memory_usages = get_memory_usages,
    });
    var load_path: ?[]u8 = null;
    {
        const args = try std.process.argsAlloc(allocator);
        defer std.process.argsFree(allocator, args);

        if (args.len >= 2) {
            if (tracing.should_load_file()) {
                load_path = try allocator.dupe(u8, args[1]);

                tracing.on_load_file_start(std.fs.path.basename(load_path.?));

                load_thread_channel.put(.{ .load_file = .{ .path = load_path.? } });
            }
        }
    }

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
                        const len = std.mem.len(event.drop.file);
                        load_path = try allocator.dupe(u8, event.drop.file[0..len]);

                        tracing.on_load_file_start(std.fs.path.basename(load_path.?));

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
