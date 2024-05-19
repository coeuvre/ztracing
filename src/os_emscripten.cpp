struct OsLoadingFile {
    SDL_mutex *mutex;
    SDL_cond *cond;
    bool eof;
    bool closed;
    u8 *buf;
    u32 idx;
    u32 len;
};

static OsLoadingFile *os_loading_file_open(char *path) {
    return 0;
}

static u32 os_loading_file_next(OsLoadingFile *file, u8 *buf, u32 len) {
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
            memory_free(file->buf);
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

static void os_loading_file_close(OsLoadingFile *file) {
    int err = SDL_LockMutex(file->mutex);
    ASSERT(err == 0, "%s", SDL_GetError());
    if (file->buf) {
        memory_free(file->buf);
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

EM_JS(int, get_canvas_width, (), { return Module.canvas.width; });
EM_JS(int, get_canvas_height, (), { return Module.canvas.height; });

static Vec2 get_initial_window_size() {
    Vec2 result = {};
    result.x = get_canvas_width();
    result.y = get_canvas_height();
    return result;
}

EMSCRIPTEN_KEEPALIVE
extern "C" void *app_memory_alloc(usize size) {
    void *result = memory_alloc(size);
    return result;
}

EMSCRIPTEN_KEEPALIVE
extern "C" void app_set_window_size(int width, int height) {
    SDL_SetWindowSize(MAIN_LOOP.window, width, height);
}

EMSCRIPTEN_KEEPALIVE
extern "C" bool app_accept_accept_load() {
    return ztracing_accept_load(&MAIN_LOOP.app);
}

EMSCRIPTEN_KEEPALIVE
extern "C" void app_on_load_begin(char *path, isize total) {
    OsLoadingFile *file = (OsLoadingFile *)memory_alloc(sizeof(OsLoadingFile));
    *file = {};
    file->mutex = SDL_CreateMutex();
    ASSERT(file->mutex, "%s", SDL_GetError());
    file->cond = SDL_CreateCond();
    ASSERT(file->cond, "%s", SDL_GetError());
    ztracing_load_file(&MAIN_LOOP.app, file);
}

EMSCRIPTEN_KEEPALIVE
extern "C" bool app_accept_load_chunk() {
    bool result = false;
    OsLoadingFile *file = ztracing_get_loading_file(&MAIN_LOOP.app);
    if (file) {
        int err = SDL_LockMutex(file->mutex);
        ASSERT(err == 0, "%s", SDL_GetError());
        result = file->closed;
        err = SDL_UnlockMutex(file->mutex);
        ASSERT(err == 0, "%s", SDL_GetError());
    }
    return result;
}

EMSCRIPTEN_KEEPALIVE
extern "C" void app_on_load_chunk(u8 *buf, u32 len) {
    OsLoadingFile *file = ztracing_get_loading_file(&MAIN_LOOP.app);
    if (file) {
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
}

EMSCRIPTEN_KEEPALIVE
extern "C" void app_on_load_end() {
    OsLoadingFile *file = ztracing_get_loading_file(&MAIN_LOOP.app);
    if (file) {
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
        memory_free(file);
    }
}
