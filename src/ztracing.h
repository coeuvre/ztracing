#ifndef ZTRACING_SRC_ZTRACING_H_
#define ZTRACING_SRC_ZTRACING_H_

#include "src/json.h"
#include "src/types.h"

typedef struct ZtracingFile {
  usize nread;

  Str8 (*read)(void *self);
  void (*close)(void *self);
} ZtracingFile;

void ztracing_load_file(ZtracingFile *file);

void ztracing_update(void);

#endif  // ZTRACING_SRC_ZTRACING_H_
