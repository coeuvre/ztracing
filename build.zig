const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const ztracing = b.addExecutable(.{
        .name = "ztracing",
        .target = target,
        .optimize = optimize,
    });
    ztracing.addCSourceFiles(.{
        .files = &.{
            "src/draw.c",
            "src/memory.c",
            "src/string.c",
            "src/ui.c",
            "src/ui_widgets.c",
            "src/ztracing.c",

            "src/draw_sdl3.c",
            "src/log_sdl3.c",
            "src/ztracing_sdl3.c",
        },
    });
    ztracing.addIncludePath(b.path("."));
    ztracing.linkLibC();
    ztracing.linkSystemLibrary("SDL3");
    if (target.result.os.tag == .windows) {
        ztracing.linkSystemLibrary("Winmm");
        ztracing.linkSystemLibrary("Ole32");
        ztracing.linkSystemLibrary("Setupapi");
        ztracing.linkSystemLibrary("Gdi32");
        ztracing.linkSystemLibrary("OleAut32");
        ztracing.linkSystemLibrary("Imm32");
        ztracing.linkSystemLibrary("Version");
        ztracing.subsystem = .Windows;
    }
    b.installArtifact(ztracing);
}
