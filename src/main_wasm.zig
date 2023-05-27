const std = @import("std");
const log = std.log;

pub const std_options = struct {
    pub fn logFn(
        comptime message_level: std.log.Level,
        comptime scope: @Type(.EnumLiteral),
        comptime format: []const u8,
        args: anytype,
    ) void {
        const level = @enumToInt(message_level);
        const prefix2 = if (scope == .default) "" else "[" ++ @tagName(scope) ++ "] ";
        var buf = [_]u8{0} ** 256;
        const msg = std.fmt.bufPrint(&buf, prefix2 ++ format ++ "\n", args) catch return;
        js.log(level, msg.ptr, msg.len);
    }
};

const js = struct {
    pub extern "js" fn log(level: u32, ptr: [*]const u8, len: usize) void;

    // canvas
    pub extern "js" fn clearRect(x: f32, y: f32, w: f32, h: f32) void;
    pub extern "js" fn setFillStyleColor(color_ptr: [*]const u8, color_len: usize) void;
    pub extern "js" fn fillRect(x: f32, y: f32, w: f32, h: f32) void;
};

const canvas = struct {
    pub const clearRect = js.clearRect;

    pub fn setFillStyleColor(color: []const u8) void {
        js.setFillStyleColor(color.ptr, color.len);
    }

    pub const fillRect = js.fillRect;
};

const App = struct {
    width: f32,
    height: f32,

    pub fn init(width: f32, height: f32) App {
        return App{
            .width = width,
            .height = height,
        };
    }

    pub fn update(self: *App) void {
        canvas.clearRect(0, 0, self.width, self.height);
        canvas.setFillStyleColor("#FF0000");
        canvas.fillRect(0, 0, 100, 100);
    }

    pub fn onResize(self: *App, width: f32, height: f32) void {
        self.width = width;
        self.height = height;
    }
};

var app: *App = undefined;

export fn init(width: f32, height: f32) void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    var allocator = gpa.allocator();
    app = allocator.create(App) catch unreachable;
    app.* = App.init(width, height);
}

export fn update() void {
    app.update();
}

export fn onResize(width: f32, height: f32) void {
    app.onResize(width, height);
}
