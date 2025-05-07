const std = @import("std");

const Build = std.Build;

fn addDeps(b: *Build, exe: *Build.Step.Compile) void {
    const target = exe.rootModuleTarget();
    switch (target.os.tag) {
        .macos => {
            const framework_path = b.path("third_party/SDL3/macos");
            exe.addFrameworkPath(framework_path);
            exe.linkFramework("SDL3");
            exe.addRPath(framework_path);
        },
        .windows => {
            exe.addIncludePath(b.path("third_party/SDL3/windows/include"));
            exe.addLibraryPath(b.path("third_party/SDL3/windows/lib"));
            exe.linkSystemLibrary("SDL3");
        },
        else => std.debug.panic("Unsupported OS {}", .{target.os.tag}),
    }
    exe.linkLibC();

    exe.addIncludePath(b.path("."));
}

pub fn build(b: *std.Build) !void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const test_filters = b.option([]const []const u8, "test-filter", "Skip tests that do not match any filter") orelse &[0][]const u8{};

    const srcs = [_][]const u8{
        "src/draw.c",
        "src/json.c",
        "src/json_trace_profile.c",
        "src/memory.c",
        "src/string.c",
        "src/flick.c",

        "src/draw_sdl3.c",
        "src/platform_sdl3.c",
        "src/log_sdl3.c",
    };

    const ztracing = b.addExecutable(.{
        .name = "ztracing",
        .target = target,
        .optimize = optimize,
    });
    _ = try ztracing.step.addDirectoryWatchInput(b.path("src"));
    ztracing.addCSourceFiles(.{
        .files = &(srcs ++ [_][]const u8{
            "src/ztracing.c",
            "src/ztracing_sdl3.c",
        }),
    });
    addDeps(b, ztracing);
    b.installArtifact(ztracing);

    const test_binary = b.addTest(.{
        .root_source_file = b.path("test/root.zig"),
        .target = target,
        .optimize = optimize,
        .filters = test_filters,
    });
    test_binary.addCSourceFiles(.{ .files = &srcs });
    addDeps(b, test_binary);
    const install_test = b.addInstallArtifact(test_binary, .{});

    const run_test = b.addRunArtifact(install_test.artifact);
    const test_step = b.step("test", "Run tests");
    test_step.dependOn(&install_test.step);
    test_step.dependOn(&run_test.step);
}
