#ifndef ZTRACING_SRC_MATH_H_
#define ZTRACING_SRC_MATH_H_

#include <float.h>
#include <math.h>

#include "src/assert.h"
#include "src/types.h"

#define F32_INFINITY ((f32)(INFINITY))

OPTIONAL_TYPE(f32o, f32, f32);

static inline bool f32_is_finite(f32 a) { return a != F32_INFINITY; }
static inline bool f32_is_infinity(f32 a) { return a == F32_INFINITY; }

static inline f32 f32_pow(f32 a, f32 b) {
  f32 result = powf(a, b);
  return result;
}

static inline f32 f32_exp(f32 a) {
  f32 result = expf(a);
  return result;
}

static inline f32 f32_abs(f32 a) {
  f32 result = fabs(a);
  return result;
}

static inline f32 f32_max(f32 a, f32 b) {
  f32 result = MAX(a, b);
  return result;
}

static inline f32 f32_min(f32 a, f32 b) {
  f32 result = MIN(a, b);
  return result;
}

static inline b32 f32_contains(f32 val, f32 begin, f32 end) {
  b32 result = begin <= val && val < end;
  return result;
}

static inline f32 f32_round(f32 value) {
  f32 result = roundf(value);
  return result;
}

static inline f32 f32_lerp(f32 a, f32 b, f32 t) {
  f32 result = a * (1.0f - t) + b * t;
  return result;
}

// Exponential Smoothing
// https://lisyarus.github.io/blog/posts/exponential-smoothing.html
static inline f32 f32_animate(f32 a, f32 b, f32 dt, f32 rate) {
  f32 result = a + (b - a) * (1.0f - f32_exp(-rate * dt));
  return result;
}

typedef struct Vec2 {
  f32 x;
  f32 y;
} Vec2;

static inline f32 vec2_get(Vec2 v, usize index) {
  DEBUG_ASSERT(index < 2);
  f32 result = ((f32 *)&v)[index];
  return result;
}

static inline void vec2_set(Vec2 *v, usize index, f32 value) {
  DEBUG_ASSERT(index < 2);
  ((f32 *)v)[index] = value;
}

static inline Vec2 v2(f32 x, f32 y) {
  Vec2 result = {x, y};
  return result;
}

static inline Vec2 vec2_zero(void) {
  Vec2 result = {0};
  return result;
}

static inline b32 vec2_is_zero(Vec2 a) {
  b32 result = a.x == 0 && a.y == 0;
  return result;
}

static inline b32 vec2_is_equal(Vec2 a, Vec2 b) {
  b32 result = a.x == b.x && a.y == b.y;
  return result;
}

// Vec2 is treated as one-dimension range, Vec2.x is begin, Vec2.y is end.
static inline Vec2 vec2_from_intersection(Vec2 a, Vec2 b) {
  DEBUG_ASSERT(a.x <= a.y && b.x <= b.y);

  Vec2 result = v2(0, 0);
  if (f32_contains(b.x, a.x, a.y)) {
    result.x = b.x;
    result.y = f32_min(a.y, b.y);
  } else if (f32_contains(b.y, a.x, a.y)) {
    result.x = f32_max(a.x, b.x);
    result.y = b.y;
  } else if (f32_contains(a.x, b.x, b.y)) {
    result.x = a.x;
    result.y = a.y;
  }
  return result;
}

static inline Vec2 vec2_from_array(f32 v[2]) {
  Vec2 result = {v[0], v[1]};
  return result;
}

static inline Vec2 vec2_add(Vec2 a, Vec2 b) {
  Vec2 result;
  result.x = a.x + b.x;
  result.y = a.y + b.y;
  return result;
}

static inline Vec2 vec2_sub(Vec2 a, Vec2 b) {
  Vec2 result;
  result.x = a.x - b.x;
  result.y = a.y - b.y;
  return result;
}

static inline Vec2 vec2_mul(Vec2 a, f32 b) {
  Vec2 result;
  result.x = a.x * b;
  result.y = a.y * b;
  return result;
}

static inline Vec2 vec2_min(Vec2 a, Vec2 b) {
  Vec2 result;
  result.x = f32_min(a.x, b.x);
  result.y = f32_min(a.y, b.y);
  return result;
}

static inline Vec2 vec2_max(Vec2 a, Vec2 b) {
  Vec2 result;
  result.x = f32_max(a.x, b.x);
  result.y = f32_max(a.y, b.y);
  return result;
}

typedef struct Vec2I {
  i32 x;
  i32 y;
} Vec2I;

static inline Vec2I vec2i_from_vec2(Vec2 value) {
  Vec2I result = {(i32)value.x, (i32)value.y};
  return result;
}

static inline b32 vec2i_is_equal(Vec2I a, Vec2I b) {
  b32 result = a.x == b.x && a.y == b.y;
  return result;
}

static inline Vec2I vec2i_neg(Vec2I a) {
  Vec2I result = {-a.x, -a.y};
  return result;
}

static inline Vec2I vec2i_add(Vec2I a, Vec2I b) {
  Vec2I result = {a.x + b.x, a.y + b.y};
  return result;
}

static inline Vec2I vec2i_sub(Vec2I a, Vec2I b) {
  Vec2I result = {a.x - b.x, a.y - b.y};
  return result;
}

