#ifndef ZTRACING_SRC_DRAW_H_
#define ZTRACING_SRC_DRAW_H_

#include "src/math.h"
#include "src/string.h"
#include "src/types.h"

Vec2I GetCanvasSize(void);

typedef struct TextMetrics TextMetrics;
struct TextMetrics {
  Vec2 size;
};

TextMetrics GetTextMetricsStr8(Str8 text, f32 height);
void DrawTextStr8(Vec2 pos, Str8 text, f32 height);

void ClearCanvas(void);
void DrawRect(Vec2 min, Vec2 max, u32 color);
void DrawRectLine(Vec2 min, Vec2 max, u32 color, f32 thickness);

#endif  // ZTRACING_SRC_DRAW_H_
