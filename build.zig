const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const srcs = [_][]const u8{
        "src/draw.c",
        "src/memory.c",
        "src/string.c",
        "src/ui.c",
        "src/ui_widgets.c",
        "src/ztracing.c",

        "src/draw_sdl3.c",
        "src/log_sdl3.c",
    };

    const ztracing = b.addExecutable(.{
        .name = "ztracing",
        .target = target,
        .optimize = optimize,
    });
    ztracing.addCSourceFiles(.{
        .files = &(srcs ++ [_][]const u8{
            "src/ztracing_sdl3.c",
        }),
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

    const test_binary = b.addTest(.{
        .root_source_file = b.path("test/all.zig"),
        .target = target,
        .optimize = optimize,
    });
    test_binary.addCSourceFiles(.{ .files = &srcs });
    test_binary.addIncludePath(b.path("."));
    test_binary.linkLibC();
    test_binary.linkSystemLibrary("SDL3");
    const install_test = b.addInstallArtifact(test_binary, .{});
    b.getInstallStep().dependOn(&install_test.step);

    const run_test = b.addRunArtifact(install_test.artifact);
    const test_step = b.step("test", "Run tests");
    test_step.dependOn(&install_test.step);
    test_step.dependOn(&run_test.step);
}