static inline Vec2I vec2i_max(Vec2I a, Vec2I b) {
  Vec2I result = {MAX(a.x, b.x), MAX(a.y, b.y)};
  return result;
}

static inline Vec2I vec2i_min(Vec2I a, Vec2I b) {
  Vec2I result = {MIN(a.x, b.x), MIN(a.y, b.y)};
  return result;
}

static inline Vec2 vec2_from_vec2i(Vec2I value) {
  Vec2 result = {(f32)value.x, (f32)value.y};
  return result;
}

typedef struct Vec4 {
  f32 x;
  f32 y;
  f32 z;
  f32 w;
} Vec4;

// Premultiplied Color in sRGB space.
typedef struct ColorU32 {
  u8 a;
  u8 r;
  u8 g;
  u8 b;
} ColorU32;

static inline ColorU32 color_u32_zero() {
  ColorU32 result = {0};
  return result;
}

static Vec4 linear_color_from_srgb(ColorU32 color) {
  Vec4 result;
  result.x = f32_pow(color.r / 255.0f, 2.2f);
  result.y = f32_pow(color.g / 255.0f, 2.2f);
  result.z = f32_pow(color.b / 255.0f, 2.2f);
  result.w = color.a / 255.0f;
  return result;
}

static ColorU32 color_u32_from_linear_premultiplied(Vec4 color) {
  color.x = f32_pow(color.x, 1.0f / 2.2f);
  color.y = f32_pow(color.y, 1.0f / 2.2f);
  color.z = f32_pow(color.z, 1.0f / 2.2f);
  ColorU32 result;
  result.r = (u8)f32_round(color.x * 255.0f);
  result.g = (u8)f32_round(color.y * 255.0f);
  result.b = (u8)f32_round(color.z * 255.0f);
  result.a = (u8)f32_round(color.w * 255.0f);
  return result;
}

static inline ColorU32 color_u32_from_srgb_not_premultiplied(u8 r, u8 g, u8 b,
                                                             u8 a) {
  ColorU32 result;
  result.r = r;
  result.g = g;
  result.b = b;
  result.a = a;

  Vec4 color = linear_color_from_srgb(result);
  color.x *= color.w;
  color.y *= color.w;
  color.z *= color.w;

  result = color_u32_from_linear_premultiplied(color);
  return result;
}

static inline ColorU32 color_u32_from_hex(u32 hex) {
  ColorU32 result = color_u32_from_srgb_not_premultiplied(
      (hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF, 0xFF);
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

static inline Rect2 r2(Vec2 min, Vec2 max) {
  Rect2 result = {min, max};
  return result;
}

static inline Rect2 rect2_zero(void) {
  Rect2 result = {0};
  return result;
}

static inline Rect2 rect2_from_intersection(Rect2 a, Rect2 b) {
  Vec2 x_axis =
      vec2_from_intersection(v2(a.min.x, a.max.x), v2(b.min.x, b.max.x));
  Vec2 y_axis =
      vec2_from_intersection(v2(a.min.y, a.max.y), v2(b.min.y, b.max.y));
  Rect2 result;
  result.min.x = x_axis.x;
  result.max.x = x_axis.y;
  result.min.y = y_axis.x;
  result.max.y = y_axis.y;
  return result;
}

static inline f32 rect2_get_area(Rect2 a) {
  Vec2 size = vec2_sub(a.max, a.min);
  f32 result = size.x * size.y;
  return result;
}

static inline b32 f32_contains_including_end(f32 val, f32 begin, f32 end) {
  b32 result = begin <= val && val <= end;
  return result;
}

static inline b32 vec2_contains(Vec2 val, Vec2 begin, Vec2 end) {
  b32 result = f32_contains(val.x, begin.x, end.x) &&
               f32_contains(val.y, begin.y, end.y);
  return result;
}

static inline b32 vec2_contains_including_end(Vec2 val, Vec2 begin, Vec2 end) {
  b32 result = f32_contains_including_end(val.x, begin.x, end.x) &&
               f32_contains_including_end(val.y, begin.y, end.y);
  return result;
}

// Clamp value in range [min, max]
static inline f32 f32_clamp(f32 value, f32 min, f32 max) {
  f32 result = f32_min(f32_max(value, min), max);
  return result;
}

static inline i32 i32_clamp(i32 value, i32 min, i32 max) {
  i32 result = MAX(value, min);
  result = MIN(value, max);
  return result;
}

static inline Vec2 vec2_clamp(Vec2 value, Vec2 min, Vec2 max) {
  Vec2 result = {f32_clamp(value.x, min.x, max.x),
                 f32_clamp(value.y, min.y, max.y)};
  return result;
}

static inline Vec2I vec2i_clamp(Vec2I value, Vec2I min, Vec2I max) {
  Vec2I result = {i32_clamp(value.x, min.x, max.x),
                  i32_clamp(value.y, min.y, max.y)};
  return result;
}

static inline Vec2 vec2_round(Vec2 value) {
  Vec2 result = {f32_round(value.x), f32_round(value.y)};
  return result;
}

static inline f32 f32_floor(f32 value) {
  f32 result = floorf(value);
  return result;
}

#endif  // ZTRACING_SRC_MATH_H_
