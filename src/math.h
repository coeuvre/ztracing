#ifndef ZTRACING_SRC_MATH_H_
#define ZTRACING_SRC_MATH_H_

#include <float.h>
#include <math.h>

#include "src/assert.h"
#include "src/types.h"

#define F32_INFINITY ((f32)(INFINITY))

static inline f32 MaxF32(f32 a, f32 b) {
  f32 result = MAX(a, b);
  return result;
}

static inline f32 MinF32(f32 a, f32 b) {
  f32 result = MIN(a, b);
  return result;
}

static inline b32 IsNaNF32(f32 a) {
  b32 result = isnanf(a);
  return result;
}

typedef struct Vec2 {
  f32 x;
  f32 y;
} Vec2;

static inline f32 GetItemVec2(Vec2 v, usize index) {
  DEBUG_ASSERT(index < 2);
  f32 result = ((f32 *)&v)[index];
  return result;
}

static inline void SetItemVec2(Vec2 *v, usize index, f32 value) {
  DEBUG_ASSERT(index < 2);
  ((f32 *)v)[index] = value;
}

static inline Vec2 V2(f32 x, f32 y) {
  Vec2 result = {x, y};
  return result;
}

static inline b32 IsEqualVec2(Vec2 a, Vec2 b) {
  b32 result = a.x == b.x && a.y == b.y;
  return result;
}

static inline Vec2 Vec2FromArray(f32 v[2]) {
  Vec2 result = {v[0], v[1]};
  return result;
}

static inline Vec2 AddVec2(Vec2 a, Vec2 b) {
  Vec2 result;
  result.x = a.x + b.x;
  result.y = a.y + b.y;
  return result;
}

static inline Vec2 SubVec2(Vec2 a, Vec2 b) {
  Vec2 result;
  result.x = a.x - b.x;
  result.y = a.y - b.y;
  return result;
}

static inline Vec2 MulVec2(Vec2 a, f32 b) {
  Vec2 result;
  result.x = a.x * b;
  result.y = a.y * b;
  return result;
}

static inline Vec2 MinVec2(Vec2 a, Vec2 b) {
  Vec2 result;
  result.x = MinF32(a.x, b.x);
  result.y = MinF32(a.y, b.y);
  return result;
}

static inline Vec2 MaxVec2(Vec2 a, Vec2 b) {
  Vec2 result;
  result.x = MaxF32(a.x, b.x);
  result.y = MaxF32(a.y, b.y);
  return result;
}

typedef struct Vec2I {
  i32 x;
  i32 y;
} Vec2I;

static inline Vec2I Vec2IFromVec2(Vec2 value) {
  Vec2I result = {(i32)value.x, (i32)value.y};
  return result;
}

static inline b32 IsEqualVec2I(Vec2I a, Vec2I b) {
  b32 result = a.x == b.x && a.y == b.y;
  return result;
}

static inline Vec2I NegVec2I(Vec2I a) {
  Vec2I result = {-a.x, -a.y};
  return result;
}

static inline Vec2I AddVec2I(Vec2I a, Vec2I b) {
  Vec2I result = {a.x + b.x, a.y + b.y};
  return result;
}

static inline Vec2I SubVec2I(Vec2I a, Vec2I b) {
  Vec2I result = {a.x - b.x, a.y - b.y};
  return result;
}

static inline Vec2I MaxVec2I(Vec2I a, Vec2I b) {
  Vec2I result = {MAX(a.x, b.x), MAX(a.y, b.y)};
  return result;
}

static inline Vec2I MinVec2I(Vec2I a, Vec2I b) {
  Vec2I result = {MIN(a.x, b.x), MIN(a.y, b.y)};
  return result;
}

static inline Vec2 Vec2FromVec2I(Vec2I value) {
  Vec2 result = {(f32)value.x, (f32)value.y};
  return result;
}

typedef struct Vec4 {
  f32 x;
  f32 y;
  f32 z;
  f32 w;
} Vec4;

typedef struct ColorU32 {
  u8 a;
  u8 r;
  u8 g;
  u8 b;
} ColorU32;

static inline ColorU32 ColorU32FromRGBA(u8 r, u8 g, u8 b, u8 a) {
  ColorU32 result;
  result.r = r;
  result.g = g;
  result.b = b;
  result.a = a;
  return result;
}

static inline ColorU32 ColorU32FromHex(u32 hex) {
  ColorU32 result =
      ColorU32FromRGBA((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF, 0xFF);
  return result;
}

typedef enum Axis2 {
  kAxis2X,
  kAxis2Y,
  kAxis2Count,
} Axis2;

typedef struct Rect2 {
  Vec2 min;
  Vec2 max;
} Rect2;

static inline b32 ContainsF32IncludingEnd(f32 val, f32 begin, f32 end) {
  b32 result = begin <= val && val <= end;
  return result;
}

static inline b32 ContainsF32(f32 val, f32 begin, f32 end) {
  b32 result = begin <= val && val < end;
  return result;
}

static inline b32 ContainsVec2(Vec2 val, Vec2 begin, Vec2 end) {
  b32 result =
      ContainsF32(val.x, begin.x, end.x) && ContainsF32(val.y, begin.y, end.y);
  return result;
}

static inline b32 ContainsVec2IncludingEnd(Vec2 val, Vec2 begin, Vec2 end) {
  b32 result = ContainsF32IncludingEnd(val.x, begin.x, end.x) &&
               ContainsF32IncludingEnd(val.y, begin.y, end.y);
  return result;
}

// Clamp value in range [min, max]
static inline f32 ClampF32(f32 value, f32 min, f32 max) {
  f32 result = MinF32(MaxF32(value, min), max);
  return result;
}

static inline i32 ClampI32(i32 value, i32 min, i32 max) {
  i32 result = MAX(value, min);
  result = MIN(value, max);
  return result;
}

static inline Vec2 ClampVec2(Vec2 value, Vec2 min, Vec2 max) {
  Vec2 result = {ClampF32(value.x, min.x, max.x),
                 ClampF32(value.y, min.y, max.y)};
  return result;
}

static inline Vec2I ClampVec2I(Vec2I value, Vec2I min, Vec2I max) {
  Vec2I result = {ClampI32(value.x, min.x, max.x),
                  ClampI32(value.y, min.y, max.y)};
  return result;
}

static inline f32 RoundF32(f32 value) {
  f32 result = roundf(value);
  return result;
}

static inline Vec2 RoundVec2(Vec2 value) {
  Vec2 result = {RoundF32(value.x), RoundF32(value.y)};
  return result;
}

static inline f32 FloorF32(f32 value) {
  f32 result = floorf(value);
  return result;
}

#endif  // ZTRACING_SRC_MATH_H_
