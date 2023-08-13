const std = @import("std");

fn addGenProfile(b: *std.Build, target: std.zig.CrossTarget, optimize: std.builtin.Mode) void {
    const gen_profile = b.addExecutable(.{
        .name = "gen_profile",
        .root_source_file = .{ .path = "src/gen_profile.zig" },
        .target = target,
        .optimize = optimize,
    });
    const install_gen_profile = b.addInstallArtifact(gen_profile, .{});
    const install_gen_profile_step = b.step("gen_profile", "Install gen_profile");
    install_gen_profile_step.dependOn(&install_gen_profile.step);

    const run_gen_profile = b.addRunArtifact(gen_profile);
    run_gen_profile.step.dependOn(install_gen_profile_step);
    if (b.args) |args| {
        run_gen_profile.addArgs(args);
    }
    const run_gen_profile_step = b.step("run_gen_profile", "Run gen_profile");
    run_gen_profile_step.dependOn(&run_gen_profile.step);
}

fn addBenchParser(b: *std.Build, target: std.zig.CrossTarget, optimize: std.builtin.Mode) void {
    const bench_parser = b.addExecutable(.{
        .name = "bench_parser",
        .root_source_file = .{ .path = "src/bench_parser.zig" },
        .target = target,
        .optimize = optimize,
    });
    const install_bench_parser = b.addInstallArtifact(bench_parser, .{});
    const install_bench_parser_step = b.step("bench_parser", "Install bench_parser");
    install_bench_parser_step.dependOn(&install_bench_parser.step);

    const run_bench_parser = b.addRunArtifact(bench_parser);
    run_bench_parser.step.dependOn(install_bench_parser_step);
    if (b.args) |args| {
        run_bench_parser.addArgs(args);
    }
    const run_bench_parser_step = b.step("run_bench_parser", "Run bench_parser");
    run_bench_parser_step.dependOn(&run_bench_parser.step);
}

// Although this function looks imperative, note that its job is to
// declaratively construct a build graph that will be executed by an external
// runner.
pub fn build(b: *std.Build) void {
    // Standard target options allows the person running `zig build` to choose
    // what target to build for. Here we do not override the defaults, which
    // means any target is allowed, and the default is native. Other options
    // for restricting supported target set are available.
    const target = b.standardTargetOptions(.{});

    // Standard optimization options allow the person running `zig build` to select
    // between Debug, ReleaseSafe, ReleaseFast, and ReleaseSmall. Here we do not
    // set a preferred release mode, allowing the user to decide how to optimize.
    const optimize = b.standardOptimizeOption(.{});

    const imgui = b.addStaticLibrary(.{
        .name = "imgui",
        .target = target,
        .optimize = .ReleaseSmall,
    });
    imgui.addIncludePath(.{ .path = "third_party" });
    imgui.addIncludePath(.{ .path = "third_party/cimgui" });
    imgui.addIncludePath(.{ .path = "third_party/cimgui/imgui" });
    imgui.addCSourceFiles(&.{
        "src/imgui_wrapper.cpp",
    }, &.{});
    imgui.linkLibC();
    imgui.linkLibCpp();

    const rtracing = b.addSharedLibrary(.{
        .name = "ztracing",
        .root_source_file = .{ .path = "src/main_wasm.zig" },
        .target = target,
        .optimize = optimize,
    });
    rtracing.export_symbol_names = &[_][]const u8{
        "init",
        "update",
        "onResize",
        "onMousePos",
        "onMouseButton",
        "onMouseWheel",
        "onKey",
        "onFocus",
        "shouldLoadFile",
        "onLoadFileStart",
        "onLoadFileChunk",
        "onLoadFileDone",
    };
    rtracing.linkLibrary(imgui);
    rtracing.addIncludePath(.{ .path = "third_party/cimgui" });

    // This declares intent for the executable to be installed into the
    // standard location when the user invokes the "install" step (the default
    // step when running `zig build`).
    b.installArtifact(rtracing);

    addGenProfile(b, target, optimize);
    addBenchParser(b, target, optimize);

    // This *creates* a Run step in the build graph, to be executed when another
    // step is evaluated that depends on it. The next line below will establish
    // such a dependency.
    // const run_cmd = b.addRunArtifact(exe);

    // By making the run step depend on the install step, it will be run from the
    // installation directory rather than directly from within the cache directory.
    // This is not necessary, however, if the application depends on other installed
    // files, this ensures they will be present and in the expected location.
    // run_cmd.step.dependOn(b.getInstallStep());

    // This allows the user to pass arguments to the application in the build
    // command itself, like this: `zig build run -- arg1 arg2 etc`
    // if (b.args) |args| {
    //     run_cmd.addArgs(args);
    // }

    // This creates a build step. It will be visible in the `zig build --help` menu,
    // and can be selected like this: `zig build run`
    // This will evaluate the `run` step rather than the default, which is "install".
    // const run_step = b.step("run", "Run the app");
    // run_step.dependOn(&run_cmd.step);

    // Creates a step for unit testing. This only builds the test executable
    // but does not run it.
    const unit_tests = b.addTest(.{
        .root_source_file = .{ .path = "src/json_profile_parser.zig" },
        .target = target,
        .optimize = optimize,
    });

    const run_unit_tests = b.addRunArtifact(unit_tests);

    // Similar to creating the run step earlier, this exposes a `test` step to
    // the `zig build --help` menu, providing a way for the user to request
    // running the unit tests.
    const test_step = b.step("test", "Run unit tests");
    test_step.dependOn(&run_unit_tests.step);
}
