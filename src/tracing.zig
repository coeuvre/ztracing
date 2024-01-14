const std = @import("std");
const c = @import("c.zig");
const ig = @import("imgui.zig");
const json_profile_parser = @import("json_profile_parser.zig");
const easing = @import("easing.zig");
const profile_ = @import("profile.zig");
const tracy = @import("tracy.zig");

const Allocator = std.mem.Allocator;
const JsonProfileParser = json_profile_parser.JsonProfileParser;
const CountAllocator = @import("count_alloc.zig").CountAllocator;
const TraceEvent = json_profile_parser.TraceEvent;
const Profile = profile_.Profile;
const Span = profile_.Span;
const SeriesValue = profile_.SeriesValue;
const Counter = profile_.Counter;
const Thread = profile_.Thread;

var global_buf: [1024]u8 = [_]u8{0} ** 1024;

pub const Tracing = struct {
    const Self = @This();

    count_allocator: *CountAllocator,
    state: State,
    show_open_file_picker: ?*const fn () void,

    show_imgui_demo_window: bool = false,
    show_color_palette: bool = false,

    pub fn init(count_allocator: *CountAllocator, show_open_file_picker: ?*const fn () void) Self {
        return .{
            .count_allocator = count_allocator,
            .state = .{ .welcome = WelcomeState.init(count_allocator.allocator()) },
            .show_open_file_picker = show_open_file_picker,
        };
    }

    pub fn update(self: *Self, dt: f32) void {
        const trace = tracy.trace(@src());
        defer trace.end();

        // Main Menu Bar
        {
            c.igPushStyleVar_Vec2(c.ImGuiStyleVar_FramePadding, .{ .x = 10, .y = 4 });
            if (c.igBeginMainMenuBar()) {
                if (self.show_open_file_picker != null and self.should_load_file()) {
                    c.igSetCursorPosX(0);
                    if (c.igButton("Load", .{ .x = 0, .y = 0 })) {
                        self.show_open_file_picker.?();
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
                    const allocated_bytes: f64 = @floatFromInt(self.count_allocator.allocatedBytes());
                    const allocated_bytes_mb = allocated_bytes / 1024.0 / 1024.0;
                    const text = std.fmt.bufPrintZ(&global_buf, "Memory: {d:.1} MB", .{allocated_bytes_mb}) catch unreachable;
                    c.igTextUnformatted(text.ptr, null);
                }

                const io = c.igGetIO();
                if (io.*.Framerate < 10000) {
                    const window_width = c.igGetWindowWidth();
                    const text = std.fmt.bufPrintZ(&global_buf, "{d:.0} ", .{io.*.Framerate}) catch unreachable;
                    var text_size: c.ImVec2 = undefined;
                    c.igCalcTextSize(&text_size, text.ptr, null, false, -1.0);
                    c.igSetCursorPosX(window_width - text_size.x);
                    c.igTextUnformatted(text.ptr, null);
                }

                c.igEndMainMenuBar();
            }
            c.igPopStyleVar(1);
        }

        self.state.update(dt, self);

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
    }

    pub fn should_load_file(self: *const Self) bool {
        switch (self.state) {
            .welcome => {
                return true;
            },
            .load_file => |load_file| {
                return load_file.should_load_file();
            },
            .view => {
                return true;
            },
        }
    }

    pub fn on_load_file_start(self: *Self) void {
        switch (self.state) {
            .welcome => |*welcome| {
                welcome.on_file_load_start(self);
            },
            .view => |*view| {
                view.on_file_load_start(self);
            },
            else => {
                std.log.err("Unexpected event on_file_load_start, current state is {s}", .{@tagName(self.state)});
            },
        }
    }

    pub fn on_load_file_progress(self: *Self, offset: usize, total: usize) void {
        switch (self.state) {
            .load_file => |*load_file| {
                load_file.on_load_file_progress(offset, total);
            },
            else => {
                std.log.err("Unexpected event onLoadFileChunk, current state is {s}", .{@tagName(self.state)});
            },
        }
    }

    pub fn on_load_file_done(self: *Self, profile: *Profile) void {
        switch (self.state) {
            .load_file => |*load_file| {
                load_file.on_load_file_done(profile, self);
            },
            else => {
                std.log.err("Unexpected event onLoadFileDone, current state is {s}", .{@tagName(self.state)});
            },
        }
    }

    pub fn on_load_file_error(self: *Self, msg: []const u8) void {
        switch (self.state) {
            .load_file => |*load_file| {
                load_file.on_load_file_error(msg);
            },
            else => {
                std.log.err("Unexpected event onLoadFileDone, current state is {s}", .{@tagName(self.state)});
            },
        }
    }

    fn switch_state(self: *Self, new_state: State) void {
        self.state.deinit();
        self.state = new_state;
    }
};

const State = union(enum) {
    welcome: WelcomeState,
    load_file: LoadFileState,
    view: ViewState,

    pub fn update(self: *State, dt: f32, tracing: *Tracing) void {
        switch (self.*) {
            inline else => |*s| s.update(dt, tracing),
        }
    }

    pub fn deinit(self: *State) void {
        switch (self.*) {
            inline else => |*s| {
                s.deinit();
            },
        }
    }
};

const container_window_flags =
    c.ImGuiWindowFlags_NoTitleBar |
    c.ImGuiWindowFlags_NoCollapse |
    c.ImGuiWindowFlags_NoResize |
    c.ImGuiWindowFlags_NoMove |
    c.ImGuiWindowFlags_NoBringToFrontOnFocus |
    c.ImGuiWindowFlags_NoNavFocus;

// Returns current window content region in screen space
fn get_window_content_region() c.ImRect {
    const pos = ig.getWindowPos();
    const scroll_x = c.igGetScrollX();
    const scroll_y = c.igGetScrollY();
    var min = ig.getWindowContentRegionMin();
    min.x += pos.x + scroll_x;
    min.y += pos.y + scroll_y;
    var max = ig.getWindowContentRegionMax();
    max.x += pos.x + scroll_x;
    max.y += pos.y + scroll_y;
    return .{ .Min = min, .Max = max };
}

fn ig_text_centered(bb: c.ImRect, text: []const u8) void {
    const text_end = text.ptr + text.len;
    var text_size: c.ImVec2 = undefined;
    c.igCalcTextSize(&text_size, text.ptr, text_end, false, 0);
    const x = bb.Min.x + (bb.Max.x - bb.Min.x) * 0.5 - text_size.x * 0.5;
    const y = bb.Min.y + (bb.Max.y - bb.Min.y) * 0.5 - text_size.y * 0.5;
    c.igSetCursorPosX(x);
    c.igSetCursorPosY(y);
    c.igTextUnformatted(text.ptr, text_end);
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

    pub fn deinit(self: *WelcomeState) void {
        _ = self;
    }

    pub fn update(self: *WelcomeState, dt: f32, tracing: *Tracing) void {
        _ = self;
        _ = dt;
        _ = tracing;
        const viewport = c.igGetMainViewport();
        c.igSetNextWindowPos(viewport.*.WorkPos, 0, .{ .x = 0, .y = 0 });
        c.igSetNextWindowSize(viewport.*.WorkSize, 0);
        _ = c.igBegin("MainWindow", null, container_window_flags);

        const logo =
            \\ ________  _________  _______          _        ______  _____  ____  _____   ______   
            \\|  __   _||  _   _  ||_   __ \        / \     .' ___  ||_   _||_   \|_   _|.' ___  |  
            \\|_/  / /  |_/ | | \_|  | |__) |      / _ \   / .'   \_|  | |    |   \ | | / .'   \_|  
            \\   .'.' _     | |      |  __ /      / ___ \  | |         | |    | |\ \| | | |   ____  
            \\ _/ /__/ |   _| |_    _| |  \ \_  _/ /   \ \_\ `.___.'\ _| |_  _| |_\   |_\ `.___]  | 
            \\|________|  |_____|  |____| |___||____| |____|`.____ .'|_____||_____|\____|`._____.'  
            \\                                                                                      
            \\
            \\                        Drag & Drop a trace file to start.
            \\
            \\
        ;

        ig_text_centered(get_window_content_region(), logo);

        c.igEnd();
    }

    pub fn on_file_load_start(self: *WelcomeState, tracing: *Tracing) void {
        const state = .{ .load_file = LoadFileState.init(self.allocator) };
        tracing.switch_state(state);
    }
};

