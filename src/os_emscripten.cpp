struct OsLoadingFile {
    Arena arena;
    Buffer path;
    isize total;
    Channel *channel;

    Buffer chunk;
    isize offset;
};

static OsLoadingFile *
OsOpenFile(char *path) {
    return 0;
}

static u32
OsReadFile(OsLoadingFile *file, u8 *buf, u32 len) {
    int nread = 0;

    if (!file->chunk.data) {
        ReceiveFromChannel(file->channel, &file->chunk);
    }

    if (file->chunk.data) {
        u32 remaining = file->chunk.size - file->offset;
        nread = MIN(remaining, len);
        ASSERT(nread > 0);

        CopyMemory(buf, file->chunk.data + file->offset, nread);
        file->offset += nread;

        if (file->offset == file->chunk.size) {
            DeallocateMemory(file->chunk.data, file->chunk.size);
            file->chunk = {};
            file->offset = 0;
        }
    }

    return nread;
}

static void
OsLoadingFileDestroy(OsLoadingFile *file) {
    if (file->chunk.data) {
        DeallocateMemory(file->chunk.data, file->chunk.size);
    }
    ClearArena(&file->arena);
}

static void
OsCloseFile(OsLoadingFile *file) {
    if (CloseChannelRx(file->channel)) {
        OsLoadingFileDestroy(file);
    }
}

static Buffer
OsGetFilePath(OsLoadingFile *file) {
    return file->path;
}

EM_JS(int, GetCanvasWidth, (), { return Module.canvas.width; });
EM_JS(int, GetCanvasHeight, (), { return Module.canvas.height; });
EM_JS(void, AppSetupResolve, (), {
    if (Module.AppSetupResolve) {
        Module.AppSetupResolve();
    } else {
        Module.AppSetupResolve = true;
    }
});

static Vec2
GetInitialWindowSize() {
    Vec2 result = {};
    result.x = GetCanvasWidth();
    result.y = GetCanvasHeight();
    return result;
}

static void
NotifyAppInitDone() {
    AppSetupResolve();
}

EMSCRIPTEN_KEEPALIVE
extern "C" void *
AppAllocateMemory(isize size) {
    void *result = AllocateMemory(size);
    return result;
}

EMSCRIPTEN_KEEPALIVE
extern "C" void
AppSetWindowSize(int width, int height) {
    SDL_SetWindowSize(MAIN_LOOP.window, width, height);
}

EMSCRIPTEN_KEEPALIVE
extern "C" bool
AppOnLoadBegin(char *path, isize total) {
    bool result = !MAIN_LOOP.loading_file && AppCanLoadFile(MAIN_LOOP.app);

    if (result) {
        OsLoadingFile *file = BootstrapPushStruct(OsLoadingFile, arena);
        Buffer src = {};
        src.data = (u8 *)path;
        src.size = strlen(path);
        file->path = PushBuffer(&file->arena, src);
        file->total = total;
        file->channel = CreateChannel(sizeof(Buffer), 0);

        MAIN_LOOP.loading_file = file;
        AppLoadFile(MAIN_LOOP.app, file);
    }

    return result;
}

EMSCRIPTEN_KEEPALIVE
extern "C" bool
AppOnLoadChunk(u8 *buf, isize len) {
    OsLoadingFile *file = MAIN_LOOP.loading_file;

    Buffer chunk = {};
    chunk.data = buf;
    chunk.size = len;
    bool result = SendToChannel(file->channel, &chunk);
    if (!result) {
        DeallocateMemory(chunk.data, chunk.size);
    }

    return result;
}

EMSCRIPTEN_KEEPALIVE
extern "C" void
AppOnLoadEnd() {
    OsLoadingFile *file = MAIN_LOOP.loading_file;
    ASSERT(file);

    if (CloseChannelTx(file->channel)) {
        OsLoadingFileDestroy(file);
    }

    MAIN_LOOP.loading_file = 0;
}
