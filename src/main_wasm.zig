const std = @import("std");
const log = std.log;

const Allocator = std.mem.Allocator;

const c = @import("c.zig");

const JsonProfileParser = @import("./json_profile_parser.zig").JsonProfileParser;
const TraceEvent = @import("./json_profile_parser.zig").TraceEvent;

const eql = std.mem.eql;

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
const num_usize = alignment / @sizeOf(usize);

fn imguiAlloc(size: usize, user_data: ?*anyopaque) callconv(.C) *anyopaque {
    _ = user_data;
    const total_size = @sizeOf(usize) * num_usize + size;
    var allocator = app.allocator;
    const buf = allocator.alignedAlloc(u8, alignment, total_size) catch unreachable;
    var ptr: [*]usize = @ptrCast(buf.ptr);
    ptr[0] = total_size;
    return @ptrCast(ptr + num_usize);
}

fn imguiFree(maybe_ptr: ?*anyopaque, user_data: ?*anyopaque) callconv(.C) void {
    _ = user_data;
    if (maybe_ptr) |ptr| {
        var allocator = app.allocator;
        const ptr_after_num_usize: [*]usize = @ptrCast(@alignCast(ptr));
        const base = ptr_after_num_usize - num_usize;
        const total_size = base[0];

        const raw: [*]align(alignment) u8 = @ptrCast(@alignCast(base));
        allocator.free(raw[0..total_size]);
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

const ProfileCounterSeriesValue = struct {
    ts: i64,
    val: f64,
};

const ProfileCounterSeries = struct {
    name: []u8,
    values: std.ArrayList(ProfileCounterSeriesValue),

    pub fn addValue(self: *ProfileCounterSeries, ts: i64, val: f64) !void {
        try self.values.append(.{ .ts = ts, .val = val });
    }
};

const ProfileCounterLane = struct {
    name: []u8,
    series: std.ArrayList(ProfileCounterSeries),

    fn init(allocator: Allocator, name: []const u8) ProfileCounterLane {
        return .{
            .name = allocator.dupe(u8, name) catch unreachable,
            .series = std.ArrayList(ProfileCounterSeries).init(allocator),
        };
    }

    fn getOrCreateSeries(self: *ProfileCounterLane, name: []const u8) !*ProfileCounterSeries {
        for (self.series.items) |*series| {
            if (eql(u8, series.name, name)) {
                return series;
            }
        }

        try self.series.append(.{
            .name = try self.series.allocator.dupe(u8, name),
            .values = std.ArrayList(ProfileCounterSeriesValue).init(self.series.allocator),
        });

        return &self.series.items[self.series.items.len - 1];
    }

    pub fn addSeriesValue(self: *ProfileCounterLane, name: []const u8, ts: i64, val: f64) !void {
        var series = try self.getOrCreateSeries(name);
        try series.addValue(ts, val);
    }
};

const ProfileLane = union(enum) {
    counter: ProfileCounterLane,
    trace,
};

const Profile = struct {
    lanes: std.ArrayList(ProfileLane),

    pub fn init(allocator: Allocator) Profile {
        return .{
            .lanes = std.ArrayList(ProfileLane).init(allocator),
        };
    }

    pub fn getOrCreateCounterLane(self: *Profile, name: []const u8) !*ProfileCounterLane {
        for (self.lanes.items) |*lane| {
            switch (lane.*) {
                .counter => |*counter_lane| {
                    if (eql(u8, counter_lane.name, name)) {
                        return counter_lane;
                    }
                },
                else => {},
            }
        }

        try self.lanes.append(.{ .counter = ProfileCounterLane.init(self.lanes.allocator, name) });
        return &self.lanes.items[self.lanes.items.len - 1].counter;
    }
};

const LoadFileState = struct {
    allocator: Allocator,
    done: bool,
    total: usize,
    offset: usize,
    json_parser: JsonProfileParser,
    buffer: std.ArrayList(u8),
    progress_message: ?[:0]u8,
    error_message: ?[:0]u8,
    profile: Profile,

    const popup_id = "LoadFilePopup";

    pub fn init(allocator: Allocator, len: usize) LoadFileState {
        return .{
            .allocator = allocator,
            .done = false,
            .total = len,
            .offset = 0,
            .json_parser = JsonProfileParser.init(allocator),
            .buffer = std.ArrayList(u8).init(allocator),
            .progress_message = null,
            .error_message = null,
            .profile = Profile.init(allocator),
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
                    const offset: f32 = @floatFromInt(self.offset);
                    const total: f32 = @floatFromInt(self.total);
                    const percentage: i32 = @intFromFloat(@round(offset / total * 100.0));
                    self.setProgress("Loading file ... ({}%)", .{percentage});
                } else {
                    self.setProgress("Loading file ... ({})", .{self.offset});
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
        return self.error_message == null and !self.done;
    }

    pub fn onLoadFileChunk(self: *LoadFileState, offset: usize, chunk: js.JsObject, len: usize) void {
        if (self.shouldLoadFile()) {
            var buf = self.allocator.alloc(u8, len) catch unreachable;
            defer self.allocator.free(buf);

            js.copyChunk(chunk, buf.ptr, len);

            self.json_parser.feedInput(buf);
            self.continueScan();

            if (self.total > 0) {
                self.offset = offset;
            } else {
                self.offset += len;
            }
        }
    }

    pub fn onLoadFileDone(self: *LoadFileState) void {
        if (self.shouldLoadFile()) {
            self.json_parser.endInput();
            self.continueScan();
            self.json_parser.deinit();

            self.done = true;
            if (self.total > 0) {
                self.offset = self.total;
            }

            switch_state(.{ .view = ViewState.init(self.allocator, self.profile) });
        }
    }

    fn continueScan(self: *LoadFileState) void {
        while (!self.json_parser.done()) {
            const event = self.json_parser.next() catch |err| {
                self.setError("Failed to parse file: {}", .{err});
                return;
            };

            switch (event) {
                .trace_event => |trace_event| {
                    self.handleTraceEvent(trace_event) catch |err| {
                        self.setError("Failed to handle trace event: {}", .{err});
                        return;
                    };
                },
                .none => return,
            }
        }
    }

    fn handleTraceEvent(self: *LoadFileState, trace_event: *TraceEvent) !void {
        switch (trace_event.ph) {
            'C' => {
                // TODO: handle trace_event.id
                const name = trace_event.name.?;

                var counter_lane = try self.profile.getOrCreateCounterLane(name);

                if (trace_event.args) |args| {
                    switch (args) {
                        .object => |obj| {
                            var iter = obj.iterator();
                            while (iter.next()) |entry| {
                                switch (entry.value_ptr.*) {
                                    .number_string => |num| {
                                        const val = try std.fmt.parseFloat(f64, num);
                                        try counter_lane.addSeriesValue(entry.key_ptr.*, trace_event.ts.?, val);
                                    },
                                    .float => |val| {
                                        try counter_lane.addSeriesValue(entry.key_ptr.*, trace_event.ts.?, val);
                                    },
                                    .integer => |val| {
                                        try counter_lane.addSeriesValue(entry.key_ptr.*, trace_event.ts.?, @floatFromInt(val));
                                    },
                                    else => {},
                                }
                            }
                        },
                        else => {},
                    }
                }
            },
            'M' => {
                // metadata event
                // if (trace_event.name) |name| {
                //     if (eql(u8, name, "thread_name")) {
                //     }
                // }
            },
            else => {},
        }
    }
};

const ViewState = struct {
    allocator: Allocator,
    profile: Profile,

    pub fn init(allocator: Allocator, profile: Profile) ViewState {
        return .{
            .allocator = allocator,
            .profile = profile,
        };
    }

    pub fn update(self: *ViewState, dt: f32) void {
        _ = dt;
        var buf: [1024]u8 = .{};

        for (self.profile.lanes.items) |lane| {
            switch (lane) {
                .counter => |counter_lane| {
                    const text = std.fmt.bufPrintZ(&buf, "{s}", .{counter_lane.name}) catch unreachable;
                    c.igSeparatorText(@as([*c]u8, @ptrCast(text)));
                },
                else => {},
            }
        }
    }

    pub fn onLoadFileStart(self: *ViewState, len: usize) void {
        const state = .{ .load_file = LoadFileState.init(self.allocator, len) };
        switch_state(state);
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
    show_imgui_demo_window: bool,

    io: *c.ImGuiIO,

    pub fn init(self: *App, allocator: Allocator, width: f32, height: f32) void {
        self.allocator = allocator;
        self.state = .{ .welcome = WelcomeState.init(allocator) };

        self.width = width;
        self.height = height;
        self.show_imgui_demo_window = false;

        c.igSetAllocatorFunctions(imguiAlloc, imguiFree, null);

        _ = c.igCreateContext(null);

        const io = c.igGetIO();
        self.io = io;

        {
            var style = c.igGetStyle();
            c.igStyleColorsLight(style);
        }

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
            const addr: usize = @intCast(tex.ref);
            c.ImFontAtlas_SetTexID(io.*.Fonts, @ptrFromInt(addr));
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
                    if (c.igMenuItem_Bool("Show ImGui Demo Window", null, self.show_imgui_demo_window, true)) {
                        self.show_imgui_demo_window = !self.show_imgui_demo_window;
                    }
                    c.igEndMenu();
                }

                var buf: [32]u8 = .{};
                {
                    c.igSetCursorPosX(c.igGetCursorPosX() + 10.0);
                    const allocated_bytes: f64 = @floatFromInt(global_counted_allocator.allocated_bytes);
                    const allocated_bytes_mb = allocated_bytes / 1024.0 / 1024.0;
                    const text = std.fmt.bufPrintZ(&buf, "Memory: {d:.1} MB", .{allocated_bytes_mb}) catch unreachable;
                    c.igTextUnformatted(text.ptr, null);
                }

                if (self.io.Framerate < 1000) {
                    const window_width = c.igGetWindowWidth();
                    const text = std.fmt.bufPrintZ(&buf, "{d:.0} ", .{self.io.Framerate}) catch unreachable;
                    var text_size: c.ImVec2 = undefined;
                    c.igCalcTextSize(&text_size, text.ptr, null, false, -1.0);
                    c.igSetCursorPosX(window_width - text_size.x);
                    c.igTextUnformatted(text.ptr, null);
                }

                c.igEndMainMenuBar();
            }
            c.igPopStyleVar(1);
        }

        // Main Window
        {
            const window_flags =
                c.ImGuiWindowFlags_NoDocking |
                c.ImGuiWindowFlags_NoTitleBar |
                c.ImGuiWindowFlags_NoCollapse |
                c.ImGuiWindowFlags_NoResize |
                c.ImGuiWindowFlags_NoMove |
                c.ImGuiWindowFlags_NoBringToFrontOnFocus |
                c.ImGuiWindowFlags_NoNavFocus;
            const viewport = c.igGetMainViewport();
            c.igSetNextWindowPos(viewport.*.WorkPos, 0, .{ .x = 0, .y = 0 });
            c.igSetNextWindowSize(viewport.*.WorkSize, 0);
            _ = c.igBegin("MainWindow", null, window_flags);
            self.state.update(dt);

            c.igEnd();
        }

        if (self.show_imgui_demo_window) {
            c.igShowDemoWindow(&self.show_imgui_demo_window);
        }

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

        for (0..@intCast(draw_data.CmdListsCount)) |cmd_list_index| {
            const cmd_list = draw_data.*.CmdLists[cmd_list_index];

            const vtx_buffer_size = cmd_list.*.VtxBuffer.Size * @sizeOf(c.ImDrawVert);
            const idx_buffer_size = cmd_list.*.IdxBuffer.Size * @sizeOf(c.ImDrawIdx);

            glBufferData(GL_ARRAY_BUFFER, vtx_buffer_size, @ptrCast(cmd_list.*.VtxBuffer.Data), GL_STREAM_DRAW);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx_buffer_size, @ptrCast(cmd_list.*.IdxBuffer.Data), GL_STREAM_DRAW);

            for (0..@intCast(cmd_list.*.CmdBuffer.Size)) |cmd_index| {
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
                    @intFromFloat(clip_min_x),
                    @intFromFloat(fb_height - clip_max_y),
                    @intFromFloat(clip_max_x - clip_min_x),
                    @intFromFloat(clip_max_y - clip_min_y),
                );

                const tex_ref = js.JsObject{
                    .ref = @intFromPtr(c.ImDrawCmd_GetTexID(cmd)),
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
var global_counted_allocator = CountedAllocator.init(gpa.allocator());

const CountedAllocator = struct {
    underlying: Allocator,
    allocated_bytes: usize,

    fn init(underlying: Allocator) CountedAllocator {
        return .{
            .underlying = underlying,
            .allocated_bytes = 0,
        };
    }

    fn allocator(self: *CountedAllocator) Allocator {
        return .{
            .ptr = self,
            .vtable = &.{
                .alloc = alloc,
                .resize = resize,
                .free = free,
            },
        };
    }

    fn alloc(ctx: *anyopaque, len: usize, log2_ptr_align: u8, ret_addr: usize) ?[*]u8 {
        const self: *CountedAllocator = @ptrCast(@alignCast(ctx));
        self.allocated_bytes += len;
        return self.underlying.rawAlloc(len, log2_ptr_align, ret_addr);
    }

    fn resize(ctx: *anyopaque, buf: []u8, buf_align: u8, new_len: usize, ret_addr: usize) bool {
        const self: *CountedAllocator = @ptrCast(@alignCast(ctx));
        self.allocated_bytes -= buf.len;
        self.allocated_bytes += new_len;
        return self.underlying.rawResize(buf, buf_align, new_len, ret_addr);
    }

    fn free(ctx: *anyopaque, buf: []u8, buf_align: u8, ret_addr: usize) void {
        const self: *CountedAllocator = @ptrCast(@alignCast(ctx));
        self.allocated_bytes -= buf.len;
        self.underlying.rawFree(buf, buf_align, ret_addr);
    }
};

export fn init(width: f32, height: f32) void {
    var allocator = global_counted_allocator.allocator();
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
        .view => {
            return true;
        },
    }
}

export fn onLoadFileStart(len: usize) void {
    switch (app.state) {
        .welcome => |*welcome| {
            welcome.onLoadFileStart(len);
        },
        .view => |*view| {
            view.onLoadFileStart(len);
        },
        else => {
            log.err("Unexpected event onLoadFileStart, current state is {s}", .{@tagName(app.state)});
        },
    }
}

export fn onLoadFileChunk(offset: usize, chunk: js.JsObject, len: usize) void {
    defer js.destory(chunk);

    switch (app.state) {
        .load_file => |*load_file| {
            load_file.onLoadFileChunk(offset, chunk, len);
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
    if (error_return_trace) |trace| {
        log.err("{s}\n{}", .{ msg, trace });
    } else {
        log.err("{s}", .{msg});
    }
    std.os.abort();
}
