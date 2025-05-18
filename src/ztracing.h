#ifndef ZTRACING_SRC_ZTRACING_H_
#define ZTRACING_SRC_ZTRACING_H_

#include <stddef.h>

#include "src/flick.h"
#include "src/math.h"
#include "src/string.h"

typedef struct LoadingFile {
  Str name;
  ptrdiff_t nread;
  volatile bool interrupted;

  Str (*read)(void *self);
  void (*close)(void *self);
} LoadingFile;

typedef struct App App;

App *App_Create(FL_Allocator allocator);

void App_LoadFile(App *app, LoadingFile *file);

void App_Update(App *app, Vec2 viewport_size);

void App_Destroy(App *app);

#endif  // ZTRACING_SRC_ZTRACING_H_
