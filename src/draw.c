#include "src/draw.h"

#include "src/math.h"
#include "src/types.h"

void DrawRectLine(Vec2 min, Vec2 max, DrawColor color, f32 thickness) {
  DrawRect(min, V2(max.x, min.y + thickness), color);
  DrawRect(V2(min.x, min.y + thickness), V2(min.x + thickness, max.y), color);
  DrawRect(V2(max.x - thickness, min.y + thickness), V2(max.x, max.y), color);
  DrawRect(V2(min.x + thickness, max.y - thickness),
           V2(max.x - thickness, max.y), color);
}
