const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const test_filters = b.option([]const []const u8, "test-filter", "Skip tests that do not match any filter") orelse &[0][]const u8{};

    const srcs = [_][]const u8{
        "src/draw.c",
        "src/memory.c",
        "src/string.c",
        "src/ui.c",
        "src/ui_widgets.c",

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
            "src/ztracing.c",
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
        if (optimize != .Debug) {
            ztracing.subsystem = .Windows;
        }
    }
    b.installArtifact(ztracing);

    const test_binary = b.addTest(.{
        .root_source_file = b.path("test/root.zig"),
        .target = target,
        .optimize = optimize,
        .filters = test_filters,
    });
    test_binary.addCSourceFiles(.{ .files = &srcs });
    test_binary.addIncludePath(b.path("."));
    test_binary.linkLibC();
    test_binary.linkSystemLibrary("SDL3");
    const install_test = b.addInstallArtifact(test_binary, .{});

    const run_test = b.addRunArtifact(install_test.artifact);
    const test_step = b.step("test", "Run tests");
    test_step.dependOn(&install_test.step);
    test_step.dependOn(&run_test.step);
}
