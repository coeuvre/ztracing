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
const Track = profile_.Track;
const SeriesValue = profile_.SeriesValue;
const Counter = profile_.Counter;
const Thread = profile_.Thread;

var global_buf: [1024]u8 = [_]u8{0} ** 1024;

pub const PlatformApi = struct {
    show_open_file_picker: ?*const fn () void,
    get_memory_usages: *const fn () usize,
};

pub const Tracing = struct {
    const Self = @This();

    allocator: Allocator,
    state: State,
    api: PlatformApi,

    file_name: ?[:0]u8 = null,
    show_imgui_demo_window: bool = false,
    show_color_palette: bool = false,

    pub fn init(allocator: Allocator, api: PlatformApi) Self {
        return .{
            .allocator = allocator,
            .state = .{ .welcome = WelcomeState.init(allocator) },
            .api = api,
        };
    }

    pub fn update(self: *Self, dt: f32) void {
        const trace = tracy.trace(@src());
        defer trace.end();

        // Main Menu Bar
        {
            c.igPushStyleVar_Vec2(c.ImGuiStyleVar_FramePadding, .{ .x = 10, .y = 4 });
            if (c.igBeginMainMenuBar()) {
                if (self.api.show_open_file_picker != null and self.should_load_file()) {
                    c.igSetCursorPosX(0);
                    if (c.igButton("Load", .{ .x = 0, .y = 0 })) {
                        self.api.show_open_file_picker.?();
                    }
                }

                if (c.igBeginMenu("About", true)) {
                    if (c.igMenuItem_Bool("Color Palette", null, self.show_color_palette, true)) {
                        self.show_color_palette = !self.show_color_palette;
                    }
                    if (c.igMenuItem_Bool("Dear ImGui Demo", null, self.show_imgui_demo_window, true)) {
                        self.show_imgui_demo_window = !self.show_imgui_demo_window;
                    }
                    c.igEndMenu();
                }

                const left_width = c.igGetCursorPosX();
                var right_width: f32 = 0;

                {
                    const io = c.igGetIO();
                    const fps = if (io.*.Framerate < 10000) io.*.Framerate else 0;
                    const allocated_bytes: f64 = @floatFromInt(self.api.get_memory_usages());
                    const allocated_bytes_mb = allocated_bytes / 1024.0 / 1024.0;

                    const text = std.fmt.bufPrintZ(&global_buf, "{d:.1}MiB {d:.0} ", .{ allocated_bytes_mb, fps }) catch unreachable;

                    const window_width = c.igGetWindowWidth();
                    const text_size = ig.calc_text_size(text, false, 0);
                    right_width = text_size.x + c.igGetStyle().*.ItemSpacing.x;
                    c.igSetCursorPosX(window_width - text_size.x);
                    c.igTextUnformatted(text.ptr, null);
                }

                if (self.file_name) |text| {
                    const text_size = ig.calc_text_size(text, false, 0);

                    const window_width = c.igGetWindowWidth();
                    const max_width = window_width - left_width - right_width;
                    const frame_width = @min(text_size.x, max_width);
                    const text_x = left_width + max_width / 2 - frame_width / 2;
                    const text_y = c.igGetCursorPosY() + c.igGetStyle().*.FramePadding.y;
                    const text_max_x = text_x + frame_width;

                    const draw_list = c.igGetWindowDrawList();
                    c.igRenderTextEllipsis(
                        draw_list,
                        .{ .x = text_x, .y = text_y },
                        .{ .x = text_max_x, .y = text_y + text_size.y },
                        text_max_x,
                        text_max_x,
                        text,
                        null,
                        &text_size,
                    );
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

    pub fn set_file_name(self: *Self, file_name: []const u8) void {
        if (self.file_name) |f| {
            self.allocator.free(f);
        }
        self.file_name = self.allocator.dupeZ(u8, file_name) catch unreachable;
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

    pub fn on_load_file_start(self: *Self, file_name: []const u8) void {
        switch (self.state) {
            .welcome => |*welcome| {
                welcome.on_file_load_start(self, file_name);
            },
            .view => |*view| {
                view.on_file_load_start(self, file_name);
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
    const text_size = ig.calc_text_size(text, false, 0);
    const x = bb.Min.x + (bb.Max.x - bb.Min.x) * 0.5 - text_size.x * 0.5;
    const y = bb.Min.y + (bb.Max.y - bb.Min.y) * 0.5 - text_size.y * 0.5;
    c.igSetCursorPosX(x);
    c.igSetCursorPosY(y);
    c.igTextUnformatted(text.ptr, text.ptr + text.len);
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

    pub fn on_file_load_start(self: *WelcomeState, tracing: *Tracing, file_name: []const u8) void {
        const state = .{ .load_file = LoadFileState.init(self.allocator) };
        tracing.set_file_name(file_name);
        tracing.switch_state(state);
    }
};

const LoadFileState = struct {
    allocator: Allocator,
    total: usize,
    offset: usize,
    buf: [256]u8,
    progress_message: ?[:0]u8,
    error_message: ?[:0]u8,

    const popup_id = "LoadFilePopup";

    pub fn init(allocator: Allocator) LoadFileState {
        return .{
            .allocator = allocator,
            .total = 0,
            .offset = 0,
            .buf = undefined,
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
        _ = self;
    }

    pub fn should_load_file(self: *const LoadFileState) bool {
        return self.error_message == null;
    }

    fn set_progress(self: *LoadFileState, comptime fmt: []const u8, args: anytype) void {
        self.progress_message = std.fmt.bufPrintZ(&self.buf, fmt, args) catch unreachable;
    }

    fn setError(self: *LoadFileState, comptime fmt: []const u8, args: anytype) void {
        std.debug.assert(self.error_message == null);
        self.error_message = std.fmt.bufPrintZ(&self.buf, fmt, args) catch unreachable;
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

var is_dockspace_initialized: bool = false;

fn less_than_str(lhs: []const u8, rhs: []const u8) bool {
    const len = @min(lhs.len, rhs.len);
    for (lhs[0..len], rhs[0..len]) |a, b| {
        if (a != b) {
            return a < b;
        }
    }
    return lhs.len < rhs.len;
}

const Histogram = struct {
    const Bucket = struct {
        count: i64,
    };

    buckets: []Bucket,
    max_count: i64,
    duration_us_per_bucket: i64,
    min_duration_us: i64,

    fn build(allocator: Allocator, spans: []const *const Span, bucket_count: usize) Histogram {
        const buckets = allocator.alloc(Bucket, bucket_count) catch unreachable;
        @memset(buckets, Bucket{ .count = 0 });

        var min_duration_us: i64 = std.math.maxInt(i64);
        var max_duration_us: i64 = 0;
        for (spans) |span| {
            min_duration_us = @min(min_duration_us, span.duration_us);
            max_duration_us = @max(max_duration_us, span.duration_us);
        }

        const duration_us = max_duration_us - min_duration_us;
        const duration_us_f32: f32 = @floatFromInt(duration_us);
        const bucket_count_f32: f32 = @floatFromInt(bucket_count - 1);
        const duration_us_per_bucket = @max(1, @as(i64, @intFromFloat(@round(duration_us_f32 / bucket_count_f32))));
        const duration_us_per_bucket_f32: f32 = @floatFromInt(duration_us_per_bucket);
        var max_count: i64 = 0;
        for (spans) |span| {
            const index: usize = blk: {
                if (duration_us == 0) {
                    break :blk bucket_count / 2;
                } else {
                    break :blk @min(
                        @as(usize, @intFromFloat(@as(
                            f32,
                            @floatFromInt(span.duration_us - min_duration_us),
                        ) / duration_us_per_bucket_f32)),
                        bucket_count - 1,
                    );
                }
            };
            buckets[index].count += 1;
            max_count = @max(max_count, buckets[index].count);
        }

        return Histogram{
            .buckets = buckets,
            .max_count = max_count,
            .min_duration_us = min_duration_us,
            .duration_us_per_bucket = duration_us_per_bucket,
        };
    }
};

const Statistics = struct {
    const Self = @This();

    const SortDirection = enum {
        none,
        asc,
        desc,
    };

    const SpanSortCtx = struct {
        sort: SpanSort,
        direction: SortDirection,
    };

    const Group = struct {
        const Sort = enum {
            name,
            total_wall,
            avg_wall,
            occurence,
        };

        name: []const u8,
        total_wall: i64,
        avg_wall: i64,
        spans: std.ArrayList(*const Span),

        const Ctx = struct {
            sort: Sort,
            direction: SortDirection,
        };

        fn sort(ctx: Ctx, lhs: Group, rhs: Group) bool {
            switch (ctx.sort) {
                .name => {
                    switch (ctx.direction) {
                        .none, .desc => {
                            return less_than_str(rhs.name, lhs.name);
                        },
                        .asc => {
                            return less_than_str(lhs.name, rhs.name);
                        },
                    }
                },
                .total_wall => {
                    switch (ctx.direction) {
                        .none, .desc => {
                            return rhs.total_wall < lhs.total_wall;
                        },
                        .asc => {
                            return lhs.total_wall < rhs.total_wall;
                        },
                    }
                },
                .avg_wall => {
                    switch (ctx.direction) {
                        .none, .desc => {
                            return rhs.avg_wall < lhs.avg_wall;
                        },
                        .asc => {
                            return lhs.avg_wall < rhs.avg_wall;
                        },
                    }
                },
                .occurence => {
                    switch (ctx.direction) {
                        .none, .desc => {
                            return rhs.spans.items.len < lhs.spans.items.len;
                        },
                        .asc => {
                            return lhs.spans.items.len < rhs.spans.items.len;
                        },
                    }
                },
            }
        }
    };

    const SpanSort = enum {
        name,
        wall_time,
        start_time,
    };

    const Selection = struct {
        span: *const Span,
        highlight: bool,
    };

    allocator: Allocator,
    buf: [:0]u8,

    arena: *std.heap.ArenaAllocator,

    selected_span: ?Selection = null,

    group_sort: Group.Sort = .total_wall,
    group_sort_direction: SortDirection = .desc,
    groups: []const Group = &.{},

    spans: []const *const Span = &.{},
    span_sort: SpanSort = .wall_time,
    span_sort_direction: SortDirection = .desc,

    fn init(allocator: Allocator) Self {
        const arena = allocator.create(std.heap.ArenaAllocator) catch unreachable;
        arena.* = std.heap.ArenaAllocator.init(allocator);
        const buf = allocator.alloc(u8, 128) catch unreachable;
        buf[0] = 0;
        return Self{
            .allocator = allocator,
            .arena = arena,
            .buf = @ptrCast(buf),
        };
    }

    fn set_search_term(self: *Self, term: []const u8) void {
        const len = @min(term.len, self.buf.len - 1);
        @memcpy(self.buf[0..len], term[0..len]);
        self.buf[len] = 0;
    }

    fn get_search_term(self: *const Self) [:0]const u8 {
        return @ptrCast(self.buf[0..std.mem.indexOfSentinel(u8, 0, self.buf)]);
    }

    fn clear(self: *Self) void {
        self.selected_span = null;
        self.buf[0] = 0;
        self.clear_statistics();
    }

    fn clear_statistics(self: *Self) void {
        _ = self.arena.reset(.retain_capacity);
        self.groups = &.{};
        self.spans = &.{};
    }

    fn sort_group(self: *Self, group_sort: Group.Sort, group_sort_direction: SortDirection) void {
        self.group_sort = group_sort;
        self.group_sort_direction = group_sort_direction;
        std.sort.block(Group, @constCast(self.groups), Group.Ctx{
            .sort = self.group_sort,
            .direction = self.group_sort_direction,
        }, Group.sort);
    }

    fn sort_span(self: *Self, span_sort: SpanSort, span_sort_direction: SortDirection) void {
        self.span_sort = span_sort;
        self.span_sort_direction = span_sort_direction;
        std.sort.block(*const Span, @constCast(self.spans), SpanSortCtx{
            .sort = self.span_sort,
            .direction = self.span_sort_direction,
        }, Self.compare_span);
    }

    fn compare_span(ctx: SpanSortCtx, lhs: *const Span, rhs: *const Span) bool {
        switch (ctx.sort) {
            .name => {
                switch (ctx.direction) {
                    .none, .desc => {
                        return less_than_str(rhs.name, lhs.name);
                    },
                    .asc => {
                        return less_than_str(lhs.name, rhs.name);
                    },
                }
            },
            .wall_time => {
                switch (ctx.direction) {
                    .none, .desc => {
                        return rhs.duration_us < lhs.duration_us;
                    },
                    .asc => {
                        return lhs.duration_us < rhs.duration_us;
                    },
                }
            },
            .start_time => {
                switch (ctx.direction) {
                    .none, .desc => {
                        return rhs.start_time_us < lhs.start_time_us;
                    },
                    .asc => {
                        return lhs.start_time_us < rhs.start_time_us;
                    },
                }
            },
        }
    }

    fn build(self: *Self, profile: *const Profile) void {
        self.clear_statistics();
        const search = self.get_search_term();

        var groups = std.StringHashMap(Group).init(self.allocator);
        var group_array = std.ArrayList(Group).init(self.arena.allocator());
        var span_array = std.ArrayList(*const Span).init(self.arena.allocator());

        for (profile.processes.items) |process| {
            for (process.threads.items) |thread| {
                for (thread.spans.items) |*span| {
                    if (std.mem.indexOf(u8, span.name, search)) |_| {
                        const entry = groups.getOrPut(span.name) catch unreachable;
                        if (!entry.found_existing) {
                            entry.value_ptr.* = Group{
                                .name = span.name,
                                .total_wall = 0,
                                .avg_wall = 0,
                                .spans = std.ArrayList(*const Span).init(self.arena.allocator()),
                            };
                        }
                        entry.value_ptr.spans.append(span) catch unreachable;
                        span_array.append(span) catch unreachable;
                    }
                }
            }
        }

        var group_iter = groups.iterator();
        while (group_iter.next()) |entry| {
            const group = entry.value_ptr;
            for (group.spans.items) |span| {
                group.total_wall += span.duration_us;
            }
            group.avg_wall = @divTrunc(group.total_wall, @as(i64, @intCast(group.spans.items.len)));
            group_array.append(group.*) catch unreachable;
        }
        groups.deinit();

        self.groups = group_array.toOwnedSlice() catch unreachable;
        self.sort_group(.total_wall, .desc);

        self.spans = span_array.toOwnedSlice() catch unreachable;
        self.sort_span(.wall_time, .desc);
    }

    fn deinit(self: *Self) void {
        self.allocator.free(self.buf);
        self.arena.deinit();
        self.allocator.destroy(self.arena);
    }

    fn is_selected(self: *const Self, span: *const Span) bool {
        if (self.selected_span) |selected_span| {
            return selected_span.span == span;
        }
        return false;
    }

    fn get_span_color(self: *const Self, span: *const Span) u32 {
        var use_original_color = true;
        if (self.selected_span) |selected_span| {
            if (selected_span.highlight) {
                use_original_color = selected_span.span == span;
            }
        }
        if (use_original_color) {
            return get_color_for_span(span);
        }
        return get_im_color_u32(rgb(152, 152, 152));
    }
};

const global = struct {
    const min_duration_us: i64 = 100;
};

const Lane = union(enum) {
    counter_header: struct {
        counter: *Counter,
    },
    counter: struct {
        counter: *Counter,
    },
    thread_header: struct {
        thread: *Thread,
    },
    track: struct {
        track: *Track,
    },
};

const ViewState = struct {
    allocator: Allocator,
    profile: *Profile,
    start_time_us: i64,
    end_time_us: i64,
    lanes: std.ArrayList(Lane),
    hovered_counters: std.ArrayList(HoveredCounter),
    highlighted_spans: std.ArrayList(HoveredSpan),

    main_window_class: [*c]c.ImGuiWindowClass,

    is_dragging: bool = false,
    drag_start: ViewPos = undefined,
    hovered_span: ?HoveredSpan = null,
    selected_span: ?HoveredSpan = null,
    scroll_track: ?*const Track = null,
    include_item_index: ?i32 = null,

    open_statistics: bool = false,
    statistics: Statistics,

    pub fn init(allocator: Allocator, profile: *Profile) ViewState {
        const window_class = c.ImGuiWindowClass_ImGuiWindowClass();
        window_class.*.DockNodeFlagsOverrideSet = c.ImGuiDockNodeFlags_NoTabBar;

        const duration_us = profile.max_time_us - profile.min_time_us;
        const padding = @divTrunc(duration_us, 6);

        return .{
            .allocator = allocator,
            .profile = profile,
            .start_time_us = profile.min_time_us - padding,
            .end_time_us = profile.max_time_us + padding,
            .lanes = std.ArrayList(Lane).init(allocator),
            .hovered_counters = std.ArrayList(HoveredCounter).init(allocator),
            .highlighted_spans = std.ArrayList(HoveredSpan).init(allocator),
            .main_window_class = window_class,

            .statistics = Statistics.init(allocator),
        };
    }

    pub fn deinit(self: *ViewState) void {
        self.hovered_counters.deinit();
        c.ImGuiWindowClass_destroy(self.main_window_class);
        self.statistics.deinit();
    }

    fn calc_region(self: *ViewState, bb: c.ImRect) ViewRegion {
        const width_per_us = (bb.Max.x - bb.Min.x) / @as(f32, @floatFromInt((self.end_time_us - self.start_time_us)));
        return .{
            .bb = bb,
            .width_per_us = width_per_us,
        };
    }

    fn calc_style(self: *ViewState) ViewStyle {
        _ = self;
        const character_size = ig.calc_text_size("A", false, 0);
        const text_padding_x: f32 = character_size.x;
        const text_padding_y: f32 = character_size.y / 4.0;
        // it must be rounded in order to scroll to track precisely.
        const sub_lane_height: f32 = @round(2 * text_padding_y + character_size.y);
        return ViewStyle{
            .sub_lane_height = sub_lane_height,
            .character_size = character_size,
            .text_padding = .{ .x = text_padding_x, .y = text_padding_y },
        };
    }

    pub fn update(self: *ViewState, dt: f32, tracing: *Tracing) void {
        _ = dt;
        _ = tracing;
        const style = self.calc_style();

        const viewport = c.igGetMainViewport();
        c.igSetNextWindowPos(.{ .x = viewport.*.WorkPos.x, .y = viewport.*.WorkPos.y }, 0, .{ .x = 0, .y = 0 });
        c.igSetNextWindowSize(.{ .x = viewport.*.WorkSize.x, .y = viewport.*.WorkSize.y }, 0);

        c.igPushStyleVar_Float(c.ImGuiStyleVar_WindowRounding, 0);
        c.igPushStyleVar_Vec2(c.ImGuiStyleVar_WindowPadding, .{ .x = 0, .y = 0 });
        _ = c.igBegin("MainWindow", null, container_window_flags | c.ImGuiWindowFlags_NoDocking);

        const dockspace_id = c.igGetID_Str("MainWindowDockspace");
        _ = c.igDockSpace(dockspace_id, .{ .x = 0, .y = 0 }, c.ImGuiDockNodeFlags_PassthruCentralNode | c.ImGuiDockNodeFlags_NoDockingOverCentralNode, 0);

        if (!is_dockspace_initialized) {
            std.log.info("Building dockspace ...", .{});

            var work_area_id: c.ImGuiID = undefined;
            var tool_area_id: c.ImGuiID = undefined;
            _ = c.igDockBuilderSplitNode(dockspace_id, c.ImGuiDir_Right, 0.3, &tool_area_id, &work_area_id);
            c.igDockBuilderDockWindow("WorkArea", work_area_id);

            c.igDockBuilderDockWindow("Statistics", tool_area_id);

            c.igDockBuilderFinish(dockspace_id);
            is_dockspace_initialized = true;
        }

        c.igSetNextWindowClass(self.main_window_class);
        _ = c.igBegin("WorkArea", null, container_window_flags);
        c.igPopStyleVar(2);

        const timeline_height = style.sub_lane_height;

        self.drawTimeline(timeline_height, style);
        self.draw_main_view(timeline_height, style);

        if (self.open_statistics) {
            if (c.igBegin("Statistics", &self.open_statistics, 0)) {}
            self.draw_statistics();
            c.igEnd();
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
        var region = self.calc_region(window_bb);

        self.handleDrag(region);
        self.handleScroll(region);

        // Recalculate region after zoom
        region = self.calc_region(window_bb);

        const draw_list = c.igGetWindowDrawList();
        c.ImDrawList_PushClipRect(
            draw_list,
            region.min(),
            region.max(),
            true,
        );

        self.collect_lanes();
        self.draw_lanes(region, style);
        self.handle_hovered_counters();
        self.handle_spans();

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
        const region = self.calc_region(timeline_bb);
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
                        if (duration_us > global.min_duration_us) {
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

    fn collect_lanes(self: *ViewState) void {
        const trace = tracy.trace(@src());
        defer trace.end();

        self.lanes.clearRetainingCapacity();
        for (self.profile.processes.items) |*process| {
            for (process.counters.items) |*counter| {
                self.lanes.append(.{ .counter_header = .{ .counter = counter } }) catch unreachable;
                if (counter.ui.open) {
                    self.lanes.append(.{ .counter = .{ .counter = counter } }) catch unreachable;
                }
            }
            for (process.threads.items) |*thread| {
                if (thread.tracks.items.len == 0) {
                    continue;
                }

                self.lanes.append(.{ .thread_header = .{ .thread = thread } }) catch unreachable;
                if (thread.ui.open) {
                    for (thread.tracks.items) |*track| {
                        track.lane_index = @intCast(self.lanes.items.len);
                        self.lanes.append(.{ .track = .{ .track = track } }) catch unreachable;
                    }
                }
            }
        }
    }

    fn draw_lanes(self: *ViewState, region: ViewRegion, style: ViewStyle) void {
        const trace = tracy.trace(@src());
        defer trace.end();

        c.igPushStyleVar_Vec2(c.ImGuiStyleVar_ItemSpacing, .{ .x = 0, .y = 0 });
        defer c.igPopStyleVar(1);

        const allow_hover = c.igIsWindowHovered(0) and !self.is_dragging;
        const io = c.igGetIO();
        const mouse_pos = io.*.MousePos;
        const draw_list = c.igGetWindowDrawList();

        const last_hovered_span_name_ptr = if (self.hovered_span) |last| last.span.name.ptr else null;
        self.hovered_span = null;
        self.selected_span = null;

        const lane_height = style.sub_lane_height;

        if (self.include_item_index) |index| {
            const scroll_y = @as(f32, @floatFromInt(index)) * lane_height - region.height() / 2;
            c.igSetScrollY_Float(scroll_y);
        }
        self.include_item_index = null;

        const clipper = c.ImGuiListClipper_ImGuiListClipper();
        defer c.ImGuiListClipper_destroy(clipper);
        c.ImGuiListClipper_Begin(clipper, @intCast(self.lanes.items.len), 0);
        while (c.ImGuiListClipper_Step(clipper)) {
            for (@intCast(clipper.*.DisplayStart)..@intCast(clipper.*.DisplayEnd)) |index| {
                // const cy = ig.get_cursor_pos().y;
                const lane_top = ig.get_cursor_screen_pos().y;
                const lane_bottom = lane_top + lane_height;
                const lane_left = region.left();
                const lane_bb = c.ImRect{
                    .Min = .{ .x = region.left(), .y = lane_top },
                    .Max = .{ .x = region.right(), .y = lane_bottom },
                };
                switch (self.lanes.items[index]) {
                    .counter_header => |lane| {
                        const counter = lane.counter;
                        var hovered: bool = false;
                        draw_lane_header(
                            lane_bb,
                            counter.name,
                            style.character_size.y,
                            style.text_padding.x,
                            allow_hover,
                            &counter.ui.open,
                            &hovered,
                        );
                        if (hovered and counter.ui.open) {
                            if (c.igBeginTooltip()) {
                                c.igTextUnformatted(counter.name, null);
                            }
                            c.igEndTooltip();
                        }
                    },
                    .counter => |lane| {
                        const counter = lane.counter;

                        const min_width = 3;
                        const min_duration_us: i64 = @intFromFloat(@ceil(min_width / region.width_per_us));

                        c.igItemSize_Rect(lane_bb, -1);
                        _ = c.igItemAdd(lane_bb, 0, null, 0);

                        const color_index_base: usize = @truncate(hash_str(counter.name));
                        for (counter.series.items, 0..) |series, series_index| {
                            const col_v4 = general_purpose_colors[(color_index_base + series_index) % general_purpose_colors.len];
                            const col = get_im_color_u32(col_v4);

                            var prev_pos: ?c.ImVec2 = null;
                            var prev_value: ?*const SeriesValue = null;
                            var hovered_counter: ?HoveredCounter = null;
                            for (series.get_values(self.start_time_us, self.end_time_us, min_duration_us)) |*value| {
                                const pos = c.ImVec2{
                                    .x = lane_left + @as(f32, @floatFromInt(value.time_us - self.start_time_us)) * region.width_per_us,
                                    .y = lane_bottom - @as(f32, @floatCast((value.value / counter.max_value))) * lane_height,
                                };

                                if (prev_pos) |pp| {
                                    const bb = c.ImRect{
                                        .Min = .{ .x = pp.x, .y = lane_top },
                                        .Max = .{ .x = pos.x, .y = lane_bottom },
                                    };
                                    if (allow_hover and c.ImRect_Contains_Vec2(@constCast(&bb), mouse_pos)) {
                                        hovered_counter = .{
                                            .name = series.name,
                                            .value = prev_value.?,
                                            .pos = pp,
                                        };
                                    }

                                    c.ImDrawList_AddRectFilled(
                                        draw_list,
                                        .{ .x = pp.x, .y = lane_bottom },
                                        .{ .x = pos.x, .y = pp.y },
                                        col,
                                        0,
                                        0,
                                    );

                                    c.ImDrawList_PathLineTo(draw_list, .{ .x = pp.x, .y = pp.y });
                                    c.ImDrawList_PathLineTo(draw_list, .{ .x = pos.x, .y = pp.y });
                                }

                                if (value.time_us > self.end_time_us) {
                                    break;
                                }

                                prev_pos = pos;
                                prev_value = value;
                            }

                            c.ImDrawList_PathStroke(draw_list, get_im_color_u32(.{ .x = col_v4.x * 0.5, .y = col_v4.y * 0.5, .z = col_v4.z * 0.5, .w = 1.0 }), 0, 1);
                            c.ImDrawList_PathClear(draw_list);

                            if (hovered_counter == null) {
                                // Handle hover for last point
                                if (prev_pos) |pp| {
                                    const bb = c.ImRect{
                                        .Min = .{ .x = pp.x, .y = lane_top },
                                        .Max = .{ .x = region.right(), .y = lane_bottom },
                                    };
                                    if (allow_hover and c.ImRect_Contains_Vec2(@constCast(&bb), mouse_pos)) {
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
                    },
                    .thread_header => |lane| {
                        const thread = lane.thread;

                        const name = blk: {
                            if (thread.name) |name| {
                                break :blk name;
                            } else {
                                break :blk std.fmt.bufPrintZ(&global_buf, "Thread {}", .{thread.tid}) catch unreachable;
                            }
                        };

                        var hovered: bool = false;
                        draw_lane_header(lane_bb, name, style.character_size.y, style.text_padding.x, allow_hover, &thread.ui.open, &hovered);
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
                    },
                    .track => |lane| {
                        const track = lane.track;

                        const min_width = 6;
                        const min_duration_us: i64 = @intFromFloat(@ceil(min_width / region.width_per_us));

                        c.igItemSize_Rect(lane_bb, -1);
                        if (c.igItemAdd(lane_bb, 0, null, 0)) {
                            for (track.get_spans(self.start_time_us, self.end_time_us, min_duration_us)) |span| {
                                var x1: f32 = region.left() + @as(f32, @floatFromInt(span.start_time_us - self.start_time_us)) * region.width_per_us;
                                var x2: f32 = x1 + @as(f32, @floatFromInt(@max(span.duration_us, min_duration_us))) * region.width_per_us;
                                x1 = @max(region.left(), x1);
                                x2 = @min(region.right(), x2);

                                {
                                    const col = self.statistics.get_span_color(span);
                                    const bb = c.ImRect{
                                        .Min = .{ .x = x1, .y = lane_top },
                                        .Max = .{ .x = x2, .y = lane_top + lane_height },
                                    };
                                    c.ImDrawList_AddRectFilled(
                                        draw_list,
                                        bb.Min,
                                        bb.Max,
                                        col,
                                        0,
                                        0,
                                    );

                                    if (last_hovered_span_name_ptr == span.name.ptr) {
                                        self.highlighted_spans.append(.{
                                            .span = span,
                                            .bb = bb,
                                        }) catch unreachable;
                                    } else {
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

                                    if (allow_hover and c.ImRect_Contains_Vec2(@constCast(&bb), mouse_pos)) {
                                        self.hovered_span = .{
                                            .span = span,
                                            .bb = bb,
                                        };
                                    }

                                    if (self.statistics.is_selected(span)) {
                                        self.selected_span = .{
                                            .span = span,
                                            .bb = bb,
                                        };
                                    }
                                }

                                if (x2 - x1 > 2 * style.text_padding.x + style.character_size.x) {
                                    const text_min_x = x1 + style.text_padding.x;
                                    const text_max_x = x2 - style.text_padding.x;

                                    const text = span.name;
                                    const text_size = ig.calc_text_size(text, false, 0);
                                    const center_y = lane_top + lane_height / 2.0;

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
                                            .{ .x = text_max_x, .y = lane_top + lane_height },
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
                    },
                }
            }
        }
    }

    fn handle_hovered_counters(self: *ViewState) void {
        const draw_list = c.igGetWindowDrawList();

        var max_hovered_time: i64 = 0;
        for (self.hovered_counters.items) |hovered| {
            max_hovered_time = @max(max_hovered_time, hovered.value.time_us);
        }

        if (self.hovered_counters.items.len > 0) {
            if (c.igBeginTooltip()) {
                c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "Time: {}", .{Timestamp{ .us = max_hovered_time }}) catch unreachable, null);

                for (self.hovered_counters.items) |hovered| {
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

    fn handle_spans(self: *ViewState) void {
        const io = c.igGetIO();
        const draw_list = c.igGetWindowDrawList();

        for (self.highlighted_spans.items) |hovered| {
            c.ImDrawList_AddRect(
                draw_list,
                hovered.bb.Min,
                hovered.bb.Max,
                get_im_color_u32(.{ .x = 0, .y = 0, .z = 0, .w = 1 }),
                0,
                0,
                2,
            );
        }
        self.highlighted_spans.clearRetainingCapacity();

        if (self.hovered_span) |*hovered| {
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
                self.draw_span_tooltip(span);
            }
            c.igEndTooltip();

            if (io.*.MouseClickedCount[0] == 1) {
                self.select_span(span);
            }
        }

        if (self.selected_span) |selected| {
            c.ImDrawList_AddRect(
                draw_list,
                selected.bb.Min,
                selected.bb.Max,
                get_im_color_u32(.{ .x = 0, .y = 0.7, .z = 0, .w = 1 }),
                0,
                0,
                4,
            );
        }
    }

    fn draw_span_tooltip(self: *ViewState, span: *const Span) void {
        _ = self;
        c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "Title: {s}", .{span.name}) catch unreachable, null);
        if (span.category) |cat| {
            c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "Category: {s}", .{cat}) catch unreachable, null);
        }
        c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "Start: {}", .{Timestamp{ .us = span.start_time_us }}) catch unreachable, null);
        c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "Duration: {}", .{Timestamp{ .us = span.duration_us }}) catch unreachable, null);
        c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "Self: {}", .{Timestamp{ .us = span.self_duration_us }}) catch unreachable, null);
    }

    fn select_span(self: *ViewState, span: *const Span) void {
        self.statistics.selected_span = .{ .span = span, .highlight = false };
        self.build_statistics(span.name);
    }

    fn zoom_into_span(self: *ViewState, span: *const Span) void {
        if (span.duration_us > global.min_duration_us) {
            self.start_time_us = span.start_time_us;
            self.end_time_us = self.start_time_us + span.duration_us;
        } else {
            self.start_time_us = span.start_time_us - global.min_duration_us / 2;
            self.end_time_us = span.start_time_us + global.min_duration_us / 2;
        }
        if (!span.thread.process.ui.open) {
            span.thread.process.ui.open = true;
        }
        if (!span.thread.ui.open) {
            span.thread.ui.open = true;
        }
        self.include_item_index = span.track.lane_index;
    }

    fn build_statistics(self: *ViewState, search: []const u8) void {
        const trace = tracy.trace(@src());
        defer trace.end();

        self.statistics.set_search_term(search);
        self.statistics.build(self.profile);
        self.open_statistics = true;
    }

    const group_table_column_dict = &[_]Statistics.Group.Sort{
        .name,
        .total_wall,
        .avg_wall,
        .occurence,
    };

    const span_table_column_dict = &[_]Statistics.SpanSort{
        .name,
        .wall_time,
        .start_time,
    };

    const direction_dict = &[_]Statistics.SortDirection{
        .desc,
        .asc,
        .desc,
    };

    fn draw_statistics(self: *ViewState) void {
        const trace = tracy.trace(@src());
        defer trace.end();

        const search_submitted = c.igInputText(
            "##Search",
            self.statistics.buf.ptr,
            self.statistics.buf.len,
            c.ImGuiInputTextFlags_EnterReturnsTrue,
            null,
            null,
        );
        if (c.igButton("Find", .{ .x = 0, .y = 0 }) or search_submitted) {
            self.statistics.build(self.profile);
        }
        c.igSameLine(0, -1);
        if (c.igButton("Clear", .{ .x = 0, .y = 0 })) {
            self.statistics.clear();
        }

        if (c.igTreeNodeEx_Str("Selection", c.ImGuiTreeNodeFlags_DefaultOpen)) {
            if (self.statistics.selected_span) |selected_span| {
                const span = selected_span.span;
                c.igTextUnformatted("Title: ", null);
                c.igSameLine(0, 0);
                c.igTextWrapped("%s", span.name.ptr);

                if (span.category) |cat| {
                    c.igTextUnformatted("Category: ", null);
                    c.igSameLine(0, 0);
                    c.igTextUnformatted(cat, null);
                }

                c.igTextUnformatted("Start: ", null);
                c.igSameLine(0, 0);
                c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "{}", .{Timestamp{ .us = span.start_time_us }}) catch unreachable, null);

                c.igTextUnformatted("Duration: ", null);
                c.igSameLine(0, 0);
                c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "{}", .{Timestamp{ .us = span.duration_us }}) catch unreachable, null);

                c.igTextUnformatted("Self: ", null);
                c.igSameLine(0, 0);
                c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "{}", .{Timestamp{ .us = span.self_duration_us }}) catch unreachable, null);

                if (span.args.count() > 0) {
                    c.igTextUnformatted("Args: ", null);
                    var iter = span.args.iterator();
                    while (iter.next()) |arg| {
                        c.igText("    %s: %s", arg.key_ptr.*.ptr, arg.value_ptr.*.ptr);
                    }
                }
            }
            c.igTreePop();
        }

        if (c.igTreeNodeEx_Str("Group", c.ImGuiTreeNodeFlags_DefaultOpen)) {
            if (self.statistics.groups.len > 0 and c.igBeginTable(
                "##Group",
                4,
                c.ImGuiTableFlags_Resizable |
                    c.ImGuiTableFlags_Sortable |
                    c.ImGuiTableFlags_ScrollY |
                    c.ImGuiTableFlags_SizingFixedFit,
                .{ .x = 0, .y = c.igGetTextLineHeightWithSpacing() * 9 },
                0,
            )) {
                c.igTableSetupScrollFreeze(0, 1);
                c.igTableSetupColumn("Name", c.ImGuiTableColumnFlags_WidthStretch, 0, 0);
                c.igTableSetupColumn("Wall Duration", c.ImGuiTableColumnFlags_DefaultSort |
                    c.ImGuiTableColumnFlags_PreferSortDescending, 0, 0);
                c.igTableSetupColumn("Average Wall Duration", 0, 0, 0);
                c.igTableSetupColumn("Occurences", 0, 0, 0);
                c.igTableHeadersRow();

                if (c.igTableGetSortSpecs()) |specs| {
                    const column_index = c.igTableColumnGetSortColumnIndex(specs);
                    const sort_direction = c.igTableColumnGetSortDirection(specs);
                    const sort = group_table_column_dict[@intCast(column_index)];
                    const direction = direction_dict[@intCast(sort_direction)];
                    if (specs.*.SpecsDirty or
                        sort != self.statistics.group_sort or
                        direction != self.statistics.group_sort_direction)
                    {
                        self.statistics.sort_group(sort, direction);
                        specs.*.SpecsDirty = false;
                    }
                }

                const clipper = c.ImGuiListClipper_ImGuiListClipper();
                defer c.ImGuiListClipper_destroy(clipper);
                c.ImGuiListClipper_Begin(clipper, @intCast(self.statistics.groups.len), 0);
                while (c.ImGuiListClipper_Step(clipper)) {
                    for (@intCast(clipper.*.DisplayStart)..@intCast(clipper.*.DisplayEnd)) |row| {
                        const group = self.statistics.groups[row];

                        c.igTableNextRow(0, 0);

                        _ = c.igTableNextColumn();
                        _ = c.igSelectable_Bool(
                            "##Select",
                            false,
                            c.ImGuiSelectableFlags_SpanAllColumns |
                                c.ImGuiSelectableFlags_AllowOverlap,
                            .{},
                        );
                        c.igSameLine(0, 0);
                        c.igTextUnformatted(group.name.ptr, null);

                        _ = c.igTableNextColumn();
                        c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "{}", .{Timestamp{ .us = group.total_wall }}) catch unreachable, null);
                        _ = c.igTableNextColumn();
                        c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "{}", .{Timestamp{ .us = group.avg_wall }}) catch unreachable, null);
                        _ = c.igTableNextColumn();
                        c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "{}", .{group.spans.items.len}) catch unreachable, null);
                    }
                }

                c.igEndTable();
            }
            c.igTreePop();
        }

        if (c.igTreeNodeEx_Str("Histogram", c.ImGuiTreeNodeFlags_DefaultOpen)) {
            if (self.statistics.spans.len > 0) {
                const bb_min = ig.get_cursor_screen_pos();

                const size = c.ImVec2{ .x = c.igGetWindowWidth() - 2 * (bb_min.x - ig.getWindowPos().x), .y = 200 };
                const bb = c.ImRect{
                    .Min = bb_min,
                    .Max = .{ .x = bb_min.x + size.x, .y = bb_min.y + size.y },
                };
                c.igItemSize_Rect(bb, -1);
                if (c.igItemAdd(bb, 0, null, 0)) {
                    var arena = std.heap.ArenaAllocator.init(self.allocator);
                    defer arena.deinit();

                    const draw_list = c.igGetWindowDrawList();
                    c.ImDrawList_AddRectFilled(
                        draw_list,
                        bb.Min,
                        bb.Max,
                        get_im_color_u32(rgb(128, 128, 128)),
                        0,
                        0,
                    );
                    c.ImDrawList_AddRect(
                        draw_list,
                        .{ .x = bb.Min.x - 1, .y = bb.Min.y - 1 },
                        .{ .x = bb.Max.x + 1, .y = bb.Max.y + 1 },
                        get_im_color_u32(rgb(0, 0, 0)),
                        0,
                        0,
                        1.0,
                    );

                    const bucket_width: f32 = 4;
                    if (size.x > bucket_width * 2) {
                        const bucket_count: usize = @intFromFloat(size.x / bucket_width);
                        const histogram = Histogram.build(arena.allocator(), self.statistics.spans, bucket_count);

                        var x = bb.Min.x;
                        const height = bb.Max.y - bb.Min.y;
                        const top = bb.Min.y;
                        const bottom = bb.Min.y + height;

                        const io = c.igGetIO();
                        const mouse_pos = io.*.MousePos;
                        for (histogram.buckets, 0..) |bucket, index| {
                            if (bucket.count > 0) {
                                const bucket_height = @max(@as(f32, @floatFromInt(bucket.count)) / @as(f32, @floatFromInt(histogram.max_count)) * height, 2);
                                const bucket_bb = c.ImRect{
                                    .Min = .{ .x = x, .y = bottom - bucket_height },
                                    .Max = .{ .x = x + bucket_width, .y = bottom },
                                };
                                c.ImDrawList_AddRectFilled(
                                    draw_list,
                                    bucket_bb.Min,
                                    bucket_bb.Max,
                                    get_im_color_u32(general_purpose_colors[4]),
                                    0,
                                    0,
                                );

                                const hover_bb = c.ImRect{
                                    .Min = .{ .x = bucket_bb.Min.x, .y = top },
                                    .Max = .{ .x = bucket_bb.Max.x, .y = bottom },
                                };
                                if (c.ImRect_Contains_Vec2(@constCast(&hover_bb), mouse_pos)) {
                                    if (c.igBeginTooltip()) {
                                        c.igTextUnformatted(
                                            std.fmt.bufPrintZ(
                                                &global_buf,
                                                "Time range: {} - {}",
                                                .{
                                                    Timestamp{ .us = histogram.min_duration_us + @as(i64, @intCast(index)) * histogram.duration_us_per_bucket },
                                                    Timestamp{ .us = histogram.min_duration_us + @as(i64, @intCast(index + 1)) * histogram.duration_us_per_bucket },
                                                },
                                            ) catch unreachable,
                                            null,
                                        );
                                        c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "Count: {}", .{bucket.count}) catch unreachable, null);
                                    }
                                    c.igEndTooltip();
                                }
                            }

                            x += bucket_width;
                        }

                        if (c.ImRect_Contains_Vec2(@constCast(&bb), mouse_pos)) {
                            c.ImDrawList_AddRectFilled(
                                draw_list,
                                .{ .x = mouse_pos.x - 0.5, .y = top },
                                .{ .x = mouse_pos.x + 0.5, .y = bottom },
                                get_im_color_u32(.{ .x = 1.0, .y = 1.0, .z = 1.0, .w = 0.5 }),
                                0,
                                0,
                            );
                        }
                    }
                }
            }

            c.igTreePop();
        }

        if (c.igTreeNodeEx_Str("Samples", c.ImGuiTreeNodeFlags_DefaultOpen)) {
            if (self.statistics.spans.len > 0 and c.igBeginTable(
                "##Samples",
                3,
                c.ImGuiTableFlags_Resizable |
                    c.ImGuiTableFlags_Sortable |
                    c.ImGuiTableFlags_ScrollY |
                    c.ImGuiTableFlags_SizingFixedFit,
                .{ .x = 0, .y = c.igGetTextLineHeightWithSpacing() * 9 },
                0,
            )) {
                c.igTableSetupScrollFreeze(0, 1);
                c.igTableSetupColumn("Name", c.ImGuiTableColumnFlags_WidthStretch, 0, 0);
                c.igTableSetupColumn("Wall time", c.ImGuiTableColumnFlags_DefaultSort |
                    c.ImGuiTableColumnFlags_PreferSortDescending, 0, 0);
                c.igTableSetupColumn("Start time", 0, 0, 0);
                c.igTableHeadersRow();

                if (c.igTableGetSortSpecs()) |specs| {
                    const column_index = c.igTableColumnGetSortColumnIndex(specs);
                    const sort_direction = c.igTableColumnGetSortDirection(specs);
                    const sort = span_table_column_dict[@intCast(column_index)];
                    const direction = direction_dict[@intCast(sort_direction)];
                    if (specs.*.SpecsDirty or
                        sort != self.statistics.span_sort or
                        direction != self.statistics.span_sort_direction)
                    {
                        self.statistics.sort_span(sort, direction);
                        specs.*.SpecsDirty = false;
                    }
                }

                const clipper = c.ImGuiListClipper_ImGuiListClipper();
                defer c.ImGuiListClipper_destroy(clipper);
                c.ImGuiListClipper_Begin(clipper, @intCast(self.statistics.spans.len), 0);
                while (c.ImGuiListClipper_Step(clipper)) {
                    for (@intCast(clipper.*.DisplayStart)..@intCast(clipper.*.DisplayEnd)) |row| {
                        const span = self.statistics.spans[row];

                        c.igTableNextRow(0, 0);

                        _ = c.igTableNextColumn();
                        if (c.igSelectable_Bool(
                            std.fmt.bufPrintZ(&global_buf, "##Select{*}", .{span}) catch unreachable,
                            self.statistics.is_selected(span),
                            c.ImGuiSelectableFlags_SpanAllColumns |
                                c.ImGuiSelectableFlags_AllowOverlap,
                            .{},
                        )) {
                            self.statistics.selected_span = .{ .span = span, .highlight = true };
                            self.zoom_into_span(span);
                        }
                        c.igSameLine(0, 0);
                        c.igTextUnformatted(span.name.ptr, null);

                        _ = c.igTableNextColumn();
                        c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "{}", .{Timestamp{ .us = span.duration_us }}) catch unreachable, null);
                        _ = c.igTableNextColumn();
                        c.igTextUnformatted(std.fmt.bufPrintZ(&global_buf, "{}", .{Timestamp{ .us = span.start_time_us }}) catch unreachable, null);
                    }
                }

                c.igEndTable();
            }
            c.igTreePop();
        }
    }

    pub fn on_file_load_start(self: *ViewState, tracing: *Tracing, file_name: []const u8) void {
        const state = .{ .load_file = LoadFileState.init(self.allocator) };
        tracing.set_file_name(file_name);
        tracing.switch_state(state);
    }
};

