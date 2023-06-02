const std = @import("std");
const log = std.log;

const Allocator = std.mem.Allocator;

const c = @import("c.zig");

pub const std_options = struct {
    pub fn logFn(
        comptime message_level: std.log.Level,
        comptime scope: @Type(.EnumLiteral),
        comptime format: []const u8,
        args: anytype,
    ) void {
        const level = @enumToInt(message_level);
        const prefix2 = if (scope == .default) "" else "[" ++ @tagName(scope) ++ "] ";
        var buf: [256]u8 = undefined;
        const msg = std.fmt.bufPrint(&buf, prefix2 ++ format ++ "\n", args) catch return;
        js.log(level, msg.ptr, msg.len);
    }
};

const GL_ARRAY_BUFFER: i32 = 0x8892;
const GL_ELEMENT_ARRAY_BUFFER: i32 = 0x8893;
const GL_STREAM_DRAW: i32 = 0x88E0;
const GL_TEXTURE_2D: i32 = 0x0DE1;
const GL_TRIANGLES: i32 = 0x0004;
const GL_UNSIGNED_SHORT: i32 = 0x1403;
const GL_UNSIGNED_INT: i32 = 0x1405;

pub extern "js" fn glBufferData(target: i32, size: i32, data: [*]const u8, usage: i32) void;
pub extern "js" fn glScissor(x: i32, y: i32, w: i32, h: i32) void;
pub extern "js" fn glBindTexture(target: i32, texture_ref: js.JsObject) void;
pub extern "js" fn glDrawElements(mode: i32, count: u32, ty: i32, indices: u32) void;
pub extern "js" fn glCreateFontTexture(w: i32, h: i32, pixels: [*]const u8) js.JsObject;

const js = struct {
    pub const JsObject = extern struct {
        ref: i64,
    };

    pub extern "js" fn log(level: u32, ptr: [*]const u8, len: usize) void;

    pub extern "js" fn destory(obj: JsObject) void;

    pub extern "js" fn showOpenFilePicker() void;

    pub extern "js" fn copyChunk(chunk: JsObject, ptr: [*]const u8, len: usize) void;
};

const alignment = 8;
const numUsize = alignment / @sizeOf(usize);

fn imguiAlloc(size: usize, user_data: ?*anyopaque) callconv(.C) *anyopaque {
    _ = user_data;
    const totalSize = @sizeOf(usize) * numUsize + size;
    var allocator = app.allocator;
    const buf = allocator.alignedAlloc(u8, alignment, totalSize) catch unreachable;
    var ptr = @ptrCast([*]usize, @alignCast(@alignOf([*]usize), buf.ptr));
    ptr[0] = totalSize;
    return @ptrCast(*anyopaque, ptr + numUsize);
}

fn imguiFree(maybe_ptr: ?*anyopaque, user_data: ?*anyopaque) callconv(.C) void {
    _ = user_data;
    if (maybe_ptr) |ptr| {
        var allocator = app.allocator;
        const raw = @ptrCast([*]usize, @alignCast(@alignOf([*]usize), ptr)) - numUsize;
        const totalSize = raw[0];
        allocator.free(@alignCast(alignment, @ptrCast([*]u8, raw)[0..totalSize]));
    }
}

fn jsButtonToImguiButton(button: i32) i32 {
    return switch (button) {
        1 => 2,
        2 => 1,
        3 => 4,
        4 => 3,
        else => button,
    };
}

const WelcomeState = struct {
    allocator: Allocator,
    show_demo_window: bool,

    pub fn init(allocator: Allocator) WelcomeState {
        return .{
            .allocator = allocator,
            .show_demo_window = false,
        };
    }

    pub fn update(self: *WelcomeState, dt: f32) void {
        _ = self;
        _ = dt;
    }

    pub fn onLoadFileStart(self: *WelcomeState, len: usize) void {
        const state = .{ .load_file = LoadFileState.init(self.allocator, len) };
        switch_state(state);
    }
};

fn switch_state(new_state: State) void {
    app.state.deinit();
    app.state = new_state;
}

