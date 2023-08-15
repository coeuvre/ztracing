const std = @import("std");
const c = @import("c.zig");
const ig = @import("imgui.zig");
const CountAllocator = @import("./count_alloc.zig").CountAllocator;
const json_profile_parser = @import("./json_profile_parser.zig");
const easing = @import("./easing.zig");
const _profile = @import("profile.zig");

const log = std.log;
const Allocator = std.mem.Allocator;

const JsonProfileParser = json_profile_parser.JsonProfileParser;
const TraceEvent = json_profile_parser.TraceEvent;

const Profile = _profile.Profile;
const Span = _profile.Span;
const SeriesValue = _profile.SeriesValue;
const Counter = _profile.Counter;
const Thread = _profile.Thread;

fn normalize(v: f32, min: f32, max: f32) @TypeOf(v, min, max) {
    return (v - min) / (max - min);
}

fn hashString(s: []const u8) u64 {
    return std.hash.Wyhash.hash(0, s);
}

const Color = c.ImVec4;

const Timestamp = struct {
    us: i64,

    pub fn format(self: *const Timestamp, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        _ = options;
        _ = fmt;

        const us = @rem(self.us, 1_000_000);
        const s = @divTrunc(self.us, 1_000_000);

        if (s == 0 and us == 0) {
            try writer.print("0", .{});
        } else {
            if (s != 0) {
                try writer.print("{}s", .{Number{ .num = s }});
                if (us != 0) {
                    try writer.print(" {}us", .{Number{ .num = std.math.absInt(us) catch unreachable }});
                }
            } else {
                try writer.print("{}us", .{Number{ .num = us }});
            }
        }
    }
};

const Number = struct {
    num: i64,

    pub fn format(self: *const Number, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        _ = fmt;
        _ = options;

        if (self.num == 0) {
            try writer.print("0", .{});
            return;
        }

        var base: i64 = 1;
        const abs = std.math.absInt(self.num) catch unreachable;
        while (base <= abs) {
            base *= 1000;
        }
        base = @divTrunc(base, 1000);

        var leading = true;
        var val = self.num;
        while (base != 0) {
            const digits = @divTrunc(val, base);
            val = @rem(val, base);
            base = @divTrunc(base, 1000);

            if (leading) {
                try writer.print("{}", .{digits});
            } else {
                const h = @divTrunc(digits, 100);
                const t = @divTrunc(@rem(digits, 100), 10);
                const o = @rem(digits, 10);
                try writer.print(",{}{}{}", .{ h, t, o });
            }
            leading = false;
        }
    }
};

// https://chromium.googlesource.com/catapult/+/refs/heads/main/tracing/tracing/base/sinebow_color_generator.html
pub const SinebowColorGenerator = struct {
    // [0, 1]
    alpha: f32,
    // [0, 2]
    brightness: f32,
    color_index: u32 = 0,

    pub fn init(alpha: f32, brightness: f32) SinebowColorGenerator {
        return .{
            .alpha = alpha,
            .brightness = brightness,
        };
    }

    pub fn nextColor(self: *SinebowColorGenerator) Color {
        const col = nthColor(self.color_index);
        self.color_index += 1;
        return calculateColor(col[0], col[1], col[2], self.alpha, self.brightness);
    }

    fn calculateColor(r: f32, g: f32, b: f32, a: f32, brightness: f32) Color {
        var color = .{ .x = r, .y = g, .z = b, .w = a };
        if (brightness <= 1) {
            color.x *= brightness;
            color.y *= brightness;
            color.z *= brightness;
        } else {
            color.x = std.math.lerp(normalize(brightness, 1, 2), color.x, 1);
            color.y = std.math.lerp(normalize(brightness, 1, 2), color.y, 1);
            color.z = std.math.lerp(normalize(brightness, 1, 2), color.z, 1);
        }
        return color;
    }

    fn sinebow(h: f32) [3]f32 {
        var hh = h;
        hh += 0.5;
        hh = -hh;
        var r = @sin(std.math.pi * hh);
        var g = @sin(std.math.pi * (hh + 1.0 / 3.0));
        var b = @sin(std.math.pi * (hh + 2.0 / 3.0));
        r *= r;
        g *= g;
        b *= b;
        return [_]f32{ r, g, b };
    }

    fn nthColor(n: u32) [3]f32 {
        return sinebow(@as(f32, @floatFromInt(n)) * std.math.phi);
    }
};

var general_purpose_colors: [7]Color = [_]Color{
    rgb(169, 188, 255),
    rgb(154, 255, 255),
    rgb(24, 255, 177),
    rgb(255, 255, 173),
    rgb(255, 212, 147),
    rgb(255, 159, 140),
    rgb(255, 189, 218),
};

