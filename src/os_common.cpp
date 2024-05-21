#include "ztracing.h"

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

static void LogMessage(LogLevel level, const char *fmt, ...) {
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

static OsCond *OsCondCreate() {
    SDL_cond *cond = SDL_CreateCond();
    ASSERT(cond, "Failed to create condition variable: %s", SDL_GetError());
    return (OsCond *)cond;
}

static void OsCondDestroy(OsCond *cond) {
    SDL_DestroyCond((SDL_cond *)cond);
}

static void OsCondWait(OsCond *cond, OsMutex *mutex) {
    int ret = SDL_CondWait((SDL_cond *)cond, (SDL_mutex *)mutex);
    ASSERT(
        ret == 0,
        "Failed to wait on condition variable: %s",
        SDL_GetError()
    );
}

static void OsCondSingal(OsCond *cond) {
    int ret = SDL_CondSignal((SDL_cond *)cond);
    ASSERT(ret == 0, "Failed to singal condition variable: %s", SDL_GetError());
}

static void OsCondBroadcast(OsCond *cond) {
    int ret = SDL_CondBroadcast((SDL_cond *)cond);
    ASSERT(ret == 0, "Failed to singal condition variable: %s", SDL_GetError());
}

static OsMutex *OsMutexCreate() {
    SDL_mutex *mutex = SDL_CreateMutex();
    ASSERT(mutex, "Failed to create mutex: %s", SDL_GetError());
    return (OsMutex *)mutex;
}

static void OsMutexDestroy(OsMutex *mutex) {
    SDL_DestroyMutex((SDL_mutex *)mutex);
}

static void OsMutexLock(OsMutex *mutex) {
    int ret = SDL_LockMutex((SDL_mutex *)mutex);
    ASSERT(ret == 0, "Failed to lock mutex: %s", SDL_GetError());
}

static void OsMutexUnlock(OsMutex *mutex) {
    int ret = SDL_UnlockMutex((SDL_mutex *)mutex);
    ASSERT(ret == 0, "Failed to unlock mutex: %s", SDL_GetError());
}

static Channel *OS_TASK_CHANNEL;

static bool OsDispatchTask(Task *task) {
    bool sent = ChannelSend(OS_TASK_CHANNEL, &task);
    return sent;
}

static int WorkerMain(void *data) {
    Channel *channel = OS_TASK_CHANNEL;

    Task *task;
    while (ChannelRecv(channel, &task)) {
        task->func(task->data);

        OsMutexLock(task->mutex);
        task->done = true;
        OsCondBroadcast(task->cond);
        OsMutexUnlock(task->mutex);
    }

    ChannelCloseRx(channel);

    return 0;
}

static u64 OsGetPerformanceCounter() {
    u64 result = SDL_GetPerformanceCounter();
    return result;
}

static u64 OsGetPerformanceFrequency() {
    u64 result = SDL_GetPerformanceFrequency();
    return result;
}

static void MaybeLoadFile(App *app, OsLoadingFile *file) {
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

static void *ImGuiAlloc(usize size, void *user_data) {
    return MemoryAllocNoZero(size);
}

static void ImGuiFree(void *ptr, void *user_data) {
    MemoryFree(ptr);
}

static void *MemoryCAlloc(usize num, usize size) {
    return MemoryAlloc(num * size);
}

struct {
    SDL_mutex *mutex;
    volatile usize allocated_bytes;
} DEFAULT_ALLOCATOR;

static void DefaultAllocatorInit() {
    DEFAULT_ALLOCATOR.mutex = SDL_CreateMutex();

    SDL_SetMemoryFunctions(
        MemoryAllocNoZero,
        MemoryCAlloc,
        MemoryRealloc,
        MemoryFree
    );

    ImGui::SetAllocatorFunctions(ImGuiAlloc, ImGuiFree);
}

static void DefaultAllocatorDeinit() {
    usize n = MemoryGetAlloc();
    if (n != 0) {
        ERROR("%zu bytes leaked!", n);
    }
}

static void UpdateAllocatedBytes(usize delta) {
    int err = SDL_LockMutex(DEFAULT_ALLOCATOR.mutex);
    ASSERT(err == 0, "%s", SDL_GetError());
    DEFAULT_ALLOCATOR.allocated_bytes += delta;
    err = SDL_UnlockMutex(DEFAULT_ALLOCATOR.mutex);
    ASSERT(err == 0, "%s", SDL_GetError());
}

static void *MemoryAlloc(usize size, bool zero) {
    usize total_size = sizeof(size) + size;
    usize *result = (usize *)malloc(total_size);
    if (zero) {
        ASSERT(result, "");
        memset(result, 0, total_size);
    }
    if (result) {
        result[0] = total_size;
        result += 1;
        UpdateAllocatedBytes(total_size);
    }
    return result;
}

static void *MemoryAlloc(usize size) {
    return MemoryAlloc(size, /* zero= */ true);
}

static void *MemoryAllocNoZero(usize size) {
    return MemoryAlloc(size, /* zero= */ false);
}

static void *MemoryRealloc(void *ptr_, usize new_size) {
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

static void MemoryFree(void *ptr_) {
    usize *ptr = (usize *)ptr_;
    if (ptr) {
        ptr -= 1;
        usize total_size = ptr[0];
        UpdateAllocatedBytes(-total_size);
    }
    free(ptr);
}

static usize MemoryGetAlloc() {
    return DEFAULT_ALLOCATOR.allocated_bytes;
}

static MainLoop MAIN_LOOP = {};

static Vec2 GetInitialWindowSize();

static void MainLoopInit(MainLoop *main_loop) {
    DefaultAllocatorInit();

    if (SDL_Init(SDL_INIT_EVERYTHING & ~(SDL_INIT_TIMER | SDL_INIT_HAPTIC)) !=
        0) {
        ABORT("Failed to init SDL: %s", SDL_GetError());
    }

    OS_TASK_CHANNEL = ChannelCreate(sizeof(Task *), 1);
    MAIN_LOOP.worker_thread = SDL_CreateThread(WorkerMain, "Worker", 0);
    ASSERT(MAIN_LOOP.worker_thread, "");

    char *startup_file = 0;
    if (main_loop->argc > 1) {
        startup_file = main_loop->argv[1];
    }

    Vec2 window_size = GetInitialWindowSize();

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

    main_loop->app = AppCreate();

    if (startup_file) {
        OsLoadingFile *file = OsLoadingFileOpen(startup_file);
        if (file) {
            MaybeLoadFile(main_loop->app, file);
        }
    }

    main_loop->state = MainLoopState_Update;
}

static void MainLoopUpdate(MainLoop *main_loop) {
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

static void MainLoopShutdown(MainLoop *main_loop) {
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

static void RunMainLoop(void *arg) {
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

int main(int argc, char **argv) {
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