const HoveredCounter = struct {
    name: [:0]const u8,
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

fn get_color_for_span(span: *const Span) u32 {
    const color_index: usize = @truncate(hash_str(span.name));
    return get_im_color_u32(general_purpose_colors[color_index % general_purpose_colors.len]);
}

fn draw_lane_header(
    lane_bb: c.ImRect,
    title: [:0]const u8,
    character_size_y: f32,
    text_padding_x: f32,
    allow_hover: bool,
    open: *bool,
    hovered: *bool,
) void {
    const io = c.igGetIO();

    hovered.* = false;

    const mouse_pos = c.igGetIO().*.MousePos;
    const draw_list = c.igGetWindowDrawList();
    c.igItemSize_Rect(lane_bb, -1);
    if (!c.igItemAdd(lane_bb, 0, null, 0)) {
        return;
    }

    const text_size = ig.calc_text_size(title, false, 0);
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
    const header_bb = c.ImRect{
        .Min = .{ .x = lane_bb.Min.x, .y = lane_bb.Min.y },
        .Max = .{ .x = header_right, .y = lane_bb.Max.y },
    };

    if (allow_hover and c.ImRect_Contains_Vec2(@constCast(&header_bb), mouse_pos)) {
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

    if (hovered.* and
        io.*.MouseReleased[0] and
        c.ImRect_Contains_Vec2(@constCast(&header_bb), io.*.MouseClickedPos[0]))
    {
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
