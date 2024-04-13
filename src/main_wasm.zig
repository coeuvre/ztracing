const std = @import("std");
const builtin = @import("builtin");
const c = @import("c.zig");
const ig = @import("imgui.zig");
const software_renderer = @import("./software_renderer.zig");
const imgui = @import("imgui.zig");
const mq = @import("mq.zig");

const Allocator = std.mem.Allocator;
const Tracing = @import("tracing.zig").Tracing;
const JsonProfileParser = @import("json_profile_parser.zig").JsonProfileParser;
const Profile = @import("profile.zig").Profile;
const Arena = @import("arena.zig").SimpleArena;
const MessageQueue = mq.MessageQueue;

pub const std_options = std.Options{
    .logFn = log,
    .log_level = switch (builtin.mode) {
        .Debug => .debug,
        else => .info,
    },
};

fn log(
    comptime message_level: std.log.Level,
    comptime scope: @Type(.EnumLiteral),
    comptime format: []const u8,
    args: anytype,
) void {
    const level = @intFromEnum(message_level);
    const prefix2 = if (scope == .default) "" else "[" ++ @tagName(scope) ++ "] ";
    var buf: [256]u8 = undefined;
    const msg = std.fmt.bufPrint(&buf, prefix2 ++ format ++ "\n", args) catch return;
    js.log(level, msg.ptr, msg.len);
}

const js = struct {
    pub const JsObject = extern struct {
        ref: i64,
    };

    pub extern "js" fn log(level: u32, ptr: [*]const u8, len: usize) void;

    pub extern "js" fn destory(obj: JsObject) void;

    pub extern "js" fn showOpenFilePicker() void;

    pub extern "js" fn rendererCreateFontTexture(width: i32, height: i32, pixels: [*]const u8) JsObject;

    pub extern "js" fn rendererBufferData(vtx_buffer_ptr: [*]const u8, vtx_buffer_len: i32, idx_buffer_ptr: [*]const u8, idx_buffer_len: i32) void;

    pub extern "js" fn rendererDraw(clip_rect_min_x: f32, clip_rect_min_y: f32, clip_rect_max_x: f32, clip_rect_max_y: f32, texture_ref: JsObject, idx_count: u32, idx_offset: u32) void;

    pub extern "js" fn get_current_timestamp() usize;
};

fn jsButtonToImguiButton(button: i32) i32 {
    return switch (button) {
        1 => 2,
        2 => 1,
        3 => 4,
        4 => 3,
        else => button,
    };
}

fn show_open_file_picker() void {
    js.showOpenFilePicker();
}

const LoadState = struct {
    const Chunk = struct {
        offset: usize,
        buf: []u8,
    };

    const Status = union(enum) {
        loading: struct {
            offset: usize,
            total: usize,
        },
        err: []u8,
        done: struct {
            profile: *Profile,
            processed_bytes: usize,
        },
    };

    allocator: Allocator,
    chunk_queue: MessageQueue(Chunk),
    arena: *Arena,
    total: usize,
    parser: JsonProfileParser,
    profile: *Profile,
    processed_bytes: usize,

    status: Status,

    mutex: mq.Mutex = .{},

    pub fn init(allocator: Allocator, arena: *Arena, total: usize) LoadState {
        const profile = arena.allocator().create(Profile) catch unreachable;
        profile.* = Profile.init(arena.allocator());
        return .{
            .allocator = allocator,
            .chunk_queue = MessageQueue(Chunk).init(arena.allocator()),
            .arena = arena,
            .total = total,
            .parser = JsonProfileParser.init(allocator),
            .profile = profile,
            .processed_bytes = 0,
            .status = .{ .loading = .{ .offset = 0, .total = total } },
        };
    }

    pub fn put_chunk(self: *LoadState, offset: usize, chunk: []u8) void {
        self.chunk_queue.put(.{ .offset = offset, .buf = chunk });
    }

    pub fn get_load_status(self: *LoadState) Status {
        self.mutex.lock();
        defer self.mutex.unlock();

        return self.status;
    }

    fn load(self: *LoadState) void {
        while (true) {
            const chunk = self.chunk_queue.get();
            if (!self.load_one_chunk(chunk.offset, chunk.buf)) {
                break;
            }
        }
    }

    fn load_one_chunk(self: *LoadState, offset: usize, input: []u8) bool {
        defer if (input.len > 0) {
            self.allocator.free(input);
        };

        self.processed_bytes += input.len;
        if (input.len == 0) {
            self.parser.endInput();
        } else {
            self.parser.feedInput(input);
        }

        while (!self.parser.done()) {
            const event = self.parser.next() catch |err| {
                self.set_load_error("Failed to parse file: {}\n{}", .{
                    err,
                    self.parser.diagnostic,
                });
                return false;
            };

            switch (event) {
                .trace_event => |trace_event| {
                    self.profile.handle_trace_event(trace_event) catch |err| {
                        self.set_load_error("Failed to handle trace event: {}\n{}", .{
                            err,
                            self.parser.diagnostic,
                        });
                        return false;
                    };
                },
                .none => break,
            }
        }

        if (input.len == 0) {
            self.profile.done() catch |err| {
                self.set_load_error("Failed to finalize profile: {}", .{
                    err,
                });
                return false;
            };

            self.parser.deinit();
            self.set_load_done();
            return false;
        }

        self.set_load_progress(offset);
        return true;
    }

    fn set_load_error(self: *LoadState, comptime fmt: []const u8, args: anytype) void {
        const msg = std.fmt.allocPrint(self.allocator, fmt, args) catch unreachable;

        self.mutex.lock();
        defer self.mutex.unlock();

        self.status = .{ .err = msg };
    }

    fn set_load_progress(self: *LoadState, offset: usize) void {
        self.mutex.lock();
        defer self.mutex.unlock();

        self.status = .{ .loading = .{ .offset = offset, .total = self.total } };
    }

    fn set_load_done(self: *LoadState) void {
        self.mutex.lock();
        defer self.mutex.unlock();

        self.status = .{ .done = .{ .profile = self.profile, .processed_bytes = self.processed_bytes } };
    }
};

