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

    // canvas
    pub extern "js" fn clearRect(x: f32, y: f32, w: f32, h: f32) void;

    pub extern "js" fn setFillStyleColor(color_ptr: [*]const u8, color_len: usize) void;

    pub extern "js" fn createLinearGradient(x0: f32, y0: f32, x1: f32, y1: f32) JsObject;

    pub extern "js" fn addGradientColorStop(obj: JsObject, offset: f32, color_ptr: [*]const u8, color_len: usize) void;

    pub extern "js" fn setFillStyleGradient(obj: JsObject) void;

    pub extern "js" fn fillRect(x: f32, y: f32, w: f32, h: f32) void;

    pub extern "js" fn setLineWidth(width: f32) void;

    pub extern "js" fn setStrokeStyleColor(color_ptr: [*]const u8, color_len: usize) void;

    pub extern "js" fn strokeRect(x: f32, y: f32, w: f32, h: f32) void;

    pub extern "js" fn beginPath() void;

    pub extern "js" fn roundedRect(x: f32, y: f32, w: f32, h: f32, r: f32) void;

    pub extern "js" fn stroke() void;

    pub extern "js" fn fillText(text_ptr: [*]const u8, text_len: usize, x: f32, y: f32) void;
};

const canvas = struct {
    pub const clearRect = js.clearRect;

    pub fn setFillStyleColor(color: []const u8) void {
        js.setFillStyleColor(color.ptr, color.len);
    }

    pub const createLinearGradient = js.createLinearGradient;

    pub fn addGradientColorStop(obj: js.JsObject, offset: f32, color: []const u8) void {
        js.addGradientColorStop(obj, offset, color.ptr, color.len);
    }

    pub const setFillStyleGradient = js.setFillStyleGradient;

    pub const fillRect = js.fillRect;

    pub const setLineWidth = js.setLineWidth;

    pub fn setStrokeStyleColor(color: []const u8) void {
        js.setStrokeStyleColor(color.ptr, color.len);
    }

    pub const strokeRect = js.strokeRect;

    pub const beginPath = js.beginPath;

    pub const roundedRect = js.roundedRect;

    pub const stroke = js.stroke;

    pub fn fillText(text: []const u8, x: f32, y: f32) void {
        js.fillText(text.ptr, text.len, x, y);
    }
};

fn drawControl(width: f32) void {
    const height = 26;

    const gradient = canvas.createLinearGradient(0, 0, 0, height);
    defer js.destory(gradient);
    canvas.addGradientColorStop(gradient, 0, "#e5e5e5");
    canvas.addGradientColorStop(gradient, 1, "#d1d1d1");
    canvas.setFillStyleGradient(gradient);
    canvas.fillRect(0, 0, width, height);

    drawButton(1, 2, 58.98, 21, 1, "#808080", "rgb(240, 240, 240)");

    canvas.setFillStyleColor("#000000");
    canvas.fillText("Load", 1, 12);
}

fn drawButton(x: f32, y: f32, w: f32, h: f32, border_width: f32, border_color: []const u8, background_color: []const u8) void {
    const half_border_width = border_width / 2;
    const double_border_width = border_width * 2;

    canvas.setStrokeStyleColor(border_color);
    canvas.setLineWidth(border_width);
    canvas.strokeRect(x + half_border_width, y + half_border_width, w - border_width, h - border_width);

    canvas.setFillStyleColor(background_color);
    canvas.fillRect(x + border_width, y + border_width, w - double_border_width, h - double_border_width);
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
