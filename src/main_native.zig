const std = @import("std");
const c = @import("c.zig");
const imgui = @import("imgui.zig");

const Tracing = @import("tracing.zig").Tracing;
const Allocator = std.mem.Allocator;
const CountAllocator = @import("./count_alloc.zig").CountAllocator;

fn show_open_file_picker() void {}

fn get_seconds_elapsed(from: usize, to: usize, freq: usize) f32 {
    return @floatCast(@as(f64, @floatFromInt(to - from)) / @as(f64, @floatFromInt(freq)));
}

const LoadingFile = struct {
    file: std.fs.File,
    decompress: ?std.compress.gzip.Decompress(std.fs.File.Reader),

    pub fn init(allocator: Allocator, file: std.fs.File) !LoadingFile {
        var header = std.mem.zeroes([2]u8);
        const bytes_read = try file.read(&header);
        const is_gzip = bytes_read == 2 and header[0] == 0x1F and header[1] == 0x8B;
        try file.seekTo(0);
        return .{
            .file = file,
            .decompress = if (is_gzip) try std.compress.gzip.decompress(allocator, file.reader()) else null,
        };
    }

    pub fn deinit(self: *LoadingFile) void {
        if (self.decompress) |*decompress| {
            decompress.deinit();
        }
        self.file.close();
    }

    pub fn read(self: *LoadingFile, buffer: []u8) !usize {
        if (self.decompress) |*decompress| {
            return decompress.read(buffer);
        }
        return try self.file.read(buffer);
    }
};

pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    var count_allocator = CountAllocator.init(gpa.allocator());
    var allocator = count_allocator.allocator();

    _ = c.SDL_Init(c.SDL_INIT_EVERYTHING);

    var mode: c.SDL_DisplayMode = undefined;
    _ = c.SDL_GetCurrentDisplayMode(0, &mode);
    const window_width = mode.w;
    const window_height = mode.h;

    const window = c.SDL_CreateWindow(
        "ztracing",
        c.SDL_WINDOWPOS_CENTERED,
        c.SDL_WINDOWPOS_CENTERED,
        window_width,
        window_height,
        c.SDL_WINDOW_ALLOW_HIGHDPI | c.SDL_WINDOW_HIDDEN | c.SDL_WINDOW_RESIZABLE,
    ).?;

    const renderer = c.SDL_CreateRenderer(window, 0, c.SDL_RENDERER_ACCELERATED).?;

    c.igSetAllocatorFunctions(imgui.alloc, imgui.free, &allocator);

    _ = c.igCreateContext(null);

    const io = c.igGetIO();
    {
        const style = c.igGetStyle();
        c.igStyleColorsLight(style);

        style.*.ScrollbarRounding = 0.0;
        style.*.ScrollbarSize = 18.0;

        style.*.SeparatorTextBorderSize = 1.0;
    }
    io.*.ConfigFlags |= c.ImGuiConfigFlags_DockingEnable;

    _ = c.ig_ImplSDL2_InitForSDLRenderer(window, renderer);
    _ = c.ig_ImplSDLRenderer_Init(renderer);

    var tracing = Tracing.init(&count_allocator, show_open_file_picker);

    c.SDL_ShowWindow(window);

    const target_fps: f32 = 60.0;
    const target_seconds_per_frame = 1.0 / target_fps;

    var loading_file: ?LoadingFile = null;
    var loading_buf = std.mem.zeroes([4096]u8);

    const perf_frequency = c.SDL_GetPerformanceFrequency();
    var last_counter = c.SDL_GetPerformanceCounter();
    var running = true;
    while (running) {
        var event: c.SDL_Event = undefined;
        while (c.SDL_PollEvent(&event) == 1) {
            if (c.ig_ImplSDL2_ProcessEvent(&event)) {
                continue;
            }

            switch (event.type) {
                c.SDL_QUIT => {
                    running = false;
                    break;
                },
                c.SDL_DROPFILE => {
                    std.log.debug("Dropped file {s}", .{event.drop.file});
                    if (tracing.should_load_file()) {
                        const file = try std.fs.openFileAbsoluteZ(event.drop.file, .{});
                        const stat = try file.stat();
                        loading_file = try LoadingFile.init(allocator, file);
                        tracing.on_load_file_start(stat.size);
                    }
                },
                else => {},
            }
        }

        c.ig_ImplSDLRenderer_NewFrame();
        c.ig_ImplSDL2_NewFrame();
        c.igNewFrame();

        if (loading_file) |*file| {
            while (true) {
                const bytes_read = try file.read(&loading_buf);
                const offset = try file.file.getPos();
                tracing.on_load_file_chunk(offset, loading_buf[0..bytes_read]);
                if (bytes_read < loading_buf.len) {
                    tracing.on_load_file_done();
                    file.deinit();
                    loading_file = null;
                    break;
                }
                if (get_seconds_elapsed(last_counter, c.SDL_GetPerformanceCounter(), perf_frequency) >= target_seconds_per_frame) {
                    break;
                }
            }
        }

        const current_counter = c.SDL_GetPerformanceCounter();
        const dt = get_seconds_elapsed(last_counter, current_counter, perf_frequency);
        tracing.update(dt);
        last_counter = current_counter;

        c.igRender();

        _ = c.SDL_RenderClear(renderer);
        c.ig_ImplSDLRenderer_RenderDrawData(c.igGetDrawData());
        c.SDL_RenderPresent(renderer);
    }
}
