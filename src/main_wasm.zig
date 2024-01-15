const std = @import("std");
const c = @import("c.zig");
const ig = @import("imgui.zig");
const software_renderer = @import("./software_renderer.zig");
const imgui = @import("imgui.zig");

const log = std.log;
const Allocator = std.mem.Allocator;
const CountAllocator = @import("./count_alloc.zig").CountAllocator;
const Tracing = @import("tracing.zig").Tracing;
const JsonProfileParser = @import("json_profile_parser.zig").JsonProfileParser;
const Profile = @import("profile.zig").Profile;
const Arena = std.heap.ArenaAllocator;

pub const std_options = struct {
    pub fn logFn(
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
};

const js = struct {
    pub const JsObject = extern struct {
        ref: i64,
    };

    pub extern "js" fn log(level: u32, ptr: [*]const u8, len: usize) void;

    pub extern "js" fn destory(obj: JsObject) void;

    pub extern "js" fn showOpenFilePicker() void;

    pub extern "js" fn copy_uint8_array(chunk: JsObject, ptr: [*]const u8, len: usize) void;

    pub extern "js" fn rendererCreateFontTexture(width: i32, height: i32, pixels: [*]const u8) JsObject;

    pub extern "js" fn rendererBufferData(vtx_buffer_ptr: [*]const u8, vtx_buffer_len: i32, idx_buffer_ptr: [*]const u8, idx_buffer_len: i32) void;

    pub extern "js" fn rendererDraw(clip_rect_min_x: f32, clip_rect_min_y: f32, clip_rect_max_x: f32, clip_rect_max_y: f32, texture_ref: JsObject, idx_count: u32, idx_offset: u32) void;

    pub extern "js" fn rendererPresent(framebuffer_ptr: [*]const u8, framebuffer_len: usize, width: usize, height: usize) void;

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
    arena: *Arena,
    total: usize,
    parser: JsonProfileParser,
    profile: *Profile,
    tracing: *Tracing,
    start_timestamp: usize,
    processed_bytes: usize,

    pub fn init(parent_allocator: Allocator, total: usize, tracing: *Tracing) LoadState {
        const arena = parent_allocator.create(Arena) catch unreachable;
        arena.* = Arena.init(parent_allocator);

        const allocator = arena.allocator();
        const profile = allocator.create(Profile) catch unreachable;
        profile.* = Profile.init(arena);
        return .{
            .arena = arena,
            .total = total,
            .parser = JsonProfileParser.init(allocator),
            .profile = profile,
            .tracing = tracing,
            .start_timestamp = js.get_current_timestamp(),
            .processed_bytes = 0,
        };
    }

    pub fn deinit(self: *LoadState) void {
        const parent_allocator = self.arena.child_allocator;
        self.arena.deinit();
        parent_allocator.destroy(self.arena);
    }

    pub fn load(self: *LoadState, input: []const u8) bool {
        self.processed_bytes += input.len;
        if (input.len == 0) {
            self.parser.endInput();
        } else {
            self.parser.feedInput(input);
        }

        while (!self.parser.done()) {
            const event = self.parser.next() catch |err| {
                self.send_load_error("Failed to parse file: {}\n{}", .{
                    err,
                    self.parser.diagnostic,
                });
                return false;
            };

            switch (event) {
                .trace_event => |trace_event| {
                    self.profile.handle_trace_event(trace_event) catch |err| {
                        self.send_load_error("Failed to handle trace event: {}\n{}", .{
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
                self.send_load_error("Failed to finalize profile: {}", .{
                    err,
                });
                return false;
            };

            self.parser.deinit();

            const seconds = @as(f32, @floatFromInt(js.get_current_timestamp() - self.start_timestamp)) / 1000.0;
            const processed_mb = @as(f32, @floatFromInt(self.processed_bytes)) / 1000.0 / 1000.0;
            const speed = processed_mb / seconds;
            std.log.info("Loaded {d:.2}MB in {d:.2} seconds. {d:.2} MB/s", .{ processed_mb, seconds, speed });
        }

        return true;
    }

    fn send_load_error(self: *LoadState, comptime fmt: []const u8, args: anytype) void {
        const msg = std.fmt.allocPrint(self.arena.allocator(), fmt, args) catch unreachable;
        defer self.arena.allocator().free(msg);
        self.tracing.on_load_file_error(msg);
    }
};

fn get_memory_usages() usize {
    return global_count_allocator.get_allocated_bytes();
}

const App = struct {
    allocator: Allocator,
    renderer: Renderer,
    tracing: Tracing,

    width: f32,
    height: f32,

    io: *c.ImGuiIO,
    mouse_pos_before_blur: c.ImVec2 = undefined,
    load_state: ?LoadState,

    pub fn init(
        self: *App,
        count_allocator: *CountAllocator,
        width: f32,
        height: f32,
        device_pixel_ratio: f32,
        font_data: ?[]u8,
        font_size: f32,
    ) void {
        const allocator = count_allocator.allocator();
        self.allocator = allocator;
        self.renderer = .{};
        self.load_state = null;
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

        {
            const style = c.igGetStyle();
            c.igStyleColorsLight(style);
            c.ImGuiStyle_ScaleAllSizes(style, scale);

            style.*.ScrollbarRounding = 0.0;
            style.*.ScrollbarSize = 18.0;

            style.*.SeparatorTextBorderSize = 1.0;
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

    pub fn on_load_file_start(self: *App, len: usize) void {
        self.tracing.on_load_file_start();

        if (self.load_state) |*load_state| {
            load_state.deinit();
        }
        self.load_state = LoadState.init(self.allocator, len, &self.tracing);
    }

    pub fn on_load_file_chunk(self: *App, offset: usize, chunk: []const u8) void {
        if (self.load_state.?.load(chunk)) {
            self.tracing.on_load_file_progress(offset, self.load_state.?.total);
        }
    }

    pub fn on_load_file_done(self: *App) void {
        if (self.load_state.?.load(&[0]u8{})) {
            self.tracing.on_load_file_done(self.load_state.?.profile);
        }
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

var app: *App = undefined;
var gpa = std.heap.GeneralPurposeAllocator(.{}){};
var global_count_allocator = CountAllocator.init(gpa.allocator());

export fn init(
    width: f32,
    height: f32,
    device_pixel_ratio: f32,
    font: js.JsObject,
    font_len: usize,
    font_size: f32,
) void {
    var allocator = global_count_allocator.allocator();
    app = allocator.create(App) catch unreachable;

    const font_data: ?[]u8 = blk: {
        if (font.ref != 0) {
            defer js.destory(font);

            const font_data = allocator.alloc(u8, font_len) catch unreachable;
            js.copy_uint8_array(font, font_data.ptr, font_len);
            break :blk font_data;
        } else {
            break :blk null;
        }
    };
    defer if (font_data) |f| allocator.free(f);

    app.init(
        &global_count_allocator,
        width,
        height,
        device_pixel_ratio,
        font_data,
        font_size,
    );

    // HACK: Force ImGui to update the mosue cursor, otherwise it's in uninitialized state.
    app.onMousePos(0, 0);
}

export fn update(dt: f32) void {
    app.update(dt);
}

export fn on_resize(width: f32, height: f32) void {
    app.on_resize(width, height);
}

export fn onMousePos(x: f32, y: f32) void {
    app.onMousePos(x, y);
}

export fn onMouseButton(button: i32, down: bool) bool {
    return app.onMouseButton(jsButtonToImguiButton(button), down);
}

export fn onMouseWheel(dx: f32, dy: f32) void {
    app.onMouseWheel(dx, dy);
}

export fn onKey(key: u32, down: bool) bool {
    return app.onKey(key, down);
}

export fn onFocus(focused: bool) void {
    app.onFocus(focused);
}

export fn shouldLoadFile() bool {
    return app.tracing.should_load_file();
}

export fn onLoadFileStart(len: usize) void {
    app.on_load_file_start(len);
}

export fn onLoadFileChunk(offset: usize, chunk: js.JsObject, len: usize) void {
    defer js.destory(chunk);

    if (app.tracing.should_load_file()) {
        const allocator = app.allocator;
        const buf = allocator.alloc(u8, len) catch unreachable;
        defer allocator.free(buf);

        js.copy_uint8_array(chunk, buf.ptr, len);

        app.on_load_file_chunk(offset, buf);
    }
}

export fn onLoadFileDone() void {
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
        log.err("{s}\n{}", .{ msg, trace });
    } else {
        log.err("{s}", .{msg});
    }
    std.os.abort();
}