const LoadFileState = struct {
    allocator: Allocator,
    total: usize,
    received: usize,
    json_scanner: std.json.Scanner,
    progress_message: ?[:0]u8,
    error_message: ?[:0]u8,

    const popup_id = "LoadFilePopup";

    pub fn init(allocator: Allocator, len: usize) LoadFileState {
        return .{
            .allocator = allocator,
            .total = len,
            .received = 0,
            .json_scanner = std.json.Scanner.initStreaming(allocator),
            .progress_message = null,
            .error_message = null,
        };
    }

    pub fn update(self: *LoadFileState, dt: f32) void {
        _ = dt;

        var center: c.ImVec2 = undefined;
        c.ImGuiViewport_GetCenter(&center, c.igGetMainViewport());
        c.igSetNextWindowPos(center, c.ImGuiCond_Appearing, .{ .x = 0.5, .y = 0.5 });

        if (c.igBeginPopupModal(popup_id, null, c.ImGuiWindowFlags_AlwaysAutoResize | c.ImGuiWindowFlags_NoTitleBar | c.ImGuiWindowFlags_NoMove)) {
            if (self.error_message) |err| {
                c.igTextUnformatted(err.ptr, null);

                if (c.igButton("OK", .{ .x = 120, .y = 0 })) {
                    c.igCloseCurrentPopup();
                    switch_state(.{ .welcome = WelcomeState.init(self.allocator) });
                }
            } else {
                if (self.total > 0) {
                    self.setProgress("Loading file ... ({}%)", .{@floatToInt(usize, @round(@intToFloat(f32, self.received) / @intToFloat(f32, self.total) * 100.0))});
                } else {
                    self.setProgress("Loading file ... ({})", .{self.received});
                }

                c.igTextUnformatted(self.progress_message.?.ptr, null);
            }

            c.igEndPopup();
        }

        if (!c.igIsPopupOpen_Str(popup_id, 0)) {
            c.igOpenPopup_Str(popup_id, 0);
        }
    }

    pub fn deinit(self: *LoadFileState) void {
        if (self.progress_message) |msg| {
            self.allocator.free(msg);
        }
        if (self.error_message) |msg| {
            self.allocator.free(msg);
        }
    }

    fn setProgress(self: *LoadFileState, comptime fmt: []const u8, args: anytype) void {
        if (self.progress_message) |msg| {
            self.allocator.free(msg);
        }
        self.progress_message = std.fmt.allocPrintZ(self.allocator, fmt, args) catch unreachable;
    }

    fn setError(self: *LoadFileState, comptime fmt: []const u8, args: anytype) void {
        assert(self.error_message == null, "Error has already been set");
        self.error_message = std.fmt.allocPrintZ(self.allocator, fmt, args) catch unreachable;
    }

    pub fn shouldLoadFile(self: *const LoadFileState) bool {
        return self.error_message == null;
    }

    pub fn onLoadFileChunk(self: *LoadFileState, chunk: js.JsObject, len: usize) void {
        if (self.shouldLoadFile()) {
            var buf = self.allocator.alloc(u8, len) catch unreachable;
            defer self.allocator.free(buf);

            js.copyChunk(chunk, buf.ptr, len);

            self.json_scanner.feedInput(buf);
            self.continueScan();

            self.received += len;
        }
    }

    pub fn onLoadFileDone(self: *LoadFileState) void {
        if (self.shouldLoadFile()) {
            self.json_scanner.endInput();
            self.continueScan();
        }
    }

    fn continueScan(self: *LoadFileState) void {
        while (true) {
            const token = self.json_scanner.next() catch |err| switch (err) {
                error.BufferUnderrun => {
                    if (self.json_scanner.is_end_of_input) {
                        unreachable;
                    }
                    break;
                },
                else => {
                    self.setError("Failed to parse file: {s}", .{@errorName(err)});
                    return;
                },
            };
            // log.debug("{s}", .{@tagName(token)});
            switch (token) {
                .end_of_document => {
                    if (self.json_scanner.is_end_of_input) {
                        break;
                    }
                    self.setError("Unexpected end of document", .{});
                },
                else => {},
            }
        }
    }
};

const ViewState = struct {
    io: *c.ImGuiIO,

    pub fn update(self: *ViewState, dt: f32) void {
        _ = self;
        _ = dt;
    }
};

const State = union(enum) {
    welcome: WelcomeState,
    load_file: LoadFileState,
    view: ViewState,

    pub fn update(self: *State, dt: f32) void {
        switch (self.*) {
            inline else => |*s| s.update(dt),
        }
    }

    pub fn deinit(self: *State) void {
        switch (self.*) {
            inline else => |*s| {
                if (@hasDecl(@TypeOf(s.*), "deinit")) {
                    s.deinit();
                }
            },
        }
    }
};

