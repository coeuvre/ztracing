#ifndef ZTRACING_SRC_MATH_H_
#define ZTRACING_SRC_MATH_H_

#include <math.h>

#include "src/types.h"

typedef union Vec2 Vec2;
union Vec2 {
  struct {
    f32 x;
    f32 y;
  };
  f32 v[2];
};

typedef union Vec2I Vec2I;
union Vec2I {
  struct {
    i32 x;
    i32 y;
  };
  i32 v[2];
};

static inline Vec2I Vec2IFromVec2(Vec2 value) {
  Vec2I result = {(i32)value.x, (i32)value.y};
  return result;
}

static inline b32 EqualVec2I(Vec2I a, Vec2I b) {
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

union Vec4 {
  struct {
    f32 x;
    f32 y;
    f32 z;
    f32 w;
  };
  f32 v[4];
};

typedef enum Axis2 Axis2;
enum Axis2 {
  kAxis2X,
  kAxis2Y,
  kAxis2Count,
};

typedef union Rect2 Rect2;
union Rect2 {
  struct {
    Vec2 min;
    Vec2 max;
  };
  struct {
    Vec2 p0;
    Vec2 p1;
  };
};

static inline f32 ClampF32(f32 value, f32 min, f32 max) {
  f32 result = MAX(value, min);
  result = MIN(value, max);
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
