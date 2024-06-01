struct Chunk {
    u8 *buf;
    u32 len;
};

struct OsLoadingFile {
    char *path;
    usize total;
    Channel *channel;

    Chunk chunk;
    usize offset;
};

static OsLoadingFile *OsLoadingFileOpen(char *path) {
    return 0;
}

static u32 OsLoadingFileNext(OsLoadingFile *file, u8 *buf, u32 len) {
    int nread = 0;

    if (!file->chunk.buf) {
        ChannelRecv(file->channel, &file->chunk);
    }

    if (file->chunk.buf) {
        u32 remaining = file->chunk.len - file->offset;
        nread = MIN(remaining, len);
        ASSERT(nread > 0);

        CopyMemory(buf, file->chunk.buf + file->offset, nread);
        file->offset += nread;

        if (file->offset == file->chunk.len) {
            DeallocateMemory(file->chunk.buf);
            file->chunk = {};
            file->offset = 0;
        }
    }

    return nread;
}

static void OsLoadingFileDestroy(OsLoadingFile *file) {
    ASSERT(!file->chunk.buf);
    DeallocateMemory(file->path);
    DeallocateMemory(file);
}

static void OsLoadingFileClose(OsLoadingFile *file) {
    if (ChannelCloseRx(file->channel)) {
        OsLoadingFileDestroy(file);
    }
}

static char *OsLoadingFileGetPath(OsLoadingFile *file) {
    return file->path;
}

#include <emscripten.h>

EM_JS(int, GetCanvasWidth, (), { return Module.canvas.width; });
EM_JS(int, GetCanvasHeight, (), { return Module.canvas.height; });
EM_JS(void, AppSetupResolve, (), {
    if (Module.AppSetupResolve) {
        Module.AppSetupResolve();
    } else {
        Module.AppSetupResolve = true;
    }
});

static Vec2 GetInitialWindowSize() {
    Vec2 result = {};
    result.x = GetCanvasWidth();
    result.y = GetCanvasHeight();
    return result;
}

static void NotifyAppInitDone() {
    AppSetupResolve();
}

EMSCRIPTEN_KEEPALIVE
extern "C" void *AppAllocateMemory(usize size) {
    void *result = AllocateMemory(size);
    return result;
}

EMSCRIPTEN_KEEPALIVE
extern "C" void AppSetWindowSize(int width, int height) {
    SDL_SetWindowSize(MAIN_LOOP.window, width, height);
}

EMSCRIPTEN_KEEPALIVE
extern "C" bool AppOnLoadBegin(char *path, usize total) {
    bool result = !MAIN_LOOP.loading_file && AppCanLoadFile(MAIN_LOOP.app);

    if (result) {
        OsLoadingFile *file =
            (OsLoadingFile *)AllocateMemory(sizeof(OsLoadingFile));
        file->path = CopyString(path);
        file->total = total;
        file->channel = ChannelCreate(sizeof(Chunk), 1);

        MAIN_LOOP.loading_file = file;
        AppLoadFile(MAIN_LOOP.app, file);
    }

    return result;
}

EMSCRIPTEN_KEEPALIVE
extern "C" bool AppOnLoadChunk(u8 *buf, u32 len) {
    OsLoadingFile *file = MAIN_LOOP.loading_file;

    Chunk chunk = {buf, len};
    bool result = ChannelSend(file->channel, &chunk);
    if (!result) {
        DeallocateMemory(buf);
    }

    return result;
}

EMSCRIPTEN_KEEPALIVE
extern "C" void AppOnLoadEnd() {
    OsLoadingFile *file = MAIN_LOOP.loading_file;
    ASSERT(file);

    if (ChannelCloseTx(file->channel)) {
        OsLoadingFileDestroy(file);
    }

    MAIN_LOOP.loading_file = 0;
}
