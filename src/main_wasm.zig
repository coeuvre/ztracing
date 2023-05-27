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

    pub extern "js" fn destory(ref: i32) void;

    // canvas
    pub extern "js" fn clearRect(x: f32, y: f32, w: f32, h: f32) void;

    pub extern "js" fn setFillStyleColor(color_ptr: [*]const u8, color_len: usize) void;

    pub extern "js" fn createLinearGradient(x0: f32, y0: f32, x1: f32, y1: f32) i32;

    pub extern "js" fn addGradientColorStop(ref: i32, offset: f32, color_ptr: [*]const u8, color_len: usize) void;

    pub extern "js" fn setFillStyleGradient(ref: i32) void;

    pub extern "js" fn fillRect(x: f32, y: f32, w: f32, h: f32) void;
};

const canvas = struct {
    pub const clearRect = js.clearRect;

    pub fn setFillStyleColor(color: []const u8) void {
        js.setFillStyleColor(color.ptr, color.len);
    }

    pub const createLinearGradient = js.createLinearGradient;

    pub fn addGradientColorStop(ref: i32, offset: f32, color: []const u8) void {
        js.addGradientColorStop(ref, offset, color.ptr, color.len);
    }

    pub const setFillStyleGradient = js.setFillStyleGradient;

    pub const fillRect = js.fillRect;
};

fn drawControl(width: f32) void {
    const height = 26;
    canvas.setFillStyleColor("#e6e6e6");
    canvas.fillRect(0, 0, width, height);

    const gradient = canvas.createLinearGradient(0, 0, 0, height);
    defer js.destory(gradient);
    canvas.addGradientColorStop(gradient, 0, "#e5e5e5");
    canvas.addGradientColorStop(gradient, 1, "#d1d1d1");
    canvas.setFillStyleGradient(gradient);
    canvas.fillRect(0, 0, width, height);
}

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

        drawControl(self.width);
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
