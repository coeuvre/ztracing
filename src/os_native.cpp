static Vec2 GetInitialWindowSize() {
    Vec2 result = {1280, 720};
    return result;
}

enum LoadingFileState {
    LOADING_FILE_INIT,
    LOADING_FILE_NORMAL,
    LOADING_FILE_GZIP,
    LOADING_FILE_DONE,
};

struct OsLoadingFile {
    Arena *arena;
    char *path;
    isize total;
    SDL_RWops *rw;
    LoadingFileState state;
    z_stream zstream;
    u8 *zstream_buf;
    u32 zstream_buf_len;
};

static OsLoadingFile *OsLoadingFileOpen(char *path) {
    OsLoadingFile *file = 0;
    SDL_RWops *rw = SDL_RWFromFile(path, "rb");
    if (rw) {
        isize total = rw->size(rw);
        ASSERT(total >= 0, "Failed to get size of %s", path);

        Arena *arena = ArenaCreate();
        file = ArenaPushStruct(arena, OsLoadingFile);
        file->arena = arena;
        file->path = ArenaPushStr(file->arena, path);
        file->total = total;
        file->rw = rw;
    } else {
        ERROR("Failed to load file %s: %s", path, SDL_GetError());
    }
    return file;
}

static voidpf ZLibAlloc(voidpf opaque, uInt items, uInt size) {
    return MemAlloc(items * size);
}

static void ZLibFree(voidpf opaque, voidpf address) {
    MemFree(address);
}

static u32 OsLoadingFileNext(OsLoadingFile *file, u8 *buf, u32 len) {
    u32 nread = 0;
    SDL_RWops *rw = file->rw;
    for (bool need_more_read = true; need_more_read;) {
        switch (file->state) {
        case LOADING_FILE_INIT: {
            u8 header_buf[2];
            u32 header_nread = rw->read(rw, header_buf, 1, 2);
            if (header_nread == 2 && header_buf[0] == 0x1F &&
                header_buf[1] == 0x8B) {
                file->state = LOADING_FILE_GZIP;

                file->zstream = {};
                file->zstream.zalloc = ZLibAlloc;
                file->zstream.zfree = ZLibFree;

                int zret = inflateInit2(&file->zstream, MAX_WBITS | 32);
                // TODO: Error handling.
                ASSERT(zret == Z_OK, "");
                file->zstream_buf_len = 4096;
                file->zstream_buf = ArenaPushArray(file->arena, u8, file->zstream_buf_len);
            } else {
                file->state = LOADING_FILE_NORMAL;
            }
            i64 offset = rw->seek(rw, 0, RW_SEEK_SET);
            ASSERT(offset == 0, "");
        } break;

        case LOADING_FILE_NORMAL: {
            nread = rw->read(rw, buf, 1, len);
            need_more_read = false;
            if (nread == 0) {
                file->state = LOADING_FILE_DONE;
            }
        } break;

        case LOADING_FILE_GZIP: {
            z_stream *stream = &file->zstream;
            if (stream->avail_in == 0) {
                stream->avail_in =
                    rw->read(rw, file->zstream_buf, 1, file->zstream_buf_len);
                stream->next_in = file->zstream_buf;
            }

            if (stream->avail_in != 0) {
                stream->avail_out = len;
                stream->next_out = buf;
                int zret = inflate(stream, Z_NO_FLUSH);
                switch (zret) {
                case Z_OK: {
                    nread = len - stream->avail_out;
                    need_more_read = nread == 0;
                } break;

                case Z_STREAM_END: {
                    nread = len - stream->avail_out;
                    need_more_read = false;
                    file->state = LOADING_FILE_DONE;
                } break;

                default: {
                    // TODO: Error handling.
                    ABORT("inflate returned %d", zret);
                } break;
                }
            } else {
                need_more_read = false;
                file->state = LOADING_FILE_DONE;
            }
        } break;

        case LOADING_FILE_DONE: {
            need_more_read = false;
        } break;

        default: {
            UNREACHABLE;
        } break;
        }
    }
    return nread;
}

static void OsLoadingFileClose(OsLoadingFile *file) {
    int ret = file->rw->close(file->rw);
    ASSERT(ret == 0, "Failed to close file: %s", SDL_GetError());
    if (file->zstream_buf) {
        inflateEnd(&file->zstream);
    }
    ArenaDestroy(file->arena);
}
