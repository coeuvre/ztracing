#include "ztracing.h"

#include <SDL.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>
#include <zlib.h>

static SDL_LogPriority TO_SDL_LOG_PRIORITY[NUM_LOG_LEVEL] = {
    [LOG_LEVEL_DEBUG] = SDL_LOG_PRIORITY_DEBUG,
    [LOG_LEVEL_INFO] = SDL_LOG_PRIORITY_INFO,
    [LOG_LEVEL_WARN] = SDL_LOG_PRIORITY_WARN,
    [LOG_LEVEL_ERROR] = SDL_LOG_PRIORITY_ERROR,
    [LOG_LEVEL_CRITICAL] = SDL_LOG_PRIORITY_CRITICAL,
};

static void os_log_message(LogLevel level, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    SDL_LogMessageV(
        SDL_LOG_CATEGORY_APPLICATION,
        TO_SDL_LOG_PRIORITY[level],
        fmt,
        args
    );
    va_end(args);
}

enum OsFileReadState {
    OS_FILE_READ_NONE,
    OS_FILE_READ_NORMAL,
    OS_FILE_READ_GZIP,
    OS_FILE_READ_DONE,
};

struct OsFile {
    char *name;
    SDL_RWops *rw;
    OsFileReadState read_state;
    z_stream zstream;
    u8 *zstream_buf;
    u32 zstream_buf_len;
};

static char *os_file_get_path(OsFile *file) {
    return file->name;
}

static u32 os_file_read(OsFile *file, u8 *buf, u32 len) {
    u32 nread = 0;
    for (bool need_more_read = true; need_more_read;) {
        switch (file->read_state) {
        case OS_FILE_READ_NONE: {
            u8 header_buf[2];
            u32 header_nread = file->rw->read(file->rw, header_buf, 1, 2);
            if (header_nread == 2 && header_buf[0] == 0x1F &&
                header_buf[1] == 0x8B) {
                file->read_state = OS_FILE_READ_GZIP;
            } else {
                file->read_state = OS_FILE_READ_NORMAL;
            }
            i64 offset = file->rw->seek(file->rw, 0, RW_SEEK_SET);
            ASSERT(offset == 0, "");

            int zret = inflateInit2(&file->zstream, MAX_WBITS | 32);
            // TODO: Error handling.
            ASSERT(zret == Z_OK, "");
            file->zstream_buf_len = 4096;
            file->zstream_buf = (u8 *)malloc(file->zstream_buf_len);
        } break;

        case OS_FILE_READ_NORMAL: {
            nread = file->rw->read(file->rw, buf, 1, len);
            need_more_read = false;
            if (nread == 0) {
                file->read_state = OS_FILE_READ_DONE;
            }
        } break;

        case OS_FILE_READ_GZIP: {
            z_stream *stream = &file->zstream;
            if (stream->avail_in == 0) {
                stream->avail_in = file->rw->read(
                    file->rw,
                    file->zstream_buf,
                    1,
                    file->zstream_buf_len
                );
                stream->next_in = file->zstream_buf;
            }

            if (stream->avail_in != 0) {
                stream->avail_out = len;
                stream->next_out = buf;
                int zret = inflate(stream, Z_NO_FLUSH);
                switch (zret) {
                case Z_OK: {
                    nread = len - stream->avail_out;
                    need_more_read = nread == 0;
                } break;

                case Z_STREAM_END: {
                    nread = len - stream->avail_out;
                    need_more_read = false;
                    file->read_state = OS_FILE_READ_DONE;
                } break;

                default: {
                    // TODO: Error handling.
                    ABORT("inflate returned %d", zret);
                } break;
                }
            } else {
                need_more_read = false;
                file->read_state = OS_FILE_READ_DONE;
            }
        } break;

        case OS_FILE_READ_DONE: {
            need_more_read = false;
        } break;

        default: {
            UNREACHABLE;
        } break;
        }
    }
    return nread;
}

static void os_file_close(OsFile *file) {
    int result = file->rw->close(file->rw);
    ASSERT(
        result == 0,
        "Failed to close file %s: %s",
        file->name,
        SDL_GetError()
    );
    free(file->name);
    // TODO: free zstream
    free(file);
}

static void load_file(ZTracing *ztracing, char *path) {
    OsFile *file = (OsFile *)malloc(sizeof(OsFile));
    ASSERT(file, "");
    *file = {};
    file->name = strdup(path);
    ASSERT(file->name, "");
    file->rw = SDL_RWFromFile(path, "rb");
    if (file->rw) {
        ztracing_load_file(ztracing, file);
    } else {
        ERROR("Failed to load file %s: %s", path, SDL_GetError());
        free(file->name);
        free(file);
    }
}

static OsThread *os_thread_create(OsThreadFunction fn, void *data) {
    SDL_Thread *thread = SDL_CreateThread(fn, "Worker", data);
    return (OsThread *)thread;
}

int main(int argc, char **argv) {
    if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
        ABORT("Failed to init SDL: %s", SDL_GetError());
    }

    char *startup_file = 0;
    if (argc > 1) {
        startup_file = argv[1];
    }

    SDL_Window *window = SDL_CreateWindow(
        "ztracing",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1280,
        720,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED
    );
    ASSERT(window, "Failed to create SDL_Window: %s", SDL_GetError());

    SDL_Renderer *renderer = SDL_CreateRenderer(
        window,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    ASSERT(renderer, "Failed to create SDL_Renderer: %s", SDL_GetError());

    ImGuiContext *imgui_context = ImGui::CreateContext();
    ASSERT(imgui_context, "Failed to create ImGui context");

    {
        ImGuiIO *io = &ImGui::GetIO();
        io->IniFilename = 0;
        io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        ImGuiStyle *style = &ImGui::GetStyle();
        ImGui::StyleColorsLight(style);
    }

    if (!ImGui_ImplSDL2_InitForSDLRenderer(window, renderer)) {
        ABORT("Failed to init ImGui with SDL");
    }

    if (!ImGui_ImplSDLRenderer2_Init(renderer)) {
        ABORT("Failed to init ImGui with SDL_Renderer");
    }

    ZTracing ztracing = {};
    if (startup_file) {
        load_file(&ztracing, startup_file);
    }

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (!ImGui_ImplSDL2_ProcessEvent(&event)) {
                switch (event.type) {
                case SDL_QUIT: {
                    running = false;
                } break;

                case SDL_DROPFILE: {
                    char *file = event.drop.file;
                    load_file(&ztracing, file);
                    SDL_free(file);
                } break;
                }
            }
        }

        ImGui_ImplSDL2_NewFrame();
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui::NewFrame();

        ztracing_update(&ztracing);

        ImGui::Render();
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
        SDL_RenderPresent(renderer);
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext(imgui_context);

    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
