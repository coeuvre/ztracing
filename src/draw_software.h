#ifndef ZTRACING_SRC_DRAW_SOFTWARE_H_
#define ZTRACING_SRC_DRAW_SOFTWARE_H_

#include "src/math.h"
#include "src/memory.h"
typedef struct Bitmap Bitmap;
struct Bitmap {
  u32 *pixels;
  Vec2I size;
};

Bitmap *InitSoftwareRenderer(void);
void ResizeSoftwareRenderer(Vec2I size);

#endif  // ZTRACING_SRC_DRAW_SOFTWARE_H_
