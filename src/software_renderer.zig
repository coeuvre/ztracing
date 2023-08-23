const std = @import("std");
const c = @import("c.zig");

const Allocator = std.mem.Allocator;

pub const SoftwareRenderer = struct {
    pub fn init(allocator: Allocator, width: f32, height: f32) SoftwareRenderer {
        _ = height;
        _ = width;
        _ = allocator;
        return .{};
    }

    pub fn createFontTexture(self: *SoftwareRenderer, width: i32, height: i32, pixels: [*]const u8) c.ImTextureID {
        _ = pixels;
        _ = height;
        _ = width;
        _ = self;
        return null;
    }

    pub fn resize(self: *SoftwareRenderer, width: f32, height: f32) void {
        _ = height;
        _ = width;
        _ = self;
    }

    pub fn bufferData(self: *SoftwareRenderer, vtx_buffer: []const c.ImDrawVert, idx_buffer: []const c.ImDrawIdx) void {
        _ = idx_buffer;
        _ = vtx_buffer;
        _ = self;
    }

    pub fn draw(self: *SoftwareRenderer, clip_rect: c.ImRect, texture: c.ImTextureID, idx_count: u32, idx_offset: u32) void {
        _ = idx_offset;
        _ = idx_count;
        _ = texture;
        _ = clip_rect;
        _ = self;
    }
};