fn rgb(r: u8, g: u8, b: u8) Color {
    return .{
        .x = @as(f32, @floatFromInt(r)) / 255.0,
        .y = @as(f32, @floatFromInt(g)) / 255.0,
        .z = @as(f32, @floatFromInt(b)) / 255.0,
        .w = 1.0,
    };
}

fn getImColorU32(color: Color) u32 {
    return c.igGetColorU32_Vec4(color);
}

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

const container_window_flags =
    c.ImGuiWindowFlags_NoDocking |
    c.ImGuiWindowFlags_NoTitleBar |
    c.ImGuiWindowFlags_NoCollapse |
    c.ImGuiWindowFlags_NoResize |
    c.ImGuiWindowFlags_NoMove |
    c.ImGuiWindowFlags_NoBringToFrontOnFocus |
    c.ImGuiWindowFlags_NoNavFocus;

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
        const viewport = c.igGetMainViewport();
        c.igSetNextWindowPos(viewport.*.WorkPos, 0, .{ .x = 0, .y = 0 });
        c.igSetNextWindowSize(viewport.*.WorkSize, 0);
        _ = c.igBegin("MainWindow", null, container_window_flags);
        c.igEnd();
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
        const viewport = c.igGetMainViewport();
        c.igSetNextWindowPos(viewport.*.WorkPos, 0, .{ .x = 0, .y = 0 });
        c.igSetNextWindowSize(viewport.*.WorkSize, 0);
        _ = c.igBegin("MainWindow", null, container_window_flags);
        if (self.error_message) |err| {
            c.igTextWrapped("%s", err.ptr);

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

        c.igEnd();
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

            self.profile.done() catch |err| {
                self.setError("Failed to finalize profile: {}", .{err});
                return;
            };

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
                self.setError("Failed to parse file: {}\n{}", .{ err, self.json_parser.diagnostic });
                return;
            };

            switch (event) {
                .trace_event => |trace_event| {
                    self.profile.handleTraceEvent(trace_event) catch |err| {
                        self.setError("Failed to handle trace event: {}", .{err});
                        return;
                    };
                },
                .none => return,
            }
        }
    }
};

// Returns current window content region in screen space
fn getWindowContentRegion() c.ImRect {
    const pos = ig.getWindowPos();
    var scroll_x = c.igGetScrollX();
    var scroll_y = c.igGetScrollY();
    var min = ig.getWindowContentRegionMin();
    min.x += pos.x + scroll_x;
    min.y += pos.y + scroll_y;
    var max = ig.getWindowContentRegionMax();
    max.x += pos.x + scroll_x;
    max.y += pos.y + scroll_y;
    return .{ .Min = min, .Max = max };
}

const HoveredCounter = struct {
    name: []const u8,
    value: *const SeriesValue,
    pos: c.ImVec2,
};

const HoveredSpan = struct {
    span: *const Span,
    bb: c.ImRect,
};

const ViewPos = struct {
    time_us: i64,
    scroll_y: f32,
};

const ViewRegion = struct {
    bb: c.ImRect,
    min_duration_us: i64,
    width_per_us: f32,

    pub fn pos(self: *const ViewRegion) c.ImVec2 {
        return self.bb.Min;
    }

    pub fn min(self: *const ViewRegion) c.ImVec2 {
        return self.bb.Min;
    }

    pub fn max(self: *const ViewRegion) c.ImVec2 {
        return self.bb.Max;
    }

    pub fn size(self: *const ViewRegion) c.ImVec2 {
        return c.ImVec2{
            .x = self.bb.Max.x - self.bb.Min.x,
            .y = self.bb.Max.y - self.bb.Min.y,
        };
    }

    pub fn width(self: *const ViewRegion) f32 {
        return self.bb.Max.x - self.bb.Min.x;
    }

    pub fn height(self: *const ViewRegion) f32 {
        return self.bb.Max.y - self.bb.Min.y;
    }

    pub fn left(self: *const ViewRegion) f32 {
        return self.bb.Min.x;
    }

    pub fn top(self: *const ViewRegion) f32 {
        return self.bb.Min.y;
    }

    pub fn right(self: *const ViewRegion) f32 {
        return self.bb.Max.x;
    }

    pub fn bottom(self: *const ViewRegion) f32 {
        return self.bb.Max.y;
    }
};

const ViewStyle = struct {
    sub_lane_height: f32,
    character_size: c.ImVec2,
    text_padding: c.ImVec2,
};

