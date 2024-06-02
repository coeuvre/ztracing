#include "os_impl.h"

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

OsLoadingFile *
OsOpenFile(char *path) {
    return 0;
}

u32
OsReadFile(OsLoadingFile *file, u8 *buf, u32 len) {
    int nread = 0;

    if (!file->chunk.buf) {
        ReceiveFromChannel(file->channel, &file->chunk);
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

static void
OsLoadingFileDestroy(OsLoadingFile *file) {
    if (file->chunk.buf) {
        DeallocateMemory(file->chunk.buf);
    }
    DeallocateMemory(file->path);
    DeallocateMemory(file);
}

void
OsCloseFile(OsLoadingFile *file) {
    if (CloseChannelRx(file->channel)) {
        OsLoadingFileDestroy(file);
    }
}

char *
OsGetFilePath(OsLoadingFile *file) {
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

Vec2
GetInitialWindowSize() {
    Vec2 result = {};
    result.x = GetCanvasWidth();
    result.y = GetCanvasHeight();
    return result;
}

void
NotifyAppInitDone() {
    AppSetupResolve();
}

EMSCRIPTEN_KEEPALIVE
extern "C" void *
AppAllocateMemory(usize size) {
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
AppOnLoadBegin(char *path, usize total) {
    bool result = !MAIN_LOOP.loading_file && AppCanLoadFile(MAIN_LOOP.app);

    if (result) {
        OsLoadingFile *file =
            (OsLoadingFile *)AllocateMemory(sizeof(OsLoadingFile));
        file->path = CopyString(path);
        file->total = total;
        file->channel = CreateChannel(sizeof(Chunk), 1);

        MAIN_LOOP.loading_file = file;
        AppLoadFile(MAIN_LOOP.app, file);
    }

    return result;
}

EMSCRIPTEN_KEEPALIVE
extern "C" bool
AppOnLoadChunk(u8 *buf, u32 len) {
    OsLoadingFile *file = MAIN_LOOP.loading_file;

    Chunk chunk = {buf, len};
    bool result = SendToChannel(file->channel, &chunk);
    if (!result) {
        DeallocateMemory(buf);
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