const LoadFileState = struct {
    allocator: Allocator,
    total: usize,
    offset: usize,
    progress_message: ?[:0]u8,
    error_message: ?[:0]u8,

    const popup_id = "LoadFilePopup";

    pub fn init(allocator: Allocator) LoadFileState {
        return .{
            .allocator = allocator,
            .total = 0,
            .offset = 0,
            .progress_message = null,
            .error_message = null,
        };
    }

    pub fn update(self: *LoadFileState, dt: f32, tracing: *Tracing) void {
        _ = dt;
        const viewport = c.igGetMainViewport();
        c.igSetNextWindowPos(viewport.*.WorkPos, 0, .{ .x = 0, .y = 0 });
        c.igSetNextWindowSize(viewport.*.WorkSize, 0);
        _ = c.igBegin("MainWindow", null, container_window_flags);
        if (self.error_message) |err| {
            c.igTextWrapped("%s", err.ptr);

            if (c.igButton("OK", .{ .x = 120, .y = 0 })) {
                c.igCloseCurrentPopup();
                tracing.switch_state(.{ .welcome = WelcomeState.init(self.allocator) });
            }
        } else {
            if (self.total > 0) {
                if (self.offset == self.total) {
                    self.set_progress("Preparing the view ...", .{});
                } else {
                    const offset: f32 = @floatFromInt(self.offset);
                    const total: f32 = @floatFromInt(self.total);
                    const percentage: i32 = @intFromFloat(@round(offset / total * 100.0));
                    self.set_progress("Loading file ... ({}%)", .{percentage});
                }
            } else {
                self.set_progress("Loading file ... ({d:.2} MB)", .{@as(f32, @floatFromInt(self.offset)) / 1000.0 / 1000.0});
            }

            ig_text_centered(get_window_content_region(), self.progress_message.?);
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

    pub fn should_load_file(self: *const LoadFileState) bool {
        return self.error_message == null;
    }

    fn set_progress(self: *LoadFileState, comptime fmt: []const u8, args: anytype) void {
        if (self.progress_message) |msg| {
            self.allocator.free(msg);
        }
        self.progress_message = std.fmt.allocPrintZ(self.allocator, fmt, args) catch unreachable;
    }

    fn setError(self: *LoadFileState, comptime fmt: []const u8, args: anytype) void {
        std.debug.assert(self.error_message == null);
        self.error_message = std.fmt.allocPrintZ(self.allocator, fmt, args) catch unreachable;
    }

    pub fn on_load_file_progress(self: *LoadFileState, offset: usize, total: usize) void {
        self.total = total;
        self.offset = offset;
    }

    pub fn on_load_file_done(self: *LoadFileState, profile: *Profile, tracing: *Tracing) void {
        tracing.switch_state(.{ .view = ViewState.init(self.allocator, profile) });
    }

    pub fn on_load_file_error(self: *LoadFileState, msg: []const u8) void {
        self.setError("{s}", .{msg});
    }
};

const ViewStyle = struct {
    sub_lane_height: f32,
    character_size: c.ImVec2,
    text_padding: c.ImVec2,
};

const ViewState = struct {
    allocator: Allocator,
    profile: *Profile,
    start_time_us: i64,
    end_time_us: i64,
    hovered_counters: std.ArrayList(HoveredCounter),

    is_dragging: bool = false,
    drag_start: ViewPos = undefined,

    open_selection_span: bool = false,
    selected_span: ?*const Span = null,

    pub fn init(allocator: Allocator, profile: *Profile) ViewState {
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

    pub fn deinit(self: *ViewState) void {
        self.profile.deinit();
        self.allocator.destroy(self.profile);
        self.hovered_counters.deinit();
    }

    fn calcRegion(self: *ViewState, bb: c.ImRect) ViewRegion {
        const width_per_us = (bb.Max.x - bb.Min.x) / @as(f32, @floatFromInt((self.end_time_us - self.start_time_us)));
        const min_width = 6;
        const min_duration_us: i64 = @intFromFloat(@ceil(min_width / width_per_us));
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

    pub fn update(self: *ViewState, dt: f32, tracing: *Tracing) void {
        _ = dt;
        _ = tracing;
        const style = self.calcStyle();

        const viewport = c.igGetMainViewport();
        c.igSetNextWindowPos(.{ .x = viewport.*.WorkPos.x, .y = viewport.*.WorkPos.y }, 0, .{ .x = 0, .y = 0 });
        c.igSetNextWindowSize(.{ .x = viewport.*.WorkSize.x, .y = viewport.*.WorkSize.y }, 0);

        c.igPushStyleVar_Float(c.ImGuiStyleVar_WindowRounding, 0);
        c.igPushStyleVar_Vec2(c.ImGuiStyleVar_WindowPadding, .{ .x = 0, .y = 0 });
        _ = c.igBegin("MainWindow", null, container_window_flags | c.ImGuiWindowFlags_NoDocking);

        const dock_id = c.igGetID_Str("MainWindowDock");
        _ = c.igDockSpace(dock_id, .{ .x = 0, .y = 0 }, c.ImGuiDockNodeFlags_PassthruCentralNode | c.ImGuiDockNodeFlags_NoDockingOverCentralNode, 0);
        c.igSetNextWindowDockID(dock_id, 0);

        const window_class = c.ImGuiWindowClass_ImGuiWindowClass();
        window_class.*.DockNodeFlagsOverrideSet = c.ImGuiDockNodeFlags_NoTabBar;
        defer c.ImGuiWindowClass_destroy(window_class);
        c.igSetNextWindowClass(window_class);

        _ = c.igBegin("WorkArea", null, container_window_flags);
        c.igPopStyleVar(2);

        const timeline_height = style.sub_lane_height;

        self.drawTimeline(timeline_height, style);
        self.draw_main_view(timeline_height, style);

        if (self.selected_span) |span| {
            if (self.open_selection_span) {
                if (c.igBegin("Selected Span", &self.open_selection_span, c.ImGuiWindowFlags_AlwaysAutoResize)) {
                    self.drawSpan(span);
                }
                c.igEnd();
            }
        }

        c.igEnd();
        c.igEnd();
    }

    fn draw_main_view(self: *ViewState, timeline_height: f32, style: ViewStyle) void {
        const trace = tracy.trace(@src());
        defer trace.end();

        c.igSetCursorPosY(timeline_height);

        c.igPushStyleVar_Float(c.ImGuiStyleVar_WindowRounding, 0);
        c.igPushStyleVar_Vec2(c.ImGuiStyleVar_WindowPadding, .{ .x = 0, .y = 0 });
        _ = c.igBeginChild_Str("MainView", .{ .x = 0, .y = 0 }, 0, container_window_flags | c.ImGuiWindowFlags_NoScrollWithMouse);
        c.igPopStyleVar(2);

        const window_bb = get_window_content_region();
        var region = self.calcRegion(window_bb);

        self.handleDrag(region);
        self.handleScroll(region);

        // Recalculate region after zoom
        region = self.calcRegion(window_bb);

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
                self.draw_counters(region, style, process.counters.items);
                self.draw_threads(region, style, process.threads.items);
            }
        }

        c.ImDrawList_PopClipRect(draw_list);

        c.igEndChild();
    }

    fn drawTimeline(self: *ViewState, timeline_height: f32, style: ViewStyle) void {
        const trace = tracy.trace(@src());
        defer trace.end();

        c.igPushStyleVar_Float(c.ImGuiStyleVar_WindowRounding, 0);
        c.igPushStyleVar_Vec2(c.ImGuiStyleVar_WindowPadding, .{ .x = 0, .y = 0 });
        _ = c.igBeginChild_Str("Timeline", .{ .x = 0, .y = timeline_height }, 0, container_window_flags);
        c.igPopStyleVar(2);

        const timeline_bb = get_window_content_region();

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

        const text_color = get_im_color_u32(.{ .x = 0, .y = 0, .z = 0, .w = 1 });
        var time_us = @divTrunc(self.start_time_us, block_duration_us) * block_duration_us;
        while (time_us < self.end_time_us) : (time_us += block_duration_us) {
            const is_large_block = @rem(time_us, large_block_duration_us) == 0;

            const x = region.left() + @as(f32, @floatFromInt(time_us - self.start_time_us)) * region.width_per_us;
            const y1 = region.bottom();
            const y2 = if (is_large_block) y1 - 12 else y1 - 6;
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

        c.ImDrawList_AddLine(draw_list, .{ .x = region.left(), .y = region.bottom() - 1 }, .{ .x = region.right(), .y = region.bottom() - 1 }, text_color, 1);

        c.igEndChild();
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

    fn draw_counters(self: *ViewState, region: ViewRegion, style: ViewStyle, counters: []Counter) void {
        const trace = tracy.trace(@src());
        defer trace.end();

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
                if (hovered and counter.ui.open) {
                    if (c.igBeginTooltip()) {
                        c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "{s}", .{counter.name}) catch unreachable, null);
                    }
                    c.igEndTooltip();
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
                    const color_index_base: usize = @truncate(hash_str(counter.name));

                    for (counter.series.items, 0..) |series, series_index| {
                        const col_v4 = general_purpose_colors[(color_index_base + series_index) % general_purpose_colors.len];
                        const col = get_im_color_u32(col_v4);

                        var iter = series.iter(self.start_time_us, region.min_duration_us);
                        var prev_pos: ?c.ImVec2 = null;
                        var prev_value: ?*const SeriesValue = null;
                        var hovered_counter: ?HoveredCounter = null;
                        while (iter.next()) |value| {
                            const pos = c.ImVec2{
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
                                    get_im_color_u32(.{ .x = col_v4.x * 0.5, .y = col_v4.y * 0.5, .z = col_v4.z * 0.5, .w = 1.0 }),
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
                        get_im_color_u32(.{ .x = 0, .y = 0, .z = 0, .w = 1.0 }),
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

    fn draw_threads(self: *ViewState, region: ViewRegion, style: ViewStyle, threads: []Thread) void {
        const trace = tracy.trace(@src());
        defer trace.end();

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
                const trace1 = tracy.traceNamed(@src(), "draw_threads/header");
                defer trace1.end();
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
                if (hovered and thread.ui.open) {
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

            if (thread.ui.open) {
                const trace1 = tracy.traceNamed(@src(), "draw_threads/body");
                defer trace1.end();

                const lane_top = region.top() + c.igGetCursorPosY() - c.igGetScrollY();
                const lane_height = @as(f32, @floatFromInt(thread.tracks.items.len)) * style.sub_lane_height;
                const lane_bb = c.ImRect{
                    .Min = .{ .x = region.left(), .y = lane_top },
                    .Max = .{ .x = region.right(), .y = lane_top + lane_height },
                };
                c.igItemSize_Rect(lane_bb, -1);
                if (c.igItemAdd(lane_bb, 0, null, 0)) {
                    var sub_lane_top = lane_top;
                    for (thread.tracks.items) |sub_lane| {
                        const trace2 = tracy.traceNamed(@src(), "draw_threads/body/track");
                        defer trace2.end();

                        var iter = sub_lane.iter(self.start_time_us, region.min_duration_us);
                        while (iter.next()) |span| {
                            if (span.start_time_us > self.end_time_us) {
                                break;
                            }

                            var x1: f32 = region.left() + @as(f32, @floatFromInt(span.start_time_us - self.start_time_us)) * region.width_per_us;
                            var x2: f32 = x1 + @as(f32, @floatFromInt(@max(span.duration_us, region.min_duration_us))) * region.width_per_us;
                            x1 = @max(region.left(), x1);
                            x2 = @min(region.right(), x2);

                            {
                                const col = get_color_for_span(span);
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
                                        get_im_color_u32(.{ .x = 0, .y = 0, .z = 0, .w = 0.4 }),
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
                                        get_im_color_u32(.{ .x = 0, .y = 0, .z = 0, .w = 1 }),
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

                        sub_lane_top += style.sub_lane_height;
                    }
                }
            }
        }

        if (hovered_span) |*hovered| {
            const span = hovered.span;
            c.ImDrawList_AddRect(
                draw_list,
                hovered.bb.Min,
                hovered.bb.Max,
                get_im_color_u32(.{ .x = 0, .y = 0, .z = 0, .w = 1 }),
                0,
                0,
                2,
            );
            if (c.igBeginTooltip()) {
                self.drawSpan(span);
            }
            c.igEndTooltip();

            if (io.*.MouseReleased[0] and c.ImRect_Contains_Vec2(&hovered.bb, io.*.MouseClickedPos[0])) {
                self.selected_span = span;
                self.open_selection_span = true;
            }
        }
    }

    fn drawSpan(self: *ViewState, span: *const Span) void {
        _ = self;
        c.igPushTextWrapPos(c.igGetCursorPosX() + 400);
        c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "Title: {s}", .{span.name}) catch unreachable, null);
        if (span.category) |cat| {
            c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "Category: {s}", .{cat}) catch unreachable, null);
        }
        c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "Start: {}", .{Timestamp{ .us = span.start_time_us }}) catch unreachable, null);
        c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "Duration: {}", .{Timestamp{ .us = span.duration_us }}) catch unreachable, null);
        c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "Self: {}", .{Timestamp{ .us = span.self_duration_us }}) catch unreachable, null);
        c.igPopTextWrapPos();
    }

    pub fn on_file_load_start(self: *ViewState, tracing: *Tracing) void {
        const state = .{ .load_file = LoadFileState.init(self.allocator) };
        tracing.switch_state(state);
    }
};

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

