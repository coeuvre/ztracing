#ifndef ZTRACING_SRC_DRAW_H_
#define ZTRACING_SRC_DRAW_H_

#include "src/math.h"
#include "src/string.h"
#include "src/types.h"

typedef struct DrawColor {
  u8 a;
  u8 r;
  u8 g;
  u8 b;
} DrawColor;

static inline DrawColor RGBA8(u8 r, u8 g, u8 b, u8 a) {
  DrawColor result;
  result.r = r;
  result.g = g;
  result.b = b;
  result.a = a;
  return result;
}

static inline DrawColor DrawColorFromHex(u32 hex) {
  DrawColor result =
      RGBA8((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF, 0xFF);
  return result;
}

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

void DrawRect(Vec2 min, Vec2 max, DrawColor color);
void DrawRectLine(Vec2 min, Vec2 max, DrawColor color, f32 thickness);

#endif  // ZTRACING_SRC_DRAW_H_
