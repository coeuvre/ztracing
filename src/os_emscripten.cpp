struct OsLoadingFile {
    char *path;
    usize total;
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

    ASSERT(!file->closed, "");

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

static void MaybeDestroyOsLoadingFile(OsLoadingFile *file) {
    bool destroy = false;

    int err = SDL_LockMutex(file->mutex);
    ASSERT(err == 0, "%s", SDL_GetError());

    destroy = file->closed && file->eof;

    err = SDL_UnlockMutex(file->mutex);
    ASSERT(err == 0, "%s", SDL_GetError());

    if (destroy) {
        ASSERT(!file->buf, "");
        SDL_DestroyCond(file->cond);
        SDL_DestroyMutex(file->mutex);
        MemFree(file->path);
        MemFree(file);
    }
}

static void OsLoadingFileClose(OsLoadingFile *file) {
    int err = SDL_LockMutex(file->mutex);
    ASSERT(err == 0, "%s", SDL_GetError());

    ASSERT(!file->closed, "");
    file->closed = true;
    if (file->buf) {
        MemFree(file->buf);
        file->buf = 0;
        file->idx = 0;
        file->len = 0;
    }

    err = SDL_CondSignal(file->cond);
    ASSERT(err == 0, "%s", SDL_GetError());

    err = SDL_UnlockMutex(file->mutex);
    ASSERT(err == 0, "%s", SDL_GetError());

    MaybeDestroyOsLoadingFile(file);
}

static char *OsLoadingFileGetPath(OsLoadingFile *file) {
    return file->path;
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
extern "C" bool AppOnLoadBegin(char *path, usize total) {
    bool result = !MAIN_LOOP.loading_file && AppCanLoadFile(MAIN_LOOP.app);

    if (result) {
        OsLoadingFile *file = (OsLoadingFile *)MemAlloc(sizeof(OsLoadingFile));
        ASSERT(file, "");
        *file = {};
        file->path = MemStrDup(path);
        ASSERT(file->path, "");
        file->total = total;
        file->mutex = SDL_CreateMutex();
        ASSERT(file->mutex, "%s", SDL_GetError());
        file->cond = SDL_CreateCond();
        ASSERT(file->cond, "%s", SDL_GetError());

        MAIN_LOOP.loading_file = file;
        AppLoadFile(MAIN_LOOP.app, file);
    }

    return result;
}

EMSCRIPTEN_KEEPALIVE
extern "C" bool AppOnLoadChunk(u8 *buf, u32 len) {
    bool result = true;
    OsLoadingFile *file = MAIN_LOOP.loading_file;
    int err = SDL_LockMutex(file->mutex);
    ASSERT(err == 0, "%s", SDL_GetError());

    while (file->buf) {
        err = SDL_CondWait(file->cond, file->mutex);
        ASSERT(err == 0, "%s", SDL_GetError());
    }

    if (!file->closed) {
        file->buf = buf;
        file->idx = 0;
        file->len = len;
        err = SDL_CondSignal(file->cond);
        ASSERT(err == 0, "%s", SDL_GetError());
    } else {
        result = false;
    }

    err = SDL_UnlockMutex(file->mutex);
    ASSERT(err == 0, "%s", SDL_GetError());

    if (!result) {
        MemFree(buf);
    }

    return result;
}

EMSCRIPTEN_KEEPALIVE
extern "C" void AppOnLoadEnd() {
    OsLoadingFile *file = MAIN_LOOP.loading_file;
    ASSERT(file, "");

    int err = SDL_LockMutex(file->mutex);
    ASSERT(err == 0, "%s", SDL_GetError());

    file->eof = true;

    err = SDL_CondSignal(file->cond);
    ASSERT(err == 0, "%s", SDL_GetError());

    err = SDL_UnlockMutex(file->mutex);
    ASSERT(err == 0, "%s", SDL_GetError());

    MaybeDestroyOsLoadingFile(file);

    MAIN_LOOP.loading_file = 0;
}