fn get_memory_usages() usize {
    return global.gpa.total_requested_bytes;
}

const App = struct {
    allocator: Allocator,
    renderer: Renderer,
    tracing: Tracing,

    width: f32,
    height: f32,

    io: *c.ImGuiIO,
    mouse_pos_before_blur: c.ImVec2 = undefined,
    load_state: ?*LoadState,
    load_state_arena: Arena,
    load_start_at: usize,

    pub fn init(
        self: *App,
        allocator: Allocator,
        width: f32,
        height: f32,
        device_pixel_ratio: f32,
        font_data: ?[]u8,
        font_size: f32,
    ) void {
        self.allocator = allocator;
        self.renderer = .{};
        self.load_state = null;
        self.load_state_arena = Arena.init(allocator);
        self.tracing = Tracing.init(allocator, .{
            .show_open_file_picker = show_open_file_picker,
            .get_memory_usages = get_memory_usages,
        });

        self.width = width;
        self.height = height;

        c.igSetAllocatorFunctions(imgui.alloc, imgui.free, &self.allocator);

        _ = c.igCreateContext(null);

        const scale = device_pixel_ratio;

        const io = c.igGetIO();
        self.io = io;

        io.*.IniFilename = null;

        {
            const style = c.igGetStyle();
            c.igStyleColorsLight(style);

            style.*.ScrollbarRounding = 0.0;
            style.*.ScrollbarSize = 18.0;

            style.*.SeparatorTextBorderSize = 1.0;

            c.ImGuiStyle_ScaleAllSizes(style, scale);
        }

        io.*.ConfigFlags |= c.ImGuiConfigFlags_DockingEnable;

        io.*.DisplaySize.x = width;
        io.*.DisplaySize.y = height;

        if (font_data) |f| {
            _ = c.ImFontAtlas_AddFontFromMemoryTTF(
                io.*.Fonts,
                f.ptr,
                @intCast(f.len),
                @round(font_size * scale),
                null,
                null,
            );
        } else {
            io.*.FontGlobalScale = scale;
        }

        {
            var pixels: [*c]u8 = undefined;
            var w: i32 = undefined;
            var h: i32 = undefined;
            var bytes_per_pixel: i32 = undefined;
            c.ImFontAtlas_GetTexDataAsRGBA32(io.*.Fonts, &pixels, &w, &h, &bytes_per_pixel);
            std.debug.assert(bytes_per_pixel == 4);
            const tex = self.renderer.createFontTexture(w, h, pixels);
            c.ImFontAtlas_SetTexID(io.*.Fonts, tex);
        }
    }

    pub fn update(self: *App, dt: f32) void {
        self.io.*.DeltaTime = dt;

        c.igNewFrame();

        if (self.load_state) |load_state| {
            switch (load_state.get_load_status()) {
                .loading => |loading| {
                    self.tracing.on_load_file_progress(loading.offset, loading.total);
                },
                .err => |msg| {
                    defer self.allocator.free(msg);

                    self.tracing.on_load_file_error(msg);
                    self.allocator.destroy(load_state);
                    self.load_state = null;
                },
                .done => |done| {
                    self.tracing.on_load_file_done(done.profile);
                    self.allocator.destroy(load_state);
                    self.load_state = null;

                    const now = js.get_current_timestamp();
                    if (now > self.load_start_at) {
                        const seconds = @as(f32, @floatFromInt(now - self.load_start_at)) / 1000.0;
                        const processed_mb = @as(f32, @floatFromInt(done.processed_bytes)) / 1000.0 / 1000.0;
                        const speed = processed_mb / seconds;
                        std.log.info("Loaded {d:.2}MB in {d:.2} seconds. {d:.2} MB/s", .{ processed_mb, seconds, speed });
                    }
                },
            }
        }
        self.tracing.update(dt);

        c.igEndFrame();
        c.igRender();
        const draw_data = c.igGetDrawData();
        self.renderImgui(draw_data);
    }

    pub fn on_resize(self: *App, width: f32, height: f32) void {
        self.width = width;
        self.height = height;

        self.io.*.DisplaySize.x = width;
        self.io.*.DisplaySize.y = height;
    }

    pub fn onMousePos(self: *App, x: f32, y: f32) void {
        c.ImGuiIO_AddMousePosEvent(self.io, x, y);
    }

    pub fn onMouseButton(self: *App, button: i32, down: bool) bool {
        c.ImGuiIO_AddMouseButtonEvent(self.io, button, down);
        return self.io.*.WantCaptureMouse;
    }

    pub fn onMouseWheel(self: *App, dx: f32, dy: f32) void {
        c.ImGuiIO_AddMouseWheelEvent(self.io, dx / self.width * 10.0, -dy / self.height * 10.0);
    }

    pub fn onKey(self: *App, key: u32, down: bool) bool {
        c.ImGuiIO_AddKeyEvent(self.io, key, down);
        if (key == c.ImGuiKey_LeftCtrl or key == c.ImGuiKey_RightCtrl) {
            c.ImGuiIO_AddKeyEvent(self.io, c.ImGuiMod_Ctrl, down);
        } else if (key == c.ImGuiKey_LeftShift or key == c.ImGuiKey_RightShift) {
            c.ImGuiIO_AddKeyEvent(self.io, c.ImGuiMod_Shift, down);
        } else if (key == c.ImGuiKey_LeftAlt or key == c.ImGuiKey_RightAlt) {
            c.ImGuiIO_AddKeyEvent(self.io, c.ImGuiMod_Alt, down);
        } else if (key == c.ImGuiKey_LeftSuper or key == c.ImGuiKey_RightSuper) {
            c.ImGuiIO_AddKeyEvent(self.io, c.ImGuiMod_Super, down);
        }
        return self.io.*.WantCaptureKeyboard;
    }

    pub fn onFocus(self: *App, focused: bool) void {
        if (!focused) {
            self.mouse_pos_before_blur = self.io.*.MousePos;
        }
        c.ImGuiIO_AddFocusEvent(self.io, focused);

        if (focused) {
            self.onMousePos(self.mouse_pos_before_blur.x, self.mouse_pos_before_blur.y);
        }
    }

    fn renderImgui(self: *App, draw_data: *c.ImDrawData) void {
        if (!draw_data.*.Valid) {
            return;
        }

        // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
        const fb_width = draw_data.*.DisplaySize.x * draw_data.*.FramebufferScale.x;
        const fb_height = draw_data.*.DisplaySize.y * draw_data.*.FramebufferScale.y;
        if (fb_width <= 0 or fb_height <= 0) {
            return;
        }

        // Will project scissor/clipping rectangles into framebuffer space
        const clip_off_x = draw_data.*.DisplayPos.x;
        const clip_off_y = draw_data.*.DisplayPos.y;
        const clip_scale_x = draw_data.*.FramebufferScale.x;
        const clip_scale_y = draw_data.*.FramebufferScale.y;

        for (0..@intCast(draw_data.CmdListsCount)) |cmd_list_index| {
            const cmd_list = draw_data.*.CmdLists.Data[cmd_list_index];

            // const vtx_buffer_size = cmd_list.*.VtxBuffer.Size * @sizeOf(c.ImDrawVert);
            // const idx_buffer_size = cmd_list.*.IdxBuffer.Size * @sizeOf(c.ImDrawIdx);

            // js.rendererBufferData(@ptrCast(cmd_list.*.VtxBuffer.Data), vtx_buffer_size, @ptrCast(cmd_list.*.IdxBuffer.Data), idx_buffer_size);
            const vtx_buffer = cmd_list.*.VtxBuffer.Data[0..@intCast(cmd_list.*.VtxBuffer.Size)];
            const idx_buffer = cmd_list.*.IdxBuffer.Data[0..@intCast(cmd_list.*.IdxBuffer.Size)];
            self.renderer.bufferData(vtx_buffer, idx_buffer);

            for (0..@intCast(cmd_list.*.CmdBuffer.Size)) |cmd_index| {
                const cmd = &cmd_list.*.CmdBuffer.Data[cmd_index];

                // Project scissor/clipping rectangles into framebuffer space
                const clip_rect = c.ImRect{
                    .Min = .{ .x = (cmd.*.ClipRect.x - clip_off_x) * clip_scale_x, .y = (cmd.*.ClipRect.y - clip_off_y) * clip_scale_y },
                    .Max = .{ .x = (cmd.*.ClipRect.z - clip_off_x) * clip_scale_x, .y = (cmd.*.ClipRect.w - clip_off_y) * clip_scale_y },
                };
                if (clip_rect.Max.x <= clip_rect.Min.x or clip_rect.Max.y <= clip_rect.Min.y) {
                    continue;
                }

                const texture = c.ImDrawCmd_GetTexID(cmd);
                self.renderer.draw(clip_rect, texture, cmd.*.ElemCount, cmd.*.IdxOffset);
            }
        }
    }

    pub fn on_load_file_start(self: *App, len: usize, file_name: []const u8) void {
        self.tracing.on_load_file_start(file_name);

        std.debug.assert(self.load_state == null);
        self.load_state_arena.deinit();
        self.load_state_arena = Arena.init(self.allocator);
        const load_state = self.allocator.create(LoadState) catch unreachable;
        load_state.* = LoadState.init(self.allocator, &self.load_state_arena, len);
        global.queue.put(Task{ .load = load_state });

        self.load_state = load_state;
        self.load_start_at = js.get_current_timestamp();
    }

    pub fn on_load_file_chunk(self: *App, offset: usize, chunk: []u8) void {
        self.load_state.?.put_chunk(offset, chunk);
    }

    pub fn on_load_file_done(self: *App) void {
        self.load_state.?.put_chunk(0, &[0]u8{});
    }
};

