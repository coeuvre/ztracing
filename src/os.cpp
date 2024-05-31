#include "app.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <SDL.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>

static SDL_LogPriority TO_SDL_LOG_PRIORITY[LogLevel_Count] = {
    [LogLevel_Debug] = SDL_LOG_PRIORITY_DEBUG,
    [LogLevel_Info] = SDL_LOG_PRIORITY_INFO,
    [LogLevel_Warn] = SDL_LOG_PRIORITY_WARN,
    [LogLevel_Error] = SDL_LOG_PRIORITY_ERROR,
    [LogLevel_Critical] = SDL_LOG_PRIORITY_CRITICAL,
};

static void
LogMessage(LogLevel level, const char *fmt, ...) {
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

static OsCond *
OsCondCreate() {
    SDL_cond *cond = SDL_CreateCond();
    if (!cond) {
        ABORT("Failed to create condition variable: %s", SDL_GetError());
    }
    return (OsCond *)cond;
}

static void
OsCondDestroy(OsCond *cond) {
    SDL_DestroyCond((SDL_cond *)cond);
}

static void
OsCondWait(OsCond *cond, OsMutex *mutex) {
    int ret = SDL_CondWait((SDL_cond *)cond, (SDL_mutex *)mutex);
    if (ret != 0) {
        ABORT("Failed to wait on condition variable: %s", SDL_GetError());
    }
}

static void
OsCondSingal(OsCond *cond) {
    int ret = SDL_CondSignal((SDL_cond *)cond);
    if (ret != 0) {
        ABORT("Failed to singal condition variable: %s", SDL_GetError());
    }
}

static void
OsCondBroadcast(OsCond *cond) {
    int ret = SDL_CondBroadcast((SDL_cond *)cond);
    if (ret != 0) {
        ABORT("Failed to singal condition variable: %s", SDL_GetError());
    }
}

static OsMutex *
OsMutexCreate() {
    SDL_mutex *mutex = SDL_CreateMutex();
    if (!mutex) {
        ABORT("Failed to create mutex: %s", SDL_GetError());
    }
    return (OsMutex *)mutex;
}

static void
OsMutexDestroy(OsMutex *mutex) {
    SDL_DestroyMutex((SDL_mutex *)mutex);
}

static void
OsMutexLock(OsMutex *mutex) {
    int ret = SDL_LockMutex((SDL_mutex *)mutex);
    if (ret != 0) {
        ABORT("Failed to lock mutex: %s", SDL_GetError());
    }
}

static void
OsMutexUnlock(OsMutex *mutex) {
    int ret = SDL_UnlockMutex((SDL_mutex *)mutex);
    if (ret != 0) {
        ABORT("Failed to unlock mutex: %s", SDL_GetError());
    }
}

static Channel *OS_TASK_CHANNEL;

static void
OsDispatchTask(Task *task) {
    ASSERT(OS_TASK_CHANNEL);
    bool sent = ChannelSend(OS_TASK_CHANNEL, &task);
    ASSERT(sent);
}

static int
WorkerMain(void *data) {
    Channel *channel = OS_TASK_CHANNEL;

    Task *task;
    while (ChannelRecv(channel, &task)) {
        task->func(task);

        OsMutexLock(task->mutex);
        task->done = true;
        OsCondBroadcast(task->cond);
        OsMutexUnlock(task->mutex);
    }

    ChannelCloseRx(channel);

    return 0;
}

static u64
OsGetPerformanceCounter() {
    u64 result = SDL_GetPerformanceCounter();
    return result;
}

static u64
OsGetPerformanceFrequency() {
    u64 result = SDL_GetPerformanceFrequency();
    return result;
}

static void
MaybeLoadFile(App *app, OsLoadingFile *file) {
    if (AppCanLoadFile(app)) {
        AppLoadFile(app, file);
    } else {
        OsLoadingFileClose(file);
    }
}

enum MainLoopState {
    MainLoopState_Init,
    MainLoopState_Update,
    MainLoopState_Shutdown,
};

struct MainLoop {
    int argc;
    char **argv;
    SDL_Thread *worker_thread;
    MainLoopState state;
    SDL_Window *window;
    SDL_Renderer *renderer;
    App *app;
    OsLoadingFile *loading_file;
};

static void *
ImGuiAlloc(usize size, void *user_data) {
    return AllocateMemoryNoZero(size);
}

static void
ImGuiFree(void *ptr, void *user_data) {
    DeallocateMemory(ptr);
}

static void *
MemoryCAlloc(usize num, usize size) {
    return AllocateMemory(num * size);
}

struct {
    SDL_mutex *mutex;
    volatile usize allocated_bytes;
} DEFAULT_ALLOCATOR;

static void
DefaultAllocatorInit() {
    DEFAULT_ALLOCATOR.mutex = SDL_CreateMutex();

    SDL_SetMemoryFunctions(
        AllocateMemoryNoZero,
        MemoryCAlloc,
        ReallocateMemory,
        DeallocateMemory
    );

    ImGui::SetAllocatorFunctions(ImGuiAlloc, ImGuiFree);
}

static void
DefaultAllocatorDeinit() {
    usize n = GetAllocatedBytes();
    if (n != 0) {
        ERROR("%zu bytes leaked!", n);
    }
}

static void
UpdateAllocatedBytes(usize delta) {
    int err = SDL_LockMutex(DEFAULT_ALLOCATOR.mutex);
    if (err != 0) {
        ABORT("%s", SDL_GetError());
    }
    DEFAULT_ALLOCATOR.allocated_bytes += delta;
    err = SDL_UnlockMutex(DEFAULT_ALLOCATOR.mutex);
    if (err != 0) {
        ABORT("%s", SDL_GetError());
    }
}

static void *
AllocateMemory(usize size, bool zero) {
    usize total_size = sizeof(size) + size;
    usize *result = (usize *)malloc(total_size);
    if (zero) {
        ASSERT(result);
        memset(result, 0, total_size);
    }
    if (result) {
        result[0] = total_size;
        result += 1;
        UpdateAllocatedBytes(total_size);
    }
    return result;
}

static void *
AllocateMemory(usize size) {
    return AllocateMemory(size, /* zero= */ true);
}

static void *
AllocateMemoryNoZero(usize size) {
    return AllocateMemory(size, /* zero= */ false);
}

static void *
ReallocateMemory(void *ptr_, usize new_size) {
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
        UpdateAllocatedBytes(new_total_size - total_size);
    }

    return ptr;
}