const ViewState = struct {
    allocator: Allocator,
    profile: Profile,
    start_time_us: i64,
    end_time_us: i64,
    hovered_counters: std.ArrayList(HoveredCounter),

    is_dragging: bool = false,
    drag_start: ViewPos = undefined,

    pub fn init(allocator: Allocator, profile: Profile) ViewState {
        const duration_us = profile.max_time_us - profile.min_time_us;
        const padding = @divTrunc(duration_us, 6);
        return .{
            .allocator = allocator,
            .profile = profile,
            .start_time_us = profile.min_time_us - padding,
            .end_time_us = profile.max_time_us + padding,
            .hovered_counters = std.ArrayList(HoveredCounter).init(allocator),
        };
    }

    fn calcRegion(self: *ViewState, bb: c.ImRect) ViewRegion {
        const width_per_us = (bb.Max.x - bb.Min.x) / @as(f32, @floatFromInt((self.end_time_us - self.start_time_us)));
        const min_duration_us: i64 = @intFromFloat(@ceil(1 / width_per_us));
        return .{
            .bb = bb,
            .width_per_us = width_per_us,
            .min_duration_us = min_duration_us,
        };
    }

    fn calcStyle(self: *ViewState) ViewStyle {
        _ = self;
        var character_size: c.ImVec2 = undefined;
        c.igCalcTextSize(&character_size, "A", null, false, 0);
        const text_padding_x: f32 = character_size.x;
        const text_padding_y: f32 = character_size.y / 4.0;
        const sub_lane_height: f32 = 2 * text_padding_y + character_size.y;
        return ViewStyle{
            .sub_lane_height = sub_lane_height,
            .character_size = character_size,
            .text_padding = .{ .x = text_padding_x, .y = text_padding_y },
        };
    }

    pub fn update(self: *ViewState, dt: f32) void {
        _ = dt;
        const style = self.calcStyle();

        const timeline_height: f32 = 21;
        self.drawTimeline(timeline_height, style);
        self.drawMainView(timeline_height, style);
    }

    fn drawMainView(self: *ViewState, timeline_height: f32, style: ViewStyle) void {
        const viewport = c.igGetMainViewport();
        c.igSetNextWindowPos(.{ .x = viewport.*.WorkPos.x, .y = viewport.*.WorkPos.y + timeline_height }, 0, .{ .x = 0, .y = 0 });
        c.igSetNextWindowSize(.{ .x = viewport.*.WorkSize.x, .y = viewport.*.WorkSize.y - timeline_height }, 0);
        c.igPushStyleVar_Vec2(c.ImGuiStyleVar_WindowPadding, .{ .x = 0, .y = 0 });
        _ = c.igBegin("MainWindow", null, container_window_flags | c.ImGuiWindowFlags_NoScrollWithMouse);
        c.igPopStyleVar(1);

        const region = self.calcRegion(getWindowContentRegion());

        self.handleDrag(region);
        self.handleScroll(region);

        const draw_list = c.igGetWindowDrawList();
        c.ImDrawList_PushClipRect(
            draw_list,
            region.min(),
            region.max(),
            true,
        );

        for (self.profile.processes.items) |*process| {
            const name = std.fmt.bufPrintZ(&global_buf, "Process {}", .{process.pid}) catch unreachable;
            if (c.igCollapsingHeader_BoolPtr(name, null, c.ImGuiTreeNodeFlags_DefaultOpen)) {
                self.drawCounters(region, style, process.counters.items);
                self.drawThreads(region, style, process.threads.items);
            }
        }

        c.ImDrawList_PopClipRect(draw_list);

        c.igEnd();
    }

    fn drawTimeline(self: *ViewState, timeline_height: f32, style: ViewStyle) void {
        const viewport = c.igGetMainViewport();
        const timeline_bb = c.ImRect{
            .Min = .{ .x = viewport.*.WorkPos.x, .y = viewport.*.WorkPos.y },
            .Max = .{ .x = viewport.*.WorkPos.x + viewport.*.WorkSize.x, .y = viewport.*.WorkPos.y + timeline_height },
        };
        c.igSetNextWindowPos(timeline_bb.Min, 0, .{ .x = 0, .y = 0 });
        c.igSetNextWindowSize(.{ .x = timeline_bb.Max.x - timeline_bb.Min.x, .y = timeline_bb.Max.y - timeline_bb.Min.y }, 0);

        c.igPushStyleVar_Float(c.ImGuiStyleVar_WindowRounding, 0);
        c.igPushStyleVar_Vec2(c.ImGuiStyleVar_WindowMinSize, .{ .x = 0, .y = 0 });
        _ = c.igBegin("Timeline", null, container_window_flags);
        c.igPopStyleVar(2);

        const draw_list = c.igGetWindowDrawList();
        const region = self.calcRegion(timeline_bb);
        const target_block_width: f32 = 60;
        const target_num_blocks: f32 = @ceil(region.width() / target_block_width);
        var block_duration_us: i64 = @intFromFloat(@ceil(@as(f32, @floatFromInt(self.end_time_us - self.start_time_us)) / target_num_blocks));

        var base: i64 = 1;
        while (base * 10 < block_duration_us) : (base *= 10) {}
        if (block_duration_us >= base * 4) {
            base *= 4;
        } else if (block_duration_us >= base * 2) {
            base *= 2;
        }

        block_duration_us = base;

        const large_block_duration_us = block_duration_us * 5;

        const center_y = region.top() + region.height() * 0.5;

        const text_color = getImColorU32(.{ .x = 0, .y = 0, .z = 0, .w = 1 });
        var time_us = @divTrunc(self.start_time_us, block_duration_us) * block_duration_us;
        while (time_us < self.end_time_us) : (time_us += block_duration_us) {
            const is_large_block = @rem(time_us, large_block_duration_us) == 0;

            const x = region.left() + @as(f32, @floatFromInt(time_us - self.start_time_us)) * region.width_per_us;
            const y1 = region.bottom();
            const y2 = if (is_large_block) y1 - 12 else y1 - 4;
            c.ImDrawList_AddLine(
                draw_list,
                .{ .x = x, .y = y1 },
                .{ .x = x, .y = y2 },
                text_color,
                1,
            );
            if (is_large_block) {
                const text = std.fmt.bufPrintZ(&global_buf, "{}", .{Timestamp{ .us = time_us }}) catch unreachable;
                c.ImDrawList_AddText_Vec2(draw_list, .{ .x = x + style.text_padding.x, .y = center_y - style.character_size.y * 0.5 }, text_color, text, null);
            }
        }

        c.igEnd();
    }

    fn handleDrag(self: *ViewState, region: ViewRegion) void {
        const is_window_hovered = c.igIsWindowHovered(0);
        const is_mouse_dragging = c.igIsMouseDragging(c.ImGuiMouseButton_Left, 1);
        if (self.is_dragging) {
            if (is_mouse_dragging) {
                c.igSetMouseCursor(c.ImGuiMouseCursor_ResizeAll);
                var drag_delta: c.ImVec2 = undefined;
                c.igGetMouseDragDelta(&drag_delta, c.ImGuiMouseButton_Left, 0);

                const delta_us: i64 = @intFromFloat(drag_delta.x * @as(f64, @floatFromInt(self.end_time_us - self.start_time_us)) / region.width());

                const duration_us = self.end_time_us - self.start_time_us;
                self.start_time_us = self.drag_start.time_us - delta_us;
                self.end_time_us = self.start_time_us + duration_us;

                c.igSetScrollY_Float(self.drag_start.scroll_y - drag_delta.y);
            } else {
                self.is_dragging = false;
            }
        } else {
            if (is_window_hovered and is_mouse_dragging) {
                self.is_dragging = true;
                self.drag_start = .{
                    .time_us = self.start_time_us,
                    .scroll_y = c.igGetScrollY(),
                };
            }
        }
    }

    fn handleScroll(self: *ViewState, region: ViewRegion) void {
        const io = c.igGetIO();
        const is_window_hovered = c.igIsWindowHovered(0);

        if (!self.is_dragging) {
            const wheel_y = io.*.MouseWheel;
            const wheel_x = io.*.MouseWheelH;
            if (io.*.KeyCtrl) {
                if (is_window_hovered and wheel_y != 0) {
                    // Zoom
                    const mouse = io.*.MousePos.x - region.left();
                    const p: f64 = mouse / region.width();
                    var duration_us: f64 = @floatFromInt((self.end_time_us - self.start_time_us));
                    const p_us = self.start_time_us + @as(i64, @intFromFloat(@round(p * duration_us)));
                    if (wheel_y > 0) {
                        if (duration_us > 100) {
                            duration_us = duration_us * 0.8;
                        }
                    } else {
                        if (duration_us < @as(f64, @floatFromInt((self.profile.max_time_us - self.profile.min_time_us) * 2))) {
                            duration_us = duration_us * 1.25;
                        }
                    }
                    self.start_time_us = p_us - @as(i64, @intFromFloat(@round(p * duration_us)));
                    self.end_time_us = self.start_time_us + @as(i64, @intFromFloat(duration_us));
                }
            } else {
                if (is_window_hovered and (wheel_y != 0 or wheel_x != 0)) {
                    const duration_us = self.end_time_us - self.start_time_us;
                    const delta_us: f64 = 100 * wheel_x * @as(f64, @floatFromInt(duration_us)) / region.width();
                    self.start_time_us = @intFromFloat(@as(f64, @floatFromInt(self.start_time_us)) + delta_us);
                    self.end_time_us = self.start_time_us + duration_us;

                    c.igSetScrollY_Float(c.igGetScrollY() - 100 * wheel_y);
                }
            }
        }
    }

    fn drawCounters(self: *ViewState, region: ViewRegion, style: ViewStyle, counters: []Counter) void {
        const io = c.igGetIO();
        const mouse_pos = io.*.MousePos;
        const draw_list = c.igGetWindowDrawList();

        const allow_hover = c.igIsWindowHovered(0) and !self.is_dragging;
        for (counters) |*counter| {
            // Header
            {
                const name = std.fmt.bufPrintZ(&global_buf, "{s}", .{counter.name}) catch unreachable;

                const lane_top = region.top() + c.igGetCursorPosY() - c.igGetScrollY();
                const lane_height: f32 = style.sub_lane_height;
                const lane_bottom = lane_top + lane_height;
                const lane_bb = c.ImRect{
                    .Min = .{ .x = region.left(), .y = lane_top },
                    .Max = .{ .x = region.right(), .y = lane_bottom },
                };
                var hovered: bool = false;
                drawLaneHeader(lane_bb, name, style.character_size.y, style.text_padding.x, allow_hover, &counter.ui.open, &hovered);
                if (hovered) {
                    if (io.*.MouseClicked[0]) {
                        counter.ui.open = !counter.ui.open;
                    }
                    if (counter.ui.open) {
                        if (c.igBeginTooltip()) {
                            c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "{s}", .{counter.name}) catch unreachable, null);
                        }
                        c.igEndTooltip();
                    }
                }
            }

            if (counter.ui.open) {
                const lane_top = region.top() + c.igGetCursorPosY() - c.igGetScrollY();
                const lane_height: f32 = style.sub_lane_height;
                const lane_bottom = lane_top + lane_height;
                const lane_bb = c.ImRect{
                    .Min = .{ .x = region.left(), .y = lane_top },
                    .Max = .{ .x = region.right(), .y = lane_bottom },
                };
                c.igItemSize_Rect(lane_bb, -1);
                if (c.igItemAdd(lane_bb, 0, null, 0)) {
                    const color_index_base: usize = @truncate(hashString(counter.name));

                    for (counter.series.items, 0..) |series, series_index| {
                        const col_v4 = general_purpose_colors[(color_index_base + series_index) % general_purpose_colors.len];
                        const col = getImColorU32(col_v4);

                        var iter = series.iter(self.start_time_us, region.min_duration_us);
                        var prev_pos: ?c.ImVec2 = null;
                        var prev_value: ?*const SeriesValue = null;
                        var hovered_counter: ?HoveredCounter = null;
                        while (iter.next()) |value| {
                            var pos = c.ImVec2{
                                .x = region.left() + @as(f32, @floatFromInt(value.time_us - self.start_time_us)) * region.width_per_us,
                                .y = lane_bottom - @as(f32, @floatCast((value.value / counter.max_value))) * lane_height,
                            };

                            if (prev_pos) |pp| {
                                var bb = c.ImRect{
                                    .Min = .{ .x = pp.x, .y = lane_top },
                                    .Max = .{ .x = pos.x, .y = lane_bottom },
                                };
                                if (allow_hover and c.ImRect_Contains_Vec2(&bb, mouse_pos)) {
                                    hovered_counter = .{
                                        .name = series.name,
                                        .value = prev_value.?,
                                        .pos = pp,
                                    };
                                }

                                c.ImDrawList_AddQuadFilled(
                                    draw_list,
                                    .{ .x = pp.x, .y = lane_bottom },
                                    .{ .x = pos.x, .y = lane_bottom },
                                    pos,
                                    pp,
                                    col,
                                );

                                c.ImDrawList_AddLine(
                                    draw_list,
                                    .{ .x = pp.x - 1, .y = pp.y - 1 },
                                    .{ .x = pos.x, .y = pos.y - 1 },
                                    getImColorU32(.{ .x = col_v4.x * 0.5, .y = col_v4.y * 0.5, .z = col_v4.z * 0.5, .w = 1.0 }),
                                    1,
                                );
                            }

                            if (value.time_us > self.end_time_us) {
                                break;
                            }

                            prev_pos = pos;
                            prev_value = value;
                        }

                        if (hovered_counter == null) {
                            // Handle hover for last point
                            if (prev_pos) |pp| {
                                var bb = c.ImRect{
                                    .Min = .{ .x = pp.x, .y = lane_top },
                                    .Max = .{ .x = region.right(), .y = lane_bottom },
                                };
                                if (allow_hover and c.ImRect_Contains_Vec2(&bb, mouse_pos)) {
                                    hovered_counter = .{
                                        .name = series.name,
                                        .value = prev_value.?,
                                        .pos = pp,
                                    };
                                }
                            }
                        }

                        if (hovered_counter) |hc| {
                            self.hovered_counters.append(hc) catch unreachable;
                        }
                    }
                }
            }
        }

        var max_hovered_time: i64 = 0;
        for (self.hovered_counters.items) |hovered| {
            max_hovered_time = @max(max_hovered_time, hovered.value.time_us);
        }

        if (self.hovered_counters.items.len > 0) {
            if (c.igBeginTooltip()) {
                c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "Time: {}", .{Timestamp{ .us = max_hovered_time }}) catch unreachable, null);

                for (self.hovered_counters.items, 0..) |hovered, index| {
                    _ = index;
                    if (hovered.value.time_us < max_hovered_time) {
                        continue;
                    }

                    c.ImDrawList_AddRect(
                        draw_list,
                        .{ .x = hovered.pos.x - 2, .y = hovered.pos.y - 2 },
                        .{ .x = hovered.pos.x + 2, .y = hovered.pos.y + 2 },
                        getImColorU32(.{ .x = 0, .y = 0, .z = 0, .w = 1.0 }),
                        0,
                        0,
                        1,
                    );

                    c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "{s}: {d:.2}", .{ hovered.name, hovered.value.value }) catch unreachable, null);
                }
            }
            c.igEndTooltip();

            self.hovered_counters.clearRetainingCapacity();
        }
    }

    fn drawThreads(self: *ViewState, region: ViewRegion, style: ViewStyle, threads: []Thread) void {
        const io = c.igGetIO();
        const mouse_pos = io.*.MousePos;
        const draw_list = c.igGetWindowDrawList();
        const allow_hover = c.igIsWindowHovered(0) and !self.is_dragging;

        var hovered_span: ?HoveredSpan = null;
        for (threads) |*thread| {
            if (thread.tracks.items.len == 0) {
                continue;
            }

            // Header
            {
                const name = blk: {
                    if (thread.name) |name| {
                        break :blk std.fmt.bufPrintZ(&global_buf, "{s}", .{name}) catch unreachable;
                    } else {
                        break :blk std.fmt.bufPrintZ(&global_buf, "Thread {}", .{thread.tid}) catch unreachable;
                    }
                };

                const lane_top = region.top() + c.igGetCursorPosY() - c.igGetScrollY();
                const lane_height = style.sub_lane_height;
                const lane_bottom = lane_top + lane_height;
                const lane_bb = c.ImRect{
                    .Min = .{ .x = region.left(), .y = lane_top },
                    .Max = .{ .x = region.right(), .y = lane_bottom },
                };

                var hovered: bool = false;
                drawLaneHeader(lane_bb, name, style.character_size.y, style.text_padding.x, allow_hover, &thread.ui.open, &hovered);
                if (hovered) {
                    if (io.*.MouseClicked[0]) {
                        thread.ui.open = !thread.ui.open;
                    }
                    if (thread.ui.open) {
                        if (c.igBeginTooltip()) {
                            c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "TID: {}", .{thread.tid}) catch unreachable, null);
                            if (thread.sort_index) |sort_index| {
                                c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "Sort Index: {}", .{sort_index}) catch unreachable, null);
                            }
                            if (thread.name) |thread_name| {
                                c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "Name: {s}", .{thread_name}) catch unreachable, null);
                            }
                        }
                        c.igEndTooltip();
                    }
                }
            }

            if (thread.ui.open) {
                const lane_top = region.top() + c.igGetCursorPosY() - c.igGetScrollY();
                const lane_height = @as(f32, @floatFromInt(thread.tracks.items.len)) * style.sub_lane_height;
                const lane_bb = c.ImRect{
                    .Min = .{ .x = region.left(), .y = lane_top },
                    .Max = .{ .x = region.right(), .y = lane_top + lane_height },
                };
                c.igItemSize_Rect(lane_bb, -1);
                if (c.igItemAdd(lane_bb, 0, null, 0)) {
                    for (thread.tracks.items, 0..) |sub_lane, sub_lane_index| {
                        var iter = sub_lane.iter(self.start_time_us, region.min_duration_us);
                        var sub_lane_top = lane_top + @as(f32, @floatFromInt(sub_lane_index)) * style.sub_lane_height;
                        while (iter.next()) |span| {
                            if (span.start_time_us > self.end_time_us) {
                                break;
                            }

                            var x1 = region.left() + @as(f32, @floatFromInt(span.start_time_us - self.start_time_us)) * region.width_per_us;
                            var x2 = x1 + @as(f32, @floatFromInt(@max(span.duration_us, region.min_duration_us))) * region.width_per_us;

                            x1 = @max(region.left(), x1);
                            x2 = @min(region.right(), x2);

                            const col = getColorForSpan(span);
                            var bb = c.ImRect{
                                .Min = .{ .x = x1, .y = sub_lane_top },
                                .Max = .{ .x = x2, .y = sub_lane_top + style.sub_lane_height },
                            };
                            c.ImDrawList_AddRectFilled(
                                draw_list,
                                bb.Min,
                                bb.Max,
                                col,
                                0,
                                0,
                            );

                            if (bb.Max.x - bb.Min.x > 2) {
                                c.ImDrawList_AddRect(
                                    draw_list,
                                    .{ .x = bb.Min.x + 0.5, .y = bb.Min.y + 0.5 },
                                    .{ .x = bb.Max.x - 0.5, .y = bb.Max.y - 0.5 },
                                    getImColorU32(.{ .x = 0, .y = 0, .z = 0, .w = 0.4 }),
                                    0,
                                    0,
                                    1,
                                );
                            }

                            if (allow_hover and c.ImRect_Contains_Vec2(&bb, mouse_pos)) {
                                hovered_span = .{
                                    .span = span,
                                    .bb = bb,
                                };
                            }

                            if (x2 - x1 > 2 * style.text_padding.x + style.character_size.x) {
                                const text_min_x = x1 + style.text_padding.x;
                                const text_max_x = x2 - style.text_padding.x;

                                const text = std.fmt.bufPrintZ(&global_buf, "{s}", .{span.name}) catch unreachable;
                                var text_size: c.ImVec2 = undefined;
                                c.igCalcTextSize(&text_size, text, null, false, 0);
                                const center_y = sub_lane_top + style.sub_lane_height / 2.0;

                                if (text_max_x - text_min_x >= text_size.x) {
                                    const center_x = text_min_x + (text_max_x - text_min_x) / 2.0;
                                    c.ImDrawList_AddText_Vec2(
                                        draw_list,
                                        .{ .x = center_x - text_size.x / 2.0, .y = center_y - style.character_size.y / 2.0 },
                                        getImColorU32(.{ .x = 0, .y = 0, .z = 0, .w = 1 }),
                                        text,
                                        null,
                                    );
                                } else {
                                    c.igRenderTextEllipsis(
                                        draw_list,
                                        .{ .x = text_min_x, .y = center_y - style.character_size.y / 2.0 },
                                        .{ .x = text_max_x, .y = sub_lane_top + style.sub_lane_height },
                                        text_max_x,
                                        text_max_x,
                                        text,
                                        null,
                                        null,
                                    );
                                }
                            }
                        }
                    }
                }
            }
        }

        if (hovered_span) |hovered| {
            const span = hovered.span;
            c.ImDrawList_AddRect(
                draw_list,
                hovered.bb.Min,
                hovered.bb.Max,
                getImColorU32(.{ .x = 0, .y = 0, .z = 0, .w = 1 }),
                0,
                0,
                2,
            );
            if (c.igBeginTooltip()) {
                c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "{s}", .{span.name}) catch unreachable, null);
                c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "Start: {}", .{Timestamp{ .us = span.start_time_us }}) catch unreachable, null);
                c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "Duration: {}", .{Timestamp{ .us = span.duration_us }}) catch unreachable, null);
                c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "Self: {}", .{Timestamp{ .us = span.self_duration_us }}) catch unreachable, null);
            }
            c.igEndTooltip();
        }
    }

    pub fn onLoadFileStart(self: *ViewState, len: usize) void {
        const state = .{ .load_file = LoadFileState.init(self.allocator, len) };
        switch_state(state);
    }
};