const WebglRenderer = struct {
    fn createFontTexture(self: *WebglRenderer, width: i32, height: i32, pixels: [*]const u8) c.ImTextureID {
        _ = self;
        const tex = js.rendererCreateFontTexture(width, height, pixels);
        const addr: usize = @intCast(tex.ref);
        return @ptrFromInt(addr);
    }

    fn bufferData(self: *WebglRenderer, vtx_buffer: []const c.ImDrawVert, idx_buffer: []const c.ImDrawIdx) void {
        _ = self;
        std.debug.assert(@sizeOf(c.ImDrawIdx) == 4);
        js.rendererBufferData(
            @ptrCast(vtx_buffer.ptr),
            @intCast(vtx_buffer.len * @sizeOf(c.ImDrawVert)),
            @ptrCast(idx_buffer.ptr),
            @intCast(idx_buffer.len * 4),
        );
    }

    fn draw(self: *WebglRenderer, clip_rect: c.ImRect, texture: c.ImTextureID, idx_count: u32, idx_offset: u32) void {
        _ = self;
        const tex_ref = js.JsObject{
            .ref = @intCast(@intFromPtr(texture)),
        };
        js.rendererDraw(
            clip_rect.Min.x,
            clip_rect.Min.y,
            clip_rect.Max.x,
            clip_rect.Max.y,
            tex_ref,
            idx_count,
            idx_offset,
        );
    }
};

