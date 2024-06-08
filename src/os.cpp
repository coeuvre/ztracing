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

static Vec2 GetInitialWindowSize();
static void NotifyAppInitDone();

static SDL_LogPriority TO_SDL_LOG_PRIORITY[LogLevel_COUNT] = {
    SDL_LOG_PRIORITY_DEBUG,
    SDL_LOG_PRIORITY_INFO,
    SDL_LOG_PRIORITY_WARN,
    SDL_LOG_PRIORITY_ERROR,
    SDL_LOG_PRIORITY_CRITICAL
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
OsCreateCond() {
    SDL_cond *cond = SDL_CreateCond();
    if (!cond) {
        ABORT("Failed to create condition variable: %s", SDL_GetError());
    }
    return (OsCond *)cond;
}

static void
OsDestroyCond(OsCond *cond) {
    SDL_DestroyCond((SDL_cond *)cond);
}

static void
OsWaitCond(OsCond *cond, OsMutex *mutex) {
    int ret = SDL_CondWait((SDL_cond *)cond, (SDL_mutex *)mutex);
    if (ret != 0) {
        ABORT("Failed to wait on condition variable: %s", SDL_GetError());
    }
}

static void
OsSignal(OsCond *cond) {
    int ret = SDL_CondSignal((SDL_cond *)cond);
    if (ret != 0) {
        ABORT("Failed to singal condition variable: %s", SDL_GetError());
    }
}

static void
OsBroadcast(OsCond *cond) {
    int ret = SDL_CondBroadcast((SDL_cond *)cond);
    if (ret != 0) {
        ABORT("Failed to singal condition variable: %s", SDL_GetError());
    }
}

static OsMutex *
OsCreateMutex() {
    SDL_mutex *mutex = SDL_CreateMutex();
    if (!mutex) {
        ABORT("Failed to create mutex: %s", SDL_GetError());
    }
    return (OsMutex *)mutex;
}

static void
OsDestroyMutex(OsMutex *mutex) {
    SDL_DestroyMutex((SDL_mutex *)mutex);
}

static void
OsLockMutex(OsMutex *mutex) {
    int ret = SDL_LockMutex((SDL_mutex *)mutex);
    if (ret != 0) {
        ABORT("Failed to lock mutex: %s", SDL_GetError());
    }
}

static void
OsUnlockMutex(OsMutex *mutex) {
    int ret = SDL_UnlockMutex((SDL_mutex *)mutex);
    if (ret != 0) {
        ABORT("Failed to unlock mutex: %s", SDL_GetError());
    }
}

static Channel *OS_TASK_CHANNEL;

static void
OsDispatchTask(Task *task) {
    ASSERT(OS_TASK_CHANNEL);
    bool sent = SendToChannel(OS_TASK_CHANNEL, &task);
    ASSERT(sent);
}

static int
WorkerMain(void *data) {
    Channel *channel = OS_TASK_CHANNEL;

    Task *task;
    while (ReceiveFromChannel(channel, &task)) {
        task->func(task);

        OsLockMutex(task->mutex);
        task->done = true;
        OsBroadcast(task->cond);
        OsUnlockMutex(task->mutex);
    }

    CloseChannelRx(channel);

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
        OsCloseFile(file);
    }
}

struct {
    OsMutex *mutex;
    volatile isize allocated_bytes;
} DEFAULT_ALLOCATOR;

static void *
SDLMalloc(usize size) {
    usize total_size = sizeof(isize) + size;
    isize *ptr = (isize *)AllocateMemory(total_size);
    if (ptr) {
        ptr[0] = total_size;
        ptr += 1;
    }
    return ptr;
}

static void *
SDLCalloc(usize num, usize size) {
    usize total_size = num * size;
    void *ptr = SDLMalloc(total_size);
    if (ptr) {
        memset(ptr, 0, total_size);
    }
    return ptr;
}

static void *
SDLRealloc(void *ptr_, usize size) {
    isize *ptr = 0;
    isize old_size = 0;
    if (ptr_) {
        ptr = (isize *)ptr_ - 1;
        old_size = ptr[0];
    }

    isize new_size = sizeof(isize) + size;
    ptr = (isize *)ReallocateMemory(ptr, old_size, new_size);
    if (ptr) {
        ptr[0] = new_size;
        ptr += 1;
    }
    return ptr;
}

static void
SDLFree(void *ptr_) {
    isize *ptr = 0;
    isize size = 0;
    if (ptr_) {
        ptr = (isize *)ptr_ - 1;
        size = ptr[0];
    }
    DeallocateMemory(ptr, size);
}

static void *
ImGuiMalloc(usize size, void *user_data) {
    return SDLMalloc(size);
}

static void
ImGuiFree(void *ptr, void *user_data) {
    SDLFree(ptr);
}

static void
DefaultAllocatorInit() {
    DEFAULT_ALLOCATOR.mutex = OsCreateMutex();

    SDL_SetMemoryFunctions(SDLMalloc, SDLCalloc, SDLRealloc, SDLFree);

    ImGui::SetAllocatorFunctions(ImGuiMalloc, ImGuiFree);
}

static void
DefaultAllocatorDeinit() {
    isize n = GetAllocatedBytes();
    if (n != 0) {
        ERROR("%zu bytes leaked!", n);
    }
}

static void
UpdateAllocatedBytes(isize delta) {
    OsLockMutex(DEFAULT_ALLOCATOR.mutex);
    isize allocated_bytes = DEFAULT_ALLOCATOR.allocated_bytes;
    allocated_bytes += delta;
    DEFAULT_ALLOCATOR.allocated_bytes = allocated_bytes;
    OsUnlockMutex(DEFAULT_ALLOCATOR.mutex);
}

static void *
AllocateMemory(isize size) {
    void *result = malloc(size);
    if (result) {
        UpdateAllocatedBytes(size);
    }
    return result;
}

static void *
ReallocateMemory(void *ptr, isize old_size, isize new_size) {
    isize delta = -old_size;
    ptr = (isize *)realloc(ptr, new_size);
    if (ptr) {
        delta += new_size;
    }
    UpdateAllocatedBytes(delta);
    return ptr;
}

static void
DeallocateMemory(void *ptr, isize size) {
    free(ptr);
    UpdateAllocatedBytes(-size);
}

static isize
GetAllocatedBytes() {
    return DEFAULT_ALLOCATOR.allocated_bytes;
}

static MainLoop MAIN_LOOP = {};

static void
MainLoopInit(MainLoop *main_loop) {
    DefaultAllocatorInit();

    if (SDL_Init(SDL_INIT_EVERYTHING & ~(SDL_INIT_TIMER | SDL_INIT_HAPTIC)) !=
        0) {
        ABORT("Failed to init SDL: %s", SDL_GetError());
    }

    OS_TASK_CHANNEL = CreateChannel(sizeof(Task *), 0);
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

        ImFontConfig font_cfg;
        font_cfg.FontDataOwnedByAtlas = false;
        io->Fonts->AddFontFromMemoryTTF(
            JetBrainsMono_Regular_ttf,
            JetBrainsMono_Regular_ttf_len,
            15.0,
            &font_cfg
        );

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
        OsLoadingFile *file = OsOpenFile(startup_file);
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
                OsLoadingFile *file = OsOpenFile(path);
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

    CloseChannelTx(OS_TASK_CHANNEL);
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