const App = struct {
    allocator: Allocator,
    state: State,

    width: f32,
    height: f32,
    show_demo_window: bool,

    io: *c.ImGuiIO,

    pub fn init(self: *App, allocator: Allocator, width: f32, height: f32) void {
        self.allocator = allocator;
        self.state = .{ .welcome = WelcomeState.init(allocator) };

        self.width = width;
        self.height = height;
        self.show_demo_window = false;

        c.igSetAllocatorFunctions(imguiAlloc, imguiFree, null);

        _ = c.igCreateContext(null);

        const io = c.igGetIO();
        self.io = io;

        io.*.ConfigFlags |= c.ImGuiConfigFlags_DockingEnable;

        io.*.DisplaySize.x = width;
        io.*.DisplaySize.y = height;

        {
            var pixels: [*c]u8 = undefined;
            var w: i32 = undefined;
            var h: i32 = undefined;
            var bytes_per_pixel: i32 = undefined;
            c.ImFontAtlas_GetTexDataAsRGBA32(io.*.Fonts, &pixels, &w, &h, &bytes_per_pixel);
            std.debug.assert(bytes_per_pixel == 4);
            const tex = glCreateFontTexture(w, h, pixels);
            c.ImFontAtlas_SetTexID(io.*.Fonts, @intToPtr(*anyopaque, @intCast(usize, tex.ref)));
        }
    }

    pub fn update(self: *App, dt: f32) void {
        self.io.*.DeltaTime = dt;

        c.igNewFrame();

        // Main Menu Bar
        {
            c.igPushStyleVar_Vec2(c.ImGuiStyleVar_FramePadding, .{ .x = 10, .y = 4 });
            if (c.igBeginMainMenuBar()) {
                c.igSetCursorPosX(0);
                if (c.igButton("Load", .{ .x = 0, .y = 0 })) {
                    js.showOpenFilePicker();
                }

                if (c.igBeginMenu("Help", true)) {
                    if (c.igMenuItem_Bool("Show Demo Window", null, self.show_demo_window, true)) {
                        self.show_demo_window = !self.show_demo_window;
                    }
                    c.igEndMenu();
                }

                if (self.io.Framerate < 1000) {
                    const window_width = c.igGetWindowWidth();
                    var buf: [32]u8 = .{};
                    const text = std.fmt.bufPrintZ(&buf, "{d:.1} ", .{self.io.Framerate}) catch unreachable;
                    var text_size: c.ImVec2 = undefined;
                    c.igCalcTextSize(&text_size, text.ptr, null, false, -1.0);
                    c.igSetCursorPosX(window_width - text_size.x);
                    c.igTextUnformatted(text.ptr, null);
                }

                c.igEndMainMenuBar();
            }
            c.igPopStyleVar(1);
        }

        _ = c.igDockSpaceOverViewport(c.igGetMainViewport(), c.ImGuiDockNodeFlags_PassthruCentralNode, null);

        if (self.show_demo_window) {
            c.igShowDemoWindow(&self.show_demo_window);
        }

        self.state.update(dt);

        c.igEndFrame();
        c.igRender();
        const draw_data = c.igGetDrawData();
        self.renderImgui(draw_data);
    }

    pub fn onResize(self: *App, width: f32, height: f32) void {
        self.width = width;
        self.height = height;

        self.io.*.DisplaySize.x = width;
        self.io.*.DisplaySize.y = height;
    }

    pub fn onMouseMove(self: *App, x: f32, y: f32) void {
        c.ImGuiIO_AddMousePosEvent(self.io, x, y);
    }

    pub fn onMouseDown(self: *App, button: i32) void {
        c.ImGuiIO_AddMouseButtonEvent(self.io, button, true);
    }

    pub fn onMouseUp(self: *App, button: i32) void {
        c.ImGuiIO_AddMouseButtonEvent(self.io, button, false);
    }

    pub fn onWheel(self: *App, dx: f32, dy: f32) void {
        c.ImGuiIO_AddMouseWheelEvent(self.io, dx / self.width * 10.0, -dy / self.height * 10.0);
    }

    fn renderImgui(self: *App, draw_data: *c.ImDrawData) void {
        _ = self;
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

        for (0..@intCast(usize, draw_data.CmdListsCount)) |cmd_list_index| {
            const cmd_list = draw_data.*.CmdLists[cmd_list_index];

            const vtx_buffer_size = cmd_list.*.VtxBuffer.Size * @sizeOf(c.ImDrawVert);
            const idx_buffer_size = cmd_list.*.IdxBuffer.Size * @sizeOf(c.ImDrawIdx);

            glBufferData(GL_ARRAY_BUFFER, vtx_buffer_size, @ptrCast([*]const u8, cmd_list.*.VtxBuffer.Data), GL_STREAM_DRAW);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx_buffer_size, @ptrCast([*]const u8, cmd_list.*.IdxBuffer.Data), GL_STREAM_DRAW);

            for (0..@intCast(usize, cmd_list.*.CmdBuffer.Size)) |cmd_index| {
                const cmd = &cmd_list.*.CmdBuffer.Data[cmd_index];

                // Project scissor/clipping rectangles into framebuffer space
                const clip_min_x = (cmd.*.ClipRect.x - clip_off_x) * clip_scale_x;
                const clip_min_y = (cmd.*.ClipRect.y - clip_off_y) * clip_scale_y;
                const clip_max_x = (cmd.*.ClipRect.z - clip_off_x) * clip_scale_x;
                const clip_max_y = (cmd.*.ClipRect.w - clip_off_y) * clip_scale_y;
                if (clip_max_x <= clip_min_x or clip_max_y <= clip_min_y) {
                    continue;
                }
                // Apply scissor/clipping rectangle (Y is inverted in OpenGL)
                glScissor(
                    @floatToInt(i32, clip_min_x),
                    @floatToInt(i32, fb_height - clip_max_y),
                    @floatToInt(i32, clip_max_x - clip_min_x),
                    @floatToInt(i32, clip_max_y - clip_min_y),
                );

                const tex_ref = js.JsObject{
                    .ref = @ptrToInt(c.ImDrawCmd_GetTexID(cmd)),
                };
                glBindTexture(GL_TEXTURE_2D, tex_ref);

                assert(@sizeOf(c.ImDrawIdx) == 4, "expect size of ImDrawIdx to be 4.");
                glDrawElements(GL_TRIANGLES, cmd.*.ElemCount, GL_UNSIGNED_INT, cmd.*.IdxOffset * @sizeOf(c.ImDrawIdx));
            }
        }
    }
};

