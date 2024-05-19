struct OsLoadingFile {
    SDL_mutex *mutex;
    SDL_cond *cond;
    bool eof;
    bool closed;
    u8 *buf;
    u32 idx;
    u32 len;
};

static OsLoadingFile *OsLoadingFileOpen(char *path) {
    return 0;
}

static u32 OsLoadingFileNext(OsLoadingFile *file, u8 *buf, u32 len) {
    int nread = 0;

    int err = SDL_LockMutex(file->mutex);
    ASSERT(err == 0, "%s", SDL_GetError());
    while (!(file->eof || file->buf)) {
        err = SDL_CondWait(file->cond, file->mutex);
        ASSERT(err == 0, "%s", SDL_GetError());
    }
    if (file->buf) {
        u32 remaining = file->len - file->idx;
        nread = MIN(remaining, len);
        ASSERT(nread > 0, "");

        memcpy(buf, &file->buf[file->idx], nread);
        file->idx += nread;

        if (file->idx == file->len) {
            MemFree(file->buf);
            file->buf = 0;
            file->idx = 0;
            file->len = 0;
            err = SDL_CondSignal(file->cond);
            ASSERT(err == 0, "%s", SDL_GetError());
        }
    }
    err = SDL_UnlockMutex(file->mutex);
    ASSERT(err == 0, "%s", SDL_GetError());

    return nread;
}

static void OsLoadingFileClose(OsLoadingFile *file) {
    int err = SDL_LockMutex(file->mutex);
    ASSERT(err == 0, "%s", SDL_GetError());
    if (file->buf) {
        MemFree(file->buf);
        file->buf = 0;
        file->idx = 0;
        file->len = 0;
        err = SDL_CondSignal(file->cond);
        ASSERT(err == 0, "%s", SDL_GetError());
    }

    file->closed = true;
    err = SDL_CondSignal(file->cond);
    ASSERT(err == 0, "%s", SDL_GetError());

    err = SDL_UnlockMutex(file->mutex);
    ASSERT(err == 0, "%s", SDL_GetError());
}

#include <emscripten.h>

EM_JS(int, GetCanvasWidth, (), { return Module.canvas.width; });
EM_JS(int, GetCanvasHeight, (), { return Module.canvas.height; });

static Vec2 GetInitialWindowSize() {
    Vec2 result = {};
    result.x = GetCanvasWidth();
    result.y = GetCanvasHeight();
    return result;
}

EMSCRIPTEN_KEEPALIVE
extern "C" void *AppMemAlloc(usize size) {
    void *result = MemAlloc(size);
    return result;
}

EMSCRIPTEN_KEEPALIVE
extern "C" void AppSetWindowSize(int width, int height) {
    SDL_SetWindowSize(MAIN_LOOP.window, width, height);
}

EMSCRIPTEN_KEEPALIVE
extern "C" bool AppCanLoadFile() {
    return !MAIN_LOOP.loading_file && AppCanLoadFile(MAIN_LOOP.app);
}

EMSCRIPTEN_KEEPALIVE
extern "C" void AppOnLoadBegin(char *path, isize total) {
    ASSERT(AppCanLoadFile(), "");

    OsLoadingFile *file = (OsLoadingFile *)MemAlloc(sizeof(OsLoadingFile));
    ASSERT(file, "");
    *file = {};
    file->mutex = SDL_CreateMutex();
    ASSERT(file->mutex, "%s", SDL_GetError());
    file->cond = SDL_CreateCond();
    ASSERT(file->cond, "%s", SDL_GetError());

    MAIN_LOOP.loading_file = file;
    AppLoadFile(MAIN_LOOP.app, file);
}

EMSCRIPTEN_KEEPALIVE
extern "C" bool AppCanLoadChunk() {
    bool result = false;
    OsLoadingFile *file = MAIN_LOOP.loading_file;
    if (file) {
        int err = SDL_LockMutex(file->mutex);
        ASSERT(err == 0, "%s", SDL_GetError());
        result = !file->closed;
        err = SDL_UnlockMutex(file->mutex);
        ASSERT(err == 0, "%s", SDL_GetError());
    }
    return result;
}

EMSCRIPTEN_KEEPALIVE
extern "C" void AppOnLoadChunk(u8 *buf, u32 len) {
    ASSERT(AppCanLoadChunk(), "");
    OsLoadingFile *file = MAIN_LOOP.loading_file;
    int err = SDL_LockMutex(file->mutex);
    ASSERT(err == 0, "%s", SDL_GetError());
    while (file->buf) {
        err = SDL_CondWait(file->cond, file->mutex);
        ASSERT(err == 0, "%s", SDL_GetError());
    }
    file->buf = buf;
    file->idx = 0;
    file->len = len;
    err = SDL_CondSignal(file->cond);
    ASSERT(err == 0, "%s", SDL_GetError());
    err = SDL_UnlockMutex(file->mutex);
    ASSERT(err == 0, "%s", SDL_GetError());
}

EMSCRIPTEN_KEEPALIVE
extern "C" void AppOnLoadEnd() {
    OsLoadingFile *file = MAIN_LOOP.loading_file;
    ASSERT(file, "");
    MAIN_LOOP.loading_file = 0;

    int err = SDL_LockMutex(file->mutex);
    ASSERT(err == 0, "%s", SDL_GetError());

    while (file->buf) {
        err = SDL_CondWait(file->cond, file->mutex);
        ASSERT(err == 0, "%s", SDL_GetError());
    }

    file->eof = true;
    err = SDL_CondSignal(file->cond);
    ASSERT(err == 0, "%s", SDL_GetError());

    while (!file->closed) {
        err = SDL_CondWait(file->cond, file->mutex);
        ASSERT(err == 0, "%s", SDL_GetError());
    }

    ASSERT(!file->buf, "");

    ASSERT(err == 0, "%s", SDL_GetError());
    err = SDL_UnlockMutex(file->mutex);
    ASSERT(err == 0, "%s", SDL_GetError());

    SDL_DestroyCond(file->cond);
    SDL_DestroyMutex(file->mutex);
    MemFree(file);
}
