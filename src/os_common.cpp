#include "ztracing.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
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

static OsThread *os_thread_create(OsThreadFunction fn, void *data) {
    SDL_Thread *thread = SDL_CreateThread(fn, "Worker", data);
    ASSERT(thread, "Failed to create thread: %s", SDL_GetError());
    return (OsThread *)thread;
}

static void os_thread_join(OsThread *thread_) {
    SDL_Thread *thread = (SDL_Thread *)thread_;
    int status;
    SDL_WaitThread(thread, &status);
    ASSERT(status == 0, "");
}

static void maybe_load_file(App *app, OsLoadingFile *file) {
    if (ztracing_accept_load(app)) {
        ztracing_load_file(app, file);
    } else {
        os_loading_file_close(file);
    }
}

enum MainLoopState {
    MAIN_LOOP_INIT,
    MAIN_LOOP_UPDATE,
    MAIN_LOOP_SHUTDOWN,
};

struct MainLoop {
    int argc;
    char **argv;
    MainLoopState state;
    SDL_Window *window;
    SDL_Renderer *renderer;
    App app;
};

static void *imgui_alloc(usize size, void *user_data) {
    return memory_alloc(size);
}

static void imgui_free(void *ptr, void *user_data) {
    memory_free(ptr);
}

struct {
    SDL_mutex *mutex;
    volatile usize allocated_bytes;
} DEFAULT_ALLOCATOR;

static void default_allocator_init() {
    DEFAULT_ALLOCATOR.mutex = SDL_CreateMutex();

    SDL_SetMemoryFunctions(
        memory_alloc,
        memory_calloc,
        memory_realloc,
        memory_free
    );

    ImGui::SetAllocatorFunctions(imgui_alloc, imgui_free);
}

static void default_allocator_deinit() {
    ASSERT(memory_get_allocated_bytes() == 0, "Memory leaked!");
}

static void update_allocated_bytes(usize delta) {
    int err = SDL_LockMutex(DEFAULT_ALLOCATOR.mutex);
    ASSERT(err == 0, "%s", SDL_GetError());
    DEFAULT_ALLOCATOR.allocated_bytes += delta;
    err = SDL_UnlockMutex(DEFAULT_ALLOCATOR.mutex);
    ASSERT(err == 0, "%s", SDL_GetError());
}

static void *do_memory_alloc(usize size, bool zero) {
    usize total_size = sizeof(size) + size;
    usize *result = (usize *)malloc(total_size);
    if (result) {
        result[0] = total_size;
        result += 1;

        if (zero) {
            memset(result, 0, size);
        }

        update_allocated_bytes(total_size);
    }
    return result;
}

static void *memory_alloc(usize size) {
    return do_memory_alloc(size, /* zero= */ false);
}

static void *memory_calloc(usize sum, usize size) {
    return do_memory_alloc(sum * size, /* zero= */ true);
}

static void *memory_realloc(void *ptr_, usize new_size) {
    usize *ptr = (usize *)ptr_;

    usize total_size = 0;
    if (ptr) {
        ptr = ptr - 1;
        total_size = ptr[0];
    }

    usize new_total_size = sizeof(usize) + new_size;
    ptr = (usize *)realloc(ptr, new_total_size);

    if (ptr) {
        ptr[0] = new_total_size;
        ptr += 1;
        update_allocated_bytes(new_total_size - total_size);
    }

    return ptr;
}

static void memory_free(void *ptr_) {
    usize *ptr = (usize *)ptr_;
    if (ptr) {
        ptr -= 1;
        usize total_size = ptr[0];
        update_allocated_bytes(-total_size);
    }
    free(ptr);
}

static usize memory_get_allocated_bytes() {
    return DEFAULT_ALLOCATOR.allocated_bytes;
}

static MainLoop MAIN_LOOP = {};

static Vec2 get_initial_window_size();

static void main_loop_init(MainLoop *main_loop) {
    default_allocator_init();

    if (SDL_Init(SDL_INIT_EVERYTHING & ~(SDL_INIT_TIMER | SDL_INIT_HAPTIC)) !=
        0) {
        ABORT("Failed to init SDL: %s", SDL_GetError());
    }

    char *startup_file = 0;
    if (main_loop->argc > 1) {
        startup_file = main_loop->argv[1];
    }

    Vec2 window_size = get_initial_window_size();

    SDL_Window *window = SDL_CreateWindow(
        "ztracing",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        window_size.x,
        window_size.y,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED
    );
    ASSERT(window, "Failed to create SDL_Window: %s", SDL_GetError());
    main_loop->window = window;

    SDL_Renderer *renderer = SDL_CreateRenderer(
        window,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    ASSERT(renderer, "Failed to create SDL_Renderer: %s", SDL_GetError());
    main_loop->renderer = renderer;

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

    if (startup_file) {
        OsLoadingFile *file = os_loading_file_open(startup_file);
        if (file) {
            maybe_load_file(&main_loop->app, file);
        }
    }

    main_loop->state = MAIN_LOOP_UPDATE;
}

static void main_loop_update(MainLoop *main_loop) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (!ImGui_ImplSDL2_ProcessEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT: {
                main_loop->state = MAIN_LOOP_SHUTDOWN;
            } break;

            case SDL_DROPFILE: {
                char *path = event.drop.file;
                OsLoadingFile *file = os_loading_file_open(path);
                if (file) {
                    maybe_load_file(&main_loop->app, file);
                }
                SDL_free(path);
            } break;
            }
        }
    }

    ImGui_ImplSDL2_NewFrame();
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui::NewFrame();

    ztracing_update(&main_loop->app);

    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
    SDL_RenderPresent(main_loop->renderer);
}

static void main_loop_shutdown(MainLoop *main_loop) {
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(main_loop->renderer);
    SDL_DestroyWindow(main_loop->window);
    SDL_Quit();

    default_allocator_deinit();

#ifdef __EMSCRIPTEN__
    emscripten_cancel_main_loop();
#else
    exit(0);
#endif
}

static void main_loop(void *arg) {
    MainLoop *main_loop = (MainLoop *)arg;
    switch (main_loop->state) {
    case MAIN_LOOP_INIT: {
        main_loop_init(main_loop);
    } break;
    case MAIN_LOOP_UPDATE: {
        main_loop_update(main_loop);
    } break;
    case MAIN_LOOP_SHUTDOWN: {
        main_loop_shutdown(main_loop);
    } break;
    }
}

int main(int argc, char **argv) {
    MAIN_LOOP.argc = argc;
    MAIN_LOOP.argv = argv;
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(main_loop, &MAIN_LOOP, 0, 0);
#else
    while (true) {
        main_loop(&MAIN_LOOP);
    }
#endif
    return 0;
}
