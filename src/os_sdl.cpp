#include "ztracing.h"

#include <SDL.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>

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

struct OsFile {
    char *name;
    SDL_RWops *rw;
};

static char *os_file_get_path(OsFile *file) {
    return file->name;
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
    free(file);
}

static void load_file(ZTracing *ztracing, char *path) {
    OsFile *file = (OsFile *)malloc(sizeof(OsFile));
    ASSERT(file, "");
    file->name = strdup(path);
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