fn assert(ok: bool, msg: []const u8) void {
    if (!ok) {
        log.err("{s}", .{msg});
        std.os.abort();
    }
}

var app: *App = undefined;
var gpa = std.heap.GeneralPurposeAllocator(.{}){};

export fn init(width: f32, height: f32) void {
    var allocator = gpa.allocator();
    app = allocator.create(App) catch unreachable;
    app.init(allocator, width, height);
}

export fn update(dt: f32) void {
    app.update(dt);
}

export fn onResize(width: f32, height: f32) void {
    app.onResize(width, height);
}

export fn onMouseMove(x: f32, y: f32) void {
    app.onMouseMove(x, y);
}

export fn onMouseDown(button: i32) void {
    app.onMouseDown(jsButtonToImguiButton(button));
}

export fn onMouseUp(button: i32) void {
    app.onMouseUp(jsButtonToImguiButton(button));
}

export fn onWheel(dx: f32, dy: f32) void {
    app.onWheel(dx, dy);
}

export fn shouldLoadFile() bool {
    switch (app.state) {
        .welcome => {
            return true;
        },
        .load_file => |*load_file| {
            return load_file.shouldLoadFile();
        },
        else => {
            return false;
        },
    }
}

export fn onLoadFileStart(len: usize) void {
    switch (app.state) {
        .welcome => |*welcome| {
            welcome.onLoadFileStart(len);
        },
        else => {
            log.err("Unexpected event onLoadFileStart, current state is {s}", .{@tagName(app.state)});
        },
    }
}

export fn onLoadFileChunk(chunk: js.JsObject, len: usize) void {
    defer js.destory(chunk);

    switch (app.state) {
        .load_file => |*load_file| {
            load_file.onLoadFileChunk(chunk, len);
        },
        else => {
            log.err("Unexpected event onLoadFileChunk, current state is {s}", .{@tagName(app.state)});
        },
    }
}

export fn onLoadFileDone() void {
    switch (app.state) {
        .load_file => |*load_file| {
            load_file.onLoadFileDone();
        },
        else => {
            log.err("Unexpected event onLoadFileDone, current state is {s}", .{@tagName(app.state)});
        },
    }
}

export fn main(argc: i32, argv: i32) i32 {
    _ = argc;
    _ = argv;
    unreachable;
}

export fn logFromC(level: i32, cstr: [*:0]const u8) void {
    switch (level) {
        0 => std.log.err("{s}", .{cstr}),
        1 => std.log.warn("{s}", .{cstr}),
        2 => std.log.info("{s}", .{cstr}),
        3 => std.log.debug("{s}", .{cstr}),
        else => unreachable,
    }
}

pub fn panic(msg: []const u8, error_return_trace: ?*std.builtin.StackTrace, ret_addr: ?usize) noreturn {
    _ = ret_addr;
    _ = error_return_trace;
    log.err("{s}", .{msg});
    std.os.abort();
}