const Renderer = WebglRenderer;

const global = struct {
    var gpa = std.heap.GeneralPurposeAllocator(.{ .enable_memory_limit = true, .MutexType = mq.Mutex }){};
    var queue: MessageQueue(Task) = undefined;
};

const Task = union(enum) {
    load: *LoadState,
};

fn worker_main() !void {
    while (true) {
        const task = global.queue.get();
        switch (task) {
            .load => |s| {
                s.load();
            },
        }
    }
    return null;
}

export fn init(
    width: f32,
    height: f32,
    device_pixel_ratio: f32,
    font_ptr: ?[*]u8,
    font_len: usize,
    font_size: f32,
) *void {
    var allocator = global.gpa.allocator();
    global.queue = MessageQueue(Task).init(allocator);

    _ = std.Thread.spawn(.{ .allocator = allocator }, worker_main, .{}) catch unreachable;

    var app = allocator.create(App) catch unreachable;
    const font_data: ?[]u8 = blk: {
        if (font_ptr) |p| {
            break :blk p[0..font_len];
        } else {
            break :blk null;
        }
    };
    defer if (font_ptr) |p| free(p);

    app.init(
        allocator,
        width,
        height,
        device_pixel_ratio,
        font_data,
        font_size,
    );

    // HACK: Force ImGui to update the mosue cursor, otherwise it's in uninitialized state.
    app.onMousePos(0, 0);

    return @ptrCast(app);
}