static void
DeallocateMemory(void *ptr_) {
    usize *ptr = (usize *)ptr_;
    if (ptr) {
        ptr -= 1;
        usize total_size = ptr[0];
        UpdateAllocatedBytes(-total_size);
    }
    free(ptr);
}

static usize
GetAllocatedBytes() {
    return DEFAULT_ALLOCATOR.allocated_bytes;
}

static MainLoop MAIN_LOOP = {};

static Vec2 GetInitialWindowSize();

static void NotifyAppInitDone();

static void
MainLoopInit(MainLoop *main_loop) {
    DefaultAllocatorInit();

    if (SDL_Init(SDL_INIT_EVERYTHING & ~(SDL_INIT_TIMER | SDL_INIT_HAPTIC)) !=
        0) {
        ABORT("Failed to init SDL: %s", SDL_GetError());
    }

    OS_TASK_CHANNEL = ChannelCreate(sizeof(Task *), 1);
    MAIN_LOOP.worker_thread = SDL_CreateThread(WorkerMain, "Worker", 0);
    ASSERT(MAIN_LOOP.worker_thread);

    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);

    Vec2 window_size = GetInitialWindowSize();

    SDL_Window *window = SDL_CreateWindow(
        "ztracing",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        window_size.x,
        window_size.y,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED
    );
    if (!window) {
        ABORT("Failed to create SDL_Window: %s", SDL_GetError());
    }
    main_loop->window = window;

    SDL_Renderer *renderer = SDL_CreateRenderer(
        window,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    if (!renderer) {
        ABORT("Failed to create SDL_Renderer: %s", SDL_GetError());
    }
    main_loop->renderer = renderer;

    ImGuiContext *imgui_context = ImGui::CreateContext();
    if (!imgui_context) {
        ABORT("Failed to create ImGui context");
    }

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

    main_loop->app = AppCreate();

    char *startup_file = 0;
    if (main_loop->argc > 1) {
        startup_file = main_loop->argv[1];
    }
    if (startup_file) {
        OsLoadingFile *file = OsLoadingFileOpen(startup_file);
        if (file) {
            MaybeLoadFile(main_loop->app, file);
        }
    }

    main_loop->state = MainLoopState_Update;

    NotifyAppInitDone();
}

static void
MainLoopUpdate(MainLoop *main_loop) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (!ImGui_ImplSDL2_ProcessEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT: {
                main_loop->state = MainLoopState_Shutdown;
            } break;

            case SDL_DROPFILE: {
                char *path = event.drop.file;
                OsLoadingFile *file = OsLoadingFileOpen(path);
                if (file) {
                    MaybeLoadFile(main_loop->app, file);
                }
                SDL_free(path);
            } break;
            }
        }
    }

    ImGui_ImplSDL2_NewFrame();
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui::NewFrame();

    AppUpdate(main_loop->app);

    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
    SDL_RenderPresent(main_loop->renderer);
}

static void
MainLoopShutdown(MainLoop *main_loop) {
    AppDestroy(main_loop->app);

    ChannelCloseTx(OS_TASK_CHANNEL);
    SDL_WaitThread(main_loop->worker_thread, 0);

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(main_loop->renderer);
    SDL_DestroyWindow(main_loop->window);
    SDL_Quit();

    DefaultAllocatorDeinit();

#ifdef __EMSCRIPTEN__
    emscripten_cancel_main_loop();
#else
    exit(0);
#endif
}

static void
RunMainLoop(void *arg) {
    MainLoop *main_loop = (MainLoop *)arg;
    switch (main_loop->state) {
    case MainLoopState_Init: {
        MainLoopInit(main_loop);
    } break;
    case MainLoopState_Update: {
        MainLoopUpdate(main_loop);
    } break;
    case MainLoopState_Shutdown: {
        MainLoopShutdown(main_loop);
    } break;
    }
}

int
main(int argc, char **argv) {
    MAIN_LOOP.argc = argc;
    MAIN_LOOP.argv = argv;
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(RunMainLoop, &MAIN_LOOP, 0, 0);
#else
    while (true) {
        RunMainLoop(&MAIN_LOOP);
    }
#endif
    return 0;
}
