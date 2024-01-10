const std = @import("std");
const c = @import("c.zig");

pub fn main() void {
    std.log.debug("{s}", .{c.igGetVersion()});
    _ = c.SDL_ShowMessageBox(&.{}, null);
}
