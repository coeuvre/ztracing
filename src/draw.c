#include "src/draw.h"

#include "src/math.h"
#include "src/types.h"
#include "src/ui.h"

void stroke_rect(Vec2 min, Vec2 max, UIColor color, f32 thickness) {
  fill_rect(min, vec2(max.x, min.y + thickness), color);
  fill_rect(vec2(min.x, min.y + thickness), vec2(min.x + thickness, max.y),
            color);
  fill_rect(vec2(max.x - thickness, min.y + thickness), vec2(max.x, max.y),
            color);
  fill_rect(vec2(min.x + thickness, max.y - thickness),
            vec2(max.x - thickness, max.y), color);
}
