#ifndef ZTRACING_SRC_DRAW_H_
#define ZTRACING_SRC_DRAW_H_

#include "src/math.h"
#include "src/string.h"
#include "src/types.h"

// Get the DPI scale of the screen. It's the ratio for (pixels / point).
f32 GetScreenContentScale(void);
// Get the screen size in pixel.
Vec2I GetScreenSizeInPixel(void);

typedef struct TextMetrics TextMetrics;
struct TextMetrics {
  Vec2 size;
};

TextMetrics GetTextMetricsStr8(Str8 text, f32 height);
void DrawTextStr8(Vec2 pos, Str8 text, f32 height);

void ClearDraw(void);
void PresentDraw(void);

void DrawRect(Vec2 min, Vec2 max, ColorU32 color);
void DrawRectLine(Vec2 min, Vec2 max, ColorU32 color, f32 thickness);

#endif  // ZTRACING_SRC_DRAW_H_
