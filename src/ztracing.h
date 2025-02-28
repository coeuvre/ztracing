#ifndef ZTRACING_SRC_ZTRACING_H_
#define ZTRACING_SRC_ZTRACING_H_

#include "src/json.h"
#include "src/types.h"

typedef struct ZFile {
  Str8 name;
  usize nread;
  volatile bool interrupted;

  Str8 (*read)(void *self);
  void (*close)(void *self);
} ZFile;

void z_load_file(ZFile *file);

void z_update(void);
void z_quit(void);

#endif  // ZTRACING_SRC_ZTRACING_H_
