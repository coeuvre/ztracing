static Vec2 GetInitialWindowSize() {
    Vec2 result = {1280, 720};
    return result;
}

static void NotifyAppInitDone() {}

struct OsLoadingFile {
    char *path;
    usize total;
    SDL_RWops *rw;
};

static OsLoadingFile *OsLoadingFileOpen(char *path) {
    OsLoadingFile *file = 0;
    SDL_RWops *rw = SDL_RWFromFile(path, "rb");
    if (rw) {
        isize total = rw->size(rw);
        ASSERT(total >= 0, "Failed to get size of %s", path);

        file = (OsLoadingFile *)AllocateMemory(sizeof(OsLoadingFile));
        file->path = CopyString(path);
        file->total = total;
        file->rw = rw;
    } else {
        ERROR("Failed to load file %s: %s", path, SDL_GetError());
    }
    return file;
}

static u32 OsLoadingFileNext(OsLoadingFile *file, u8 *buf, u32 len) {
    u32 nread = file->rw->read(file->rw, buf, 1, len);
    return nread;
}

static void OsLoadingFileClose(OsLoadingFile *file) {
    int ret = file->rw->close(file->rw);
    ASSERT(ret == 0, "Failed to close file: %s", SDL_GetError());
    DeallocateMemory(file->path);
    DeallocateMemory(file);
}

static char *OsLoadingFileGetPath(OsLoadingFile *file) {
    return file->path;
}