fn get_color_for_span(span: *Span) u32 {
    const color_index: usize = @truncate(hash_str(span.name));
    return get_im_color_u32(general_purpose_colors[color_index % general_purpose_colors.len]);
}

fn drawLaneHeader(lane_bb: c.ImRect, title: [:0]const u8, character_size_y: f32, text_padding_x: f32, allow_hover: bool, open: *bool, hovered: *bool) void {
    const io = c.igGetIO();

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

    const col = if (open.*)
        get_im_color_u32(.{ .x = 0.3, .y = 0.3, .z = 0.3, .w = 1.0 })
    else
        get_im_color_u32(.{ .x = 0.6, .y = 0.6, .z = 0.6, .w = 1.0 });

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

    if (hovered.* and io.*.MouseReleased[0] and c.ImRect_Contains_Vec2(&header_bb, io.*.MouseClickedPos[0])) {
        open.* = !open.*;
    }
}

const Color = c.ImVec4;

fn hash_str(s: []const u8) u64 {
    return std.hash.Wyhash.hash(0, s);
}

fn get_im_color_u32(color: Color) u32 {
    return c.igGetColorU32_Vec4(color);
}

fn normalize(v: f32, min: f32, max: f32) @TypeOf(v, min, max) {
    return (v - min) / (max - min);
}

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
                    try writer.print(" {}us", .{Number{ .num = @intCast(@abs(us)) }});
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
        const abs = @abs(self.num);
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
