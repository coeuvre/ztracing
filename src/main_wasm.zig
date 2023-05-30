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

fn imguiFree(maybePtr: ?*anyopaque, user_data: ?*anyopaque) callconv(.C) void {
    _ = user_data;
    if (maybePtr) |ptr| {
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

const App = struct {
    allocator: Allocator,
    width: f32,
    height: f32,
    io: *c.ImGuiIO,

    pub fn init(self: *App, allocator: Allocator, width: f32, height: f32) void {
        self.allocator = allocator;
        self.width = width;
        self.height = height;

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

        c.igShowDemoWindow(null);

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

                glDrawElements(GL_TRIANGLES, cmd.*.ElemCount, GL_UNSIGNED_SHORT, cmd.*.IdxOffset * @sizeOf(c.ImDrawIdx));
            }
        }
    }
};

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
