static Vec2 GetInitialWindowSize() {
  Vec2 result = {1280, 720};
  return result;
}

static void NotifyAppInitDone() {}

static f32 GetDpiScale(SDL_Window *window) {
  f32 scale = 1.0;

  f32 dpi = 0.0;
  int display_index = SDL_GetWindowDisplayIndex(window);
  if (SDL_GetDisplayDPI(display_index, &dpi, 0, 0) == 0) {
    scale = dpi / 96.0f;
  }

  return scale;
}

struct OsLoadingFile {
  Arena arena;
  Buffer path;
  isize total;
  SDL_RWops *rw;
};

static OsLoadingFile *OsOpenFile(char *path) {
  OsLoadingFile *file = 0;
  SDL_RWops *rw = SDL_RWFromFile(path, "rb");
  if (rw) {
    isize total = rw->size(rw);
    if (total < 0) {
      ABORT("Failed to get size of %s", path);
    }

    file = BootstrapPushStruct(OsLoadingFile, arena);
    Buffer src = {};
    src.data = (u8 *)path;
    src.size = strlen(path);
    file->path = PushBuffer(&file->arena, src);
    file->total = total;
    file->rw = rw;
  } else {
    ERROR("Failed to load file %s: %s", path, SDL_GetError());
  }
  return file;
}

static u32 OsReadFile(OsLoadingFile *file, u8 *buf, u32 len) {
  u32 nread = file->rw->read(file->rw, buf, 1, len);
  return nread;
}

static void OsCloseFile(OsLoadingFile *file) {
  int ret = file->rw->close(file->rw);
  if (ret != 0) {
    ABORT("Failed to close file: %s", SDL_GetError());
  }
  ClearArena(&file->arena);
}

static Buffer OsGetFilePath(OsLoadingFile *file) { return file->path; }