fn getColorForSpan(span: *const Span) u32 {
    const color_index: usize = @truncate(hashString(span.name));
    return getImColorU32(general_purpose_colors[color_index % general_purpose_colors.len]);
}

fn drawLaneHeader(lane_bb: c.ImRect, title: [:0]const u8, character_size_y: f32, text_padding_x: f32, allow_hover: bool, open: *bool, hovered: *bool) void {
    hovered.* = false;

    const mouse_pos = c.igGetIO().*.MousePos;
    const draw_list = c.igGetWindowDrawList();
    c.igItemSize_Rect(lane_bb, -1);
    if (!c.igItemAdd(lane_bb, 0, null, 0)) {
        return;
    }

    var text_size: c.ImVec2 = undefined;
    c.igCalcTextSize(&text_size, title, null, false, 0);
    const text_min_x = lane_bb.Min.x + character_size_y + text_padding_x;
    const text_max_x = @min(text_min_x + text_size.x, lane_bb.Max.x);

    const center_y = lane_bb.Min.y + (lane_bb.Max.y - lane_bb.Min.y) * 0.5;

    const col = if (open.*) getImColorU32(.{ .x = 0.3, .y = 0.3, .z = 0.3, .w = 1.0 }) else getImColorU32(.{ .x = 0.6, .y = 0.6, .z = 0.6, .w = 1.0 });

    c.ImDrawList_AddLine(
        draw_list,
        .{ .x = lane_bb.Min.x, .y = center_y },
        .{ .x = text_min_x - text_padding_x, .y = center_y },
        col,
        1,
    );

    if (!open.*) {
        c.igPushStyleColor_U32(c.ImGuiCol_Text, col);
    }
    c.igRenderTextEllipsis(
        draw_list,
        .{ .x = text_min_x, .y = center_y - character_size_y / 2.0 },
        .{ .x = text_max_x, .y = lane_bb.Max.y },
        text_max_x,
        text_max_x,
        title,
        null,
        null,
    );
    if (!open.*) {
        c.igPopStyleColor(1);
    }

    const header_right = text_max_x + text_padding_x;
    var header_bb = c.ImRect{
        .Min = .{ .x = lane_bb.Min.x, .y = lane_bb.Min.y },
        .Max = .{ .x = header_right, .y = lane_bb.Max.y },
    };

    if (allow_hover and c.ImRect_Contains_Vec2(&header_bb, mouse_pos)) {
        hovered.* = true;
    } else {
        hovered.* = false;
    }

    if (header_right < lane_bb.Max.x) {
        c.ImDrawList_AddLine(
            draw_list,
            .{ .x = header_right, .y = center_y },
            .{ .x = lane_bb.Max.x, .y = center_y },
            col,
            1,
        );
    }
}

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
    show_color_palette: bool,

    io: *c.ImGuiIO,
    mouse_pos_before_blur: c.ImVec2 = undefined,

    pub fn init(self: *App, allocator: Allocator, width: f32, height: f32) void {
        self.allocator = allocator;
        self.state = .{ .welcome = WelcomeState.init(allocator) };

        self.width = width;
        self.height = height;
        self.show_imgui_demo_window = false;
        self.show_color_palette = false;

        c.igSetAllocatorFunctions(imguiAlloc, imguiFree, null);

        _ = c.igCreateContext(null);

        const io = c.igGetIO();
        self.io = io;

        {
            var style = c.igGetStyle();
            c.igStyleColorsLight(style);

            style.*.ScrollbarRounding = 0.0;
            style.*.ScrollbarSize = 18.0;

            style.*.SeparatorTextBorderSize = 1.0;
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
                if (shouldLoadFile()) {
                    c.igSetCursorPosX(0);
                    if (c.igButton("Load", .{ .x = 0, .y = 0 })) {
                        js.showOpenFilePicker();
                    }
                }

                if (c.igBeginMenu("Misc", true)) {
                    if (c.igMenuItem_Bool("Color Palette", null, self.show_color_palette, true)) {
                        self.show_color_palette = !self.show_color_palette;
                    }
                    if (c.igMenuItem_Bool("Dear ImGui Demo", null, self.show_imgui_demo_window, true)) {
                        self.show_imgui_demo_window = !self.show_imgui_demo_window;
                    }
                    c.igEndMenu();
                }

                {
                    c.igSetCursorPosX(c.igGetCursorPosX() + 10.0);
                    const allocated_bytes: f64 = @floatFromInt(global_count_allocator.allocatedBytes());
                    const allocated_bytes_mb = allocated_bytes / 1024.0 / 1024.0;
                    const text = std.fmt.bufPrintZ(&global_buf, "Memory: {d:.1} MB", .{allocated_bytes_mb}) catch unreachable;
                    c.igTextUnformatted(text.ptr, null);
                }

                if (self.io.Framerate < 1000) {
                    const window_width = c.igGetWindowWidth();
                    const text = std.fmt.bufPrintZ(&global_buf, "{d:.0} ", .{self.io.Framerate}) catch unreachable;
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
            _ = window_flags;
            const viewport = c.igGetMainViewport();
            c.igSetNextWindowPos(viewport.*.WorkPos, 0, .{ .x = 0, .y = 0 });
            c.igSetNextWindowSize(viewport.*.WorkSize, 0);
            self.state.update(dt);
        }

        if (self.show_imgui_demo_window) {
            c.igShowDemoWindow(&self.show_imgui_demo_window);
        }

        if (self.show_color_palette) {
            if (c.igBegin("Color Palette", &self.show_color_palette, 0)) {
                c.igTextUnformatted("General Purpose Colors:", null);
                c.igPushID_Str("GeneralPurposeColors");
                for (&general_purpose_colors, 0..) |*color, index| {
                    const label = std.fmt.bufPrintZ(&global_buf, "[{}]:", .{index}) catch unreachable;
                    c.igTextUnformatted(label.ptr, null);
                    c.igSameLine(0, 0);
                    _ = c.igColorEdit4(label, @ptrCast(color), c.ImGuiColorEditFlags_NoLabel);
                }
                c.igPopID();
            }
            c.igEnd();
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
var global_count_allocator = CountAllocator.init(gpa.allocator());
var global_buf: [1024]u8 = [_]u8{0} ** 1024;

export fn init(width: f32, height: f32) void {
    var allocator = global_count_allocator.allocator();
    app = allocator.create(App) catch unreachable;
    app.init(allocator, width, height);

    // HACK: Force ImGui to update the mosue cursor, otherwise it's in uninitialized state.
    app.onMousePos(0, 0);
}

export fn update(dt: f32) void {
    app.update(dt);
}

export fn onResize(width: f32, height: f32) void {
    app.onResize(width, height);
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
    switch (app.state) {
        .welcome => {
            return true;
        },
        .load_file => |*load_file| {
            return load_file.shouldLoadFile();
        },
        .view => {
            return false;
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