export fn update(app_ptr: *void, dt: f32) void {
    const app: *App = @ptrCast(@alignCast(app_ptr));
    app.update(dt);
}

export fn on_resize(app_ptr: *void, width: f32, height: f32) void {
    const app: *App = @ptrCast(@alignCast(app_ptr));
    app.on_resize(width, height);
}

export fn onMousePos(app_ptr: *void, x: f32, y: f32) void {
    const app: *App = @ptrCast(@alignCast(app_ptr));
    app.onMousePos(x, y);
}

export fn onMouseButton(app_ptr: *void, button: i32, down: bool) bool {
    const app: *App = @ptrCast(@alignCast(app_ptr));
    return app.onMouseButton(jsButtonToImguiButton(button), down);
}

export fn onMouseWheel(app_ptr: *void, dx: f32, dy: f32) void {
    const app: *App = @ptrCast(@alignCast(app_ptr));
    app.onMouseWheel(dx, dy);
}

export fn onKey(app_ptr: *void, key: u32, down: bool) bool {
    const app: *App = @ptrCast(@alignCast(app_ptr));
    return app.onKey(key, down);
}

export fn onFocus(app_ptr: *void, focused: bool) void {
    const app: *App = @ptrCast(@alignCast(app_ptr));
    app.onFocus(focused);
}

export fn shouldLoadFile(
    app_ptr: *void,
) bool {
    const app: *App = @ptrCast(@alignCast(app_ptr));
    return app.tracing.should_load_file();
}

export fn onLoadFileStart(app_ptr: *void, len: usize, file_name_ptr: [*]u8, file_name_len: usize) void {
    defer free(@ptrCast(file_name_ptr));

    const app: *App = @ptrCast(@alignCast(app_ptr));
    const file_name = file_name_ptr[0..file_name_len];
    app.on_load_file_start(len, file_name);
}

export fn onLoadFileChunk(app_ptr: *void, offset: usize, chunk_ptr: [*]u8, chunk_len: usize) void {
    defer free(@ptrCast(chunk_ptr));

    const app: *App = @ptrCast(@alignCast(app_ptr));
    if (!app.tracing.should_load_file()) {
        return;
    }

    const chunk = app.allocator.dupe(u8, chunk_ptr[0..chunk_len]) catch unreachable;
    app.on_load_file_chunk(offset, chunk);
}

export fn onLoadFileDone(app_ptr: *void) void {
    const app: *App = @ptrCast(@alignCast(app_ptr));
    app.on_load_file_done();
}

export fn main(argc: i32, argv: i32) i32 {
    _ = argc;
    _ = argv;
    unreachable;
}

pub fn panic(msg: []const u8, error_return_trace: ?*std.builtin.StackTrace, ret_addr: ?usize) noreturn {
    _ = ret_addr;
    if (error_return_trace) |trace| {
        std.log.err("{s}\n{}", .{ msg, trace });
    } else {
        std.log.err("{s}", .{msg});
    }
    std.process.abort();
}

export fn alloc(size: usize) *void {
    return @ptrCast(c.memory.malloc(global.gpa.allocator(), size).?);
}

export fn free(ptr: *anyopaque) void {
    c.memory.free(global.gpa.allocator(), ptr);
}
