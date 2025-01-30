#include "src/draw.h"

#include "src/math.h"
#include "src/types.h"

void stroke_rect(Vec2 min, Vec2 max, ColorU32 color, f32 thickness) {
  fill_rect(min, v2(max.x, min.y + thickness), color);
  fill_rect(v2(min.x, min.y + thickness), v2(min.x + thickness, max.y), color);
  fill_rect(v2(max.x - thickness, min.y + thickness), v2(max.x, max.y), color);
  fill_rect(v2(min.x + thickness, max.y - thickness),
            v2(max.x - thickness, max.y), color);
}
