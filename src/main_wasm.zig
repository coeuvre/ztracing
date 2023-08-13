const std = @import("std");
const c = @import("c.zig");
const ig = @import("imgui.zig");
const CountAllocator = @import("./count_alloc.zig").CountAllocator;
const json_profile_parser = @import("./json_profile_parser.zig");
const easing = @import("./easing.zig");

const log = std.log;
const Allocator = std.mem.Allocator;

const JsonProfileParser = json_profile_parser.JsonProfileParser;
const TraceEvent = json_profile_parser.TraceEvent;

const Profile = @import("profile.zig").Profile;
const ProfileCounterValue = @import("profile.zig").ProfileCounterValue;
const Span = @import("profile.zig").Span;

fn normalize(v: f32, min: f32, max: f32) @TypeOf(v, min, max) {
    return (v - min) / (max - min);
}

fn hashString(s: []const u8) u64 {
    return std.hash.Wyhash.hash(0, s);
}

const Color = c.ImVec4;

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

var general_purpose_colors: [23]Color = undefined;

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
    x1: f32,
    x2: f32,
    time_us: i64,
    values: []const ProfileCounterValue,
};

const ViewState = struct {
    allocator: Allocator,
    profile: Profile,
    start_time_us: i64,
    end_time_us: i64,

    was_mouse_dragging: bool = false,
    mouse_down_start_time_us: i64 = 0,
    mouse_down_duration_us: i64 = 0,
    mouse_down_scroll_y: f32 = 0,

    pub fn init(allocator: Allocator, profile: Profile) ViewState {
        return .{
            .allocator = allocator,
            .profile = profile,
            .start_time_us = profile.min_time_us,
            .end_time_us = profile.max_time_us,
        };
    }

    pub fn update(self: *ViewState, dt: f32) void {
        _ = dt;
        const io = c.igGetIO();
        const mouse_pos = io.*.MousePos;
        const is_window_hovered = c.igIsWindowHovered(0);

        const window_pos = ig.getWindowPos();
        var window_content_bb = getWindowContentRegion();
        const window_width = window_content_bb.Max.x - window_content_bb.Min.x;

        const wheel = io.*.MouseWheel;
        if (is_window_hovered and wheel != 0) {
            if (io.*.KeyAlt) {
                // Zoom
                const mouse = io.*.MousePos.x - window_content_bb.Min.x;
                const p = mouse / window_width;
                var duration_us: f32 = @floatFromInt((self.end_time_us - self.start_time_us));
                const p_us = self.start_time_us + @as(i64, @intFromFloat(@round(p * duration_us)));
                if (wheel > 0) {
                    duration_us = duration_us * 0.8;
                } else {
                    duration_us = duration_us * 1.25;
                }
                duration_us = @min(duration_us, @as(f32, @floatFromInt(self.profile.max_time_us - self.profile.min_time_us)));
                duration_us = @max(0, duration_us);
                self.start_time_us = p_us - @as(i64, @intFromFloat(@round(p * duration_us)));
                self.end_time_us = self.start_time_us + @as(i64, @intFromFloat(@round(duration_us)));
            } else {
                // Scroll
                c.igSetScrollY_Float(c.igGetScrollY() - 100 * wheel);
            }
        }

        const lane_left = window_pos.x + c.igGetCursorPosX();
        const lane_width = window_width;
        const duration_us: f32 = @floatFromInt((self.end_time_us - self.start_time_us));
        const width_per_us = lane_width / duration_us;
        const min_duration_us: i64 = @intFromFloat(@ceil(duration_us / lane_width));
        const draw_lsit = c.igGetWindowDrawList();

        const is_mouse_dragging = c.igIsMouseDragging(c.ImGuiMouseButton_Left, 1);
        if (self.was_mouse_dragging) {
            if (is_mouse_dragging) {
                c.igSetMouseCursor(c.ImGuiMouseCursor_ResizeAll);
                var drag_delta: c.ImVec2 = undefined;
                c.igGetMouseDragDelta(&drag_delta, c.ImGuiMouseButton_Left, 0);

                const delta_us = drag_delta.x / width_per_us;
                self.start_time_us = self.mouse_down_start_time_us - @as(i64, @intFromFloat(@round(delta_us)));
                self.end_time_us = self.start_time_us + self.mouse_down_duration_us;

                c.igSetScrollY_Float(self.mouse_down_scroll_y - drag_delta.y);
            } else {
                self.was_mouse_dragging = false;
            }
        } else {
            if (is_window_hovered and is_mouse_dragging) {
                self.was_mouse_dragging = true;
                self.mouse_down_start_time_us = self.start_time_us;
                self.mouse_down_duration_us = self.end_time_us - self.start_time_us;
                self.mouse_down_scroll_y = c.igGetScrollY();
            }
        }

        var character_size: c.ImVec2 = undefined;
        c.igCalcTextSize(&character_size, "A", null, false, 0);

        for (self.profile.counter_lanes.items) |counter_lane| {
            c.igSeparatorText(std.fmt.bufPrintZ(&global_buf, "{s}", .{counter_lane.name}) catch unreachable);

            const lane_top = window_pos.y + c.igGetCursorPosY() - c.igGetScrollY();
            const lane_height: f32 = 30.0;

            var maybe_hovered_counter: ?HoveredCounter = null;

            {
                var iter = counter_lane.iter(self.start_time_us, min_duration_us);

                var x1: f32 = 0;
                var x2: f32 = 0;
                var values: ?[]const ProfileCounterValue = null;
                var time_us: i64 = 0;
                if (iter.next()) |counter| {
                    if (counter.time_us > self.start_time_us) {
                        x2 = @max(lane_left, lane_left + @as(f32, @floatFromInt(counter.time_us - self.start_time_us)) * width_per_us);
                        time_us = counter.time_us;
                        values = counter.values.items;
                    }
                }

                while (iter.next()) |counter| {
                    x1 = x2;
                    x2 = lane_left + @as(f32, @floatFromInt(counter.time_us - self.start_time_us)) * width_per_us;

                    const color_index_base: usize = @truncate(hashString(counter_lane.name));

                    if (values != null and values.?.len > 0) {
                        const bb = c.ImRect{
                            .Min = .{ .x = x1, .y = lane_top },
                            .Max = .{ .x = x2, .y = lane_top + lane_height },
                        };
                        c.igItemSize_Rect(bb, -1);
                        const id = c.igGetID_Str(std.fmt.bufPrintZ(&global_buf, "##{s}_{}", .{ counter_lane.name, counter.time_us }) catch unreachable);
                        if (c.igItemAdd(bb, id, null, 0)) {
                            var hovered = false;
                            var held = false;
                            _ = c.igButtonBehavior(bb, id, &hovered, &held, 0);
                            if (hovered) {
                                maybe_hovered_counter = .{
                                    .x1 = x1,
                                    .x2 = x2,
                                    .time_us = time_us,
                                    .values = values.?,
                                };
                            }
                            var y2 = bb.Max.y;
                            for (values.?, 0..) |value, i| {
                                const y1 = y2 - lane_height * @as(f32, @floatCast(value.value / counter_lane.max_value));
                                const col = general_purpose_colors[(color_index_base + i) % general_purpose_colors.len];
                                c.ImDrawList_AddRectFilled(draw_lsit, .{ .x = x1, .y = y1 }, .{ .x = x2, .y = y2 }, getImColorU32(col), 0, 0);
                                y2 = y1;
                            }
                        }
                        c.igSameLine(0, 0);
                    }

                    time_us = counter.time_us;
                    values = counter.values.items;

                    if (x2 > window_content_bb.Max.x) {
                        break;
                    }
                }
            }

            // Deferred rendering of hovered counter to avoid overlapping with other counters
            if (maybe_hovered_counter) |hovered_counter| {
                // TODO: Styling
                const rad = 4.0;
                const col = getImColorU32(general_purpose_colors[0]);

                var sum: f64 = 0;
                for (hovered_counter.values) |value| {
                    sum += value.value;
                }
                const height: f32 = @floatCast(sum / counter_lane.max_value * lane_height);
                c.ImDrawList_AddCircleFilled(draw_lsit, .{ .x = hovered_counter.x1, .y = lane_top + lane_height - height }, rad, col, 16);

                if (c.igBeginTooltip()) {
                    c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "{}", .{hovered_counter.time_us}) catch unreachable, null);
                    for (hovered_counter.values) |value| {
                        c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "{s}: {d:.2}", .{ value.name, value.value }) catch unreachable, null);
                    }
                }
                c.igEndTooltip();
            }

            c.igNewLine();
        }

        const text_padding_x: f32 = character_size.x;
        const text_padding_y: f32 = character_size.y / 4.0;
        const sub_lane_height: f32 = 2 * text_padding_y + character_size.y;
        for (self.profile.thread_lanes.items) |thread_lane| {
            if (thread_lane.sub_lanes.items.len == 0) {
                continue;
            }

            const name = blk: {
                if (thread_lane.name) |name| {
                    break :blk std.fmt.bufPrintZ(&global_buf, "{s}", .{name}) catch unreachable;
                } else {
                    break :blk std.fmt.bufPrintZ(&global_buf, "Thread {}", .{thread_lane.tid}) catch unreachable;
                }
            };
            c.igSeparatorText(name);

            const lane_top = window_pos.y + c.igGetCursorPosY() - c.igGetScrollY();
            const lane_height = @as(f32, @floatFromInt(thread_lane.sub_lanes.items.len)) * sub_lane_height;
            const lane_bb = c.ImRect{
                .Min = .{ .x = lane_left, .y = lane_top },
                .Max = .{ .x = lane_left + lane_width, .y = lane_top + lane_height },
            };
            c.igItemSize_Rect(lane_bb, -1);
            const id = c.igGetID_Str(std.fmt.bufPrintZ(&global_buf, "##thread-{}", .{thread_lane.tid}) catch unreachable);
            if (c.igItemAdd(lane_bb, id, null, 0)) {
                for (thread_lane.sub_lanes.items, 0..) |sub_lane, sub_lane_index| {
                    var iter = sub_lane.iter(self.start_time_us, min_duration_us);
                    var sub_lane_top = lane_top + @as(f32, @floatFromInt(sub_lane_index)) * sub_lane_height;
                    while (iter.next()) |span| {
                        var x1 = lane_left + @as(f32, @floatFromInt(span.start_time_us - self.start_time_us)) * width_per_us;
                        var x2 = x1 + @as(f32, @floatFromInt(@max(span.duration_us, min_duration_us))) * width_per_us;

                        x1 = @max(lane_left, x1);
                        x2 = @min(lane_left + lane_width, x2);

                        const col = getColorForSpan(span);
                        var bb = c.ImRect{
                            .Min = .{ .x = x1, .y = sub_lane_top },
                            .Max = .{ .x = x2, .y = sub_lane_top + sub_lane_height },
                        };
                        c.ImDrawList_AddRectFilled(
                            draw_lsit,
                            bb.Min,
                            bb.Max,
                            col,
                            0,
                            0,
                        );

                        if (bb.Max.x - bb.Min.x > 2) {
                            c.ImDrawList_AddRect(
                                draw_lsit,
                                .{ .x = bb.Min.x + 0.5, .y = bb.Min.y + 0.5 },
                                .{ .x = bb.Max.x - 0.5, .y = bb.Max.y - 0.5 },
                                getImColorU32(.{ .x = 0, .y = 0, .z = 0, .w = 0.4 }),
                                0,
                                0,
                                1,
                            );
                        }

                        if (is_window_hovered and !is_mouse_dragging and c.ImRect_Contains_Vec2(&bb, mouse_pos)) {
                            c.ImDrawList_AddRect(
                                draw_lsit,
                                bb.Min,
                                bb.Max,
                                getImColorU32(.{ .x = 0, .y = 0, .z = 0, .w = 1 }),
                                0,
                                0,
                                2,
                            );

                            if (c.igBeginTooltip()) {
                                c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "{s}", .{span.name}) catch unreachable, null);
                                c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "Start: {}", .{span.start_time_us}) catch unreachable, null);
                                c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "Duration: {}", .{span.duration_us}) catch unreachable, null);
                            }
                            c.igEndTooltip();
                        }

                        if (x2 - x1 > 2 * text_padding_x + character_size.x) {
                            const text_min_x = x1 + text_padding_x;
                            const text_max_x = x2 - text_padding_x;

                            const text = std.fmt.bufPrintZ(&global_buf, "{s}", .{span.name}) catch unreachable;
                            var text_size: c.ImVec2 = undefined;
                            c.igCalcTextSize(&text_size, text, null, false, 0);
                            const center_y = sub_lane_top + sub_lane_height / 2.0;

                            if (text_max_x - text_min_x >= text_size.x) {
                                const center_x = text_min_x + (text_max_x - text_min_x) / 2.0;
                                c.ImDrawList_AddText_Vec2(
                                    draw_lsit,
                                    .{ .x = center_x - text_size.x / 2.0, .y = center_y - character_size.y / 2.0 },
                                    getImColorU32(.{ .x = 0, .y = 0, .z = 0, .w = 1 }),
                                    text,
                                    null,
                                );
                            } else {
                                c.igRenderTextEllipsis(
                                    draw_lsit,
                                    .{ .x = text_min_x, .y = center_y - character_size.y / 2.0 },
                                    .{ .x = text_max_x, .y = sub_lane_top + sub_lane_height },
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

    pub fn onLoadFileStart(self: *ViewState, len: usize) void {
        const state = .{ .load_file = LoadFileState.init(self.allocator, len) };
        switch_state(state);
    }
};

fn getColorForSpan(span: *const Span) u32 {
    const color_index: usize = @truncate(hashString(span.name));
    return getImColorU32(general_purpose_colors[color_index % general_purpose_colors.len]);
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
                    if (c.igMenuItem_Bool("Show ImGui Demo Window", null, self.show_imgui_demo_window, true)) {
                        self.show_imgui_demo_window = !self.show_imgui_demo_window;
                    }
                    if (c.igMenuItem_Bool("Color Pattle", null, self.show_color_palette, true)) {
                        self.show_color_palette = !self.show_color_palette;
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
            const viewport = c.igGetMainViewport();
            c.igSetNextWindowPos(viewport.*.WorkPos, 0, .{ .x = 0, .y = 0 });
            c.igSetNextWindowSize(viewport.*.WorkSize, 0);
            _ = c.igBegin("MainWindow", null, window_flags | c.ImGuiWindowFlags_NoScrollWithMouse);
            self.state.update(dt);

            c.igEnd();
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
        if (key == c.ImGuiKey_LeftAlt or key == c.ImGuiKey_RightAlt) {
            c.ImGuiIO_AddKeyEvent(self.io, c.ImGuiMod_Alt, down);
        }

        return self.io.*.WantCaptureKeyboard;
    }

    pub fn onFocus(self: *App, focused: bool) void {
        c.ImGuiIO_AddFocusEvent(self.io, focused);
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
    {
        var generator = SinebowColorGenerator.init(1.0, 1.5);
        for (&general_purpose_colors) |*color| {
            color.* = generator.nextColor();
        }
    }

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
