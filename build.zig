const std = @import("std");

fn addGenProfile(b: *std.Build, target: std.Build.ResolvedTarget, optimize: std.builtin.Mode) void {
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

fn addBenchParser(b: *std.Build, target: std.Build.ResolvedTarget, optimize: std.builtin.Mode) void {
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

fn add_imgui(b: *std.Build, target: std.Build.ResolvedTarget, optimize: std.builtin.Mode) *std.Build.Step.Compile {
    const imgui = b.addStaticLibrary(.{
        .name = "imgui",
        .target = target,
        .optimize = if (optimize == .Debug) .ReleaseSafe else optimize,
    });
    imgui.addIncludePath(.{ .path = "." });
    imgui.addIncludePath(.{ .path = "third_party/cimgui/imgui" });
    if (target.result.isWasm()) {
        imgui.defineCMacro("ZTRACING_WASM", "1");
    } else {
        imgui.addIncludePath(.{ .path = "third_party/SDL/build/install/include/SDL2" });
    }
    imgui.addCSourceFiles(.{
        .files = &.{
            "src/imgui_wrapper.cpp",
        },
    });
    imgui.linkLibC();
    // msvc doesn't support -lc++ yet. https://github.com/ziglang/zig/issues/5312
    if (target.result.abi != .msvc) {
        imgui.linkLibCpp();
    }

    return imgui;
}

fn add_zlib(b: *std.Build, target: std.Build.ResolvedTarget, optimize: std.builtin.Mode) *std.Build.Step.Compile {
    const zlib = b.addStaticLibrary(.{
        .name = "zlib",
        .target = target,
        .optimize = if (optimize == .Debug) .ReleaseSafe else optimize,
    });
    zlib.addIncludePath(.{ .path = "third_party/zlib" });
    zlib.defineCMacro("Z_SOLO", "1");
    zlib.addCSourceFiles(.{
        .files = &.{
            "third_party/zlib/adler32.c",
            "third_party/zlib/compress.c",
            "third_party/zlib/crc32.c",
            "third_party/zlib/deflate.c",
            "third_party/zlib/inflate.c",
            "third_party/zlib/infback.c",
            "third_party/zlib/inftrees.c",
            "third_party/zlib/inffast.c",
            "third_party/zlib/trees.c",
            "third_party/zlib/uncompr.c",
            "third_party/zlib/zutil.c",
        },
    });
    zlib.linkLibC();
    return zlib;
}

fn add_tracy(b: *std.Build, target: std.Build.ResolvedTarget, optimize: std.builtin.Mode) *std.Build.Step.Compile {
    const tracy = b.addStaticLibrary(.{
        .name = "tracy",
        .target = target,
        .optimize = if (optimize == .Debug) .ReleaseSafe else optimize,
    });
    tracy.addIncludePath(.{ .path = "third_party/tracy/public/tracy" });
    tracy.defineCMacro("TRACY_ENABLE", "1");
    tracy.addCSourceFiles(.{
        .files = &.{
            "third_party/tracy/public/TracyClient.cpp",
        },
    });
    tracy.linkLibC();
    if (target.result.os.tag != .windows) {
        tracy.linkLibCpp();
    }
    return tracy;
}

fn add_ztraing(b: *std.Build, target: std.Build.ResolvedTarget, optimize: std.builtin.Mode) *std.Build.Step.Compile {
    const build_options = b.addOptions();

    const imgui = add_imgui(b, target, optimize);
    const ztracing = blk: {
        if (target.result.isWasm()) {
            build_options.addOption(bool, "enable_tracy", false);

            const ztracing = b.addExecutable(.{
                .name = "ztracing",
                .root_source_file = .{ .path = "src/main_wasm.zig" },
                .target = target,
                .optimize = optimize,
            });
            ztracing.root_module.export_symbol_names = &.{
                "init",
                "update",
                "on_resize",
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
            ztracing.defineCMacro("ZTRACING_WASM", "1");
            break :blk ztracing;
        } else {
            const zlib = add_zlib(b, target, optimize);
            const tracy = add_tracy(b, target, optimize);

            const ztracing = b.addExecutable(.{
                .name = "ztracing",
                .root_source_file = .{ .path = "src/main_native.zig" },
                .target = target,
                .optimize = optimize,
            });
            ztracing.root_module.addAnonymousImport("assets", .{
                .root_source_file = .{ .path = "assets/assets.zig" },
            });

            ztracing.addIncludePath(.{ .path = "third_party/zlib" });
            ztracing.linkLibrary(zlib);

            ztracing.addIncludePath(.{ .path = "third_party/SDL/build/install/include" });
            switch (target.result.os.tag) {
                .windows => {
                    ztracing.addObjectFile(.{ .path = "third_party/SDL/build/install/lib/SDL2-static.lib" });
                    ztracing.linkSystemLibrary("setupapi");
                    ztracing.linkSystemLibrary("user32");
                    ztracing.linkSystemLibrary("winmm");
                    ztracing.linkSystemLibrary("gdi32");
                    ztracing.linkSystemLibrary("imm32");
                    ztracing.linkSystemLibrary("version");
                    ztracing.linkSystemLibrary("oleaut32");
                    ztracing.linkSystemLibrary("ole32");
                    ztracing.linkSystemLibrary("shell32");
                    ztracing.linkSystemLibrary("advapi32");
                },
                .macos => {
                    ztracing.addObjectFile(.{ .path = "third_party/SDL/build/install/lib/libSDL2.a" });
                    ztracing.linkFramework("CoreVideo");
                    ztracing.linkFramework("Cocoa");
                    ztracing.linkFramework("IOKit");
                    ztracing.linkFramework("ForceFeedback");
                    ztracing.linkFramework("Carbon");
                    ztracing.linkFramework("CoreAudio");
                    ztracing.linkFramework("AudioToolbox");
                    ztracing.linkFramework("AVFoundation");
                    ztracing.linkFramework("Foundation");
                    ztracing.linkFramework("GameController");
                    ztracing.linkFramework("Metal");
                    ztracing.linkFramework("QuartzCore");
                    ztracing.linkFramework("CoreHaptics");
                    ztracing.linkSystemLibrary("iconv");
                },
                .linux => {
                    ztracing.addObjectFile(.{ .path = "third_party/SDL/build/install/lib/libSDL2.a" });
                },
                else => @panic("Unsupported os"),
            }
            ztracing.linkLibC();
            ztracing.root_module.single_threaded = false;

            const enable_tracy = b.option(bool, "tracy", "Enable Tracy integration") orelse false;
            build_options.addOption(bool, "enable_tracy", enable_tracy);
            if (enable_tracy) {
                ztracing.addIncludePath(.{ .path = "third_party/tracy/public/tracy" });
                build_options.addOption(bool, "enable_tracy_allocation", true);
                build_options.addOption(bool, "enable_tracy_callstack", true);
                ztracing.linkLibrary(tracy);
            }

            break :blk ztracing;
        }
    };
    ztracing.linkLibrary(imgui);
    ztracing.addIncludePath(.{ .path = "." });
    ztracing.addIncludePath(.{ .path = "third_party/cimgui" });
    ztracing.root_module.addOptions("build_options", build_options);

    return ztracing;
}

// Although this function looks imperative, note that its job is to
// declaratively construct a build graph that will be executed by an external
// runner.
pub fn build(b: *std.Build) void {
    // Standard target options allows the person running `zig build` to choose
    // what target to build for. Here we do not override the defaults, which
    // means any target is allowed, and the default is native. Other options
    // for restricting supported target set are available.
    var target = b.standardTargetOptions(.{});
    if (target.result.os.tag == .windows and target.result.abi != .msvc) {
        target = b.resolveTargetQuery(.{ .os_tag = .windows, .abi = .msvc });
    }

    // Standard optimization options allow the person running `zig build` to select
    // between Debug, ReleaseSafe, ReleaseFast, and ReleaseSmall. Here we do not
    // set a preferred release mode, allowing the user to decide how to optimize.
    const optimize = b.standardOptimizeOption(.{});

    const ztracing = add_ztraing(b, target, optimize);
    b.installArtifact(ztracing);

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
        .root_source_file = .{ .path = "src/test.zig" },
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
