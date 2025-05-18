#ifndef ZTRACING_SRC_MATH_H_
#define ZTRACING_SRC_MATH_H_

#include <float.h>
#include <math.h>

#include "src/assert.h"
#include "src/flick.h"
#include "src/types.h"

#define F32_INFINITY ((f32)(INFINITY))

static inline bool IsNaN(f32 a) { return isnan(a); }
static inline bool IsFinite(f32 a) { return a != F32_INFINITY; }
static inline bool IsInfinite(f32 a) { return a == F32_INFINITY; }

static inline f32 Cos(f32 a) { return cosf(a); }
static inline f32 Sin(f32 a) { return sinf(a); }

static inline f32 Sqrt(f32 a) { return sqrtf(a); }

static inline f32 Pow(f32 a, f32 b) {
  f32 result = powf(a, b);
  return result;
}

static inline f32 Exp(f32 a) {
  f32 result = expf(a);
  return result;
}

static inline f32 Abs(f32 a) {
  f32 result = fabsf(a);
  return result;
}

static inline f32 Max(f32 a, f32 b) {
  f32 result = MAX(a, b);
  return result;
}

static inline f32 Min(f32 a, f32 b) {
  f32 result = MIN(a, b);
  return result;
}

static inline bool Contains(f32 val, f32 begin, f32 end) {
  bool result = begin <= val && val < end;
  return result;
}

static inline f32 Round(f32 value) {
  f32 result = roundf(value);
  return result;
}

static inline f32 Floor(f32 value) {
  f32 result = floorf(value);
  return result;
}

static inline f32 Ceil(f32 value) {
  f32 result = ceilf(value);
  return result;
}

static inline f32 Lerp(f32 a, f32 b, f32 t) {
  f32 result = a * (1.0f - t) + b * t;
  return result;
}

// Exponential Smoothing
// https://lisyarus.github.io/blog/posts/exponential-smoothing.html
static inline f32 Animate(f32 a, f32 b, f32 dt, f32 rate) {
  f32 result = a + (b - a) * (1.0f - Exp(-rate * dt));
  return result;
}

static inline f64 AbsF64(f64 a) { return fabs(a); }

static inline f64 PowF64(f64 a, f64 b) { return pow(a, b); }

static inline f64 MaxF64(f64 a, f64 b) { return MAX(a, b); }

static inline f64 MinF64(f64 a, f64 b) { return MIN(a, b); }

typedef FL_Vec2 Vec2;

static inline f32 Vec2_Get(Vec2 v, usize index) {
  DEBUG_ASSERT(index < 2);
  f32 result = ((f32 *)&v)[index];
  return result;
}

static inline bool Vec2_IsFinite(Vec2 v) {
  return IsFinite(v.x) && IsFinite(v.y);
}

static inline void Vec2_Set(Vec2 *v, usize index, f32 value) {
  DEBUG_ASSERT(index < 2);
  ((f32 *)v)[index] = value;
}

static inline Vec2 vec2(f32 x, f32 y) {
  Vec2 result = {x, y};
  return result;
}

static inline Vec2 Vec2_Zero(void) {
  Vec2 result = ZERO_INIT;
  return result;
}

static inline bool Vec2_IsZero(Vec2 a) {
  bool result = a.x == 0 && a.y == 0;
  return result;
}

static inline bool Vec2_IsEqual(Vec2 a, Vec2 b) {
  bool result = a.x == b.x && a.y == b.y;
  return result;
}

static inline f32 Vec2_GetLen(Vec2 a) { return Sqrt(a.x * a.x + a.y * a.y); }

// Vec2 is treated as one-dimension range, Vec2.x is begin, Vec2.y is end.
static inline Vec2 Vec2_FromIntersection(Vec2 a, Vec2 b) {
  DEBUG_ASSERT(a.x <= a.y && b.x <= b.y);

  Vec2 result = vec2(0, 0);
  if (Contains(b.x, a.x, a.y)) {
    result.x = b.x;
    result.y = Min(a.y, b.y);
  } else if (Contains(b.y, a.x, a.y)) {
    result.x = Max(a.x, b.x);
    result.y = b.y;
  } else if (Contains(a.x, b.x, b.y)) {
    result.x = a.x;
    result.y = a.y;
  }
  return result;
}

static inline Vec2 Vec2_FromArray(f32 v[2]) {
  Vec2 result = {v[0], v[1]};
  return result;
}

static inline Vec2 Vec2_Add(Vec2 a, Vec2 b) {
  Vec2 result;
  result.x = a.x + b.x;
  result.y = a.y + b.y;
  return result;
}

static inline Vec2 Vec2_Sub(Vec2 a, Vec2 b) {
  Vec2 result;
  result.x = a.x - b.x;
  result.y = a.y - b.y;
  return result;
}

static inline Vec2 Vec2_Mul(Vec2 a, f32 b) {
  Vec2 result;
  result.x = a.x * b;
  result.y = a.y * b;
  return result;
}

static inline Vec2 Vec2_Min(Vec2 a, Vec2 b) {
  Vec2 result;
  result.x = Min(a.x, b.x);
  result.y = Min(a.y, b.y);
  return result;
}

static inline Vec2 Vec2_Max(Vec2 a, Vec2 b) {
  Vec2 result;
  result.x = Max(a.x, b.x);
  result.y = Max(a.y, b.y);
  return result;
}

typedef struct Vec2I {
  i32 x;
  i32 y;
} Vec2I;

static inline Vec2I Vec2I_FromVec2(Vec2 value) {
  Vec2I result = {(i32)value.x, (i32)value.y};
  return result;
}

static inline bool Vec2I_IsEqual(Vec2I a, Vec2I b) {
  bool result = a.x == b.x && a.y == b.y;
  return result;
}

static inline Vec2I Vec2I_Neg(Vec2I a) {
  Vec2I result = {-a.x, -a.y};
  return result;
}

static inline Vec2I Vec2I_Add(Vec2I a, Vec2I b) {
  Vec2I result = {a.x + b.x, a.y + b.y};
  return result;
}

static inline Vec2I Vec2I_Sub(Vec2I a, Vec2I b) {
  Vec2I result = {a.x - b.x, a.y - b.y};
  return result;
}

static inline Vec2I Vec2I_Max(Vec2I a, Vec2I b) {
  Vec2I result = {MAX(a.x, b.x), MAX(a.y, b.y)};
  return result;
}

static inline Vec2I Vec2I_Min(Vec2I a, Vec2I b) {
  Vec2I result = {MIN(a.x, b.x), MIN(a.y, b.y)};
  return result;
}

static inline Vec2 Vec2_FromVec2I(Vec2I value) {
  Vec2 result = {(f32)value.x, (f32)value.y};
  return result;
}

typedef struct Vec4 {
  f32 x;
  f32 y;
  f32 z;
  f32 w;
} Vec4;

#if 0
static Vec4 linear_color_from_srgb(ColorU32 color) {
  Vec4 result;
  result.x = Pow(color.r / 255.0f, 2.2f);
  result.y = Pow(color.g / 255.0f, 2.2f);
  result.z = Pow(color.b / 255.0f, 2.2f);
  result.w = color.a / 255.0f;
  return result;
}

static ColorU32 color_u32_from_linear_premultiplied(Vec4 color) {
  color.x = Pow(color.x, 1.0f / 2.2f);
  color.y = Pow(color.y, 1.0f / 2.2f);
  color.z = Pow(color.z, 1.0f / 2.2f);
  ColorU32 result;
  result.r = (u8)Round(color.x * 255.0f);
  result.g = (u8)Round(color.y * 255.0f);
  result.b = (u8)Round(color.z * 255.0f);
  result.a = (u8)Round(color.w * 255.0f);
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
#endif

static inline i32 ClampI32(i32 value, i32 min, i32 max) {
  i32 result = MAX(value, min);
  result = MIN(value, max);
  return result;
}

FL_OPTIONAL_TYPE(i64o, i64);
FL_OPTIONAL_TYPE(f32o, f32);

static inline f64 RoundF64(f64 value) {
  f64 result = round(value);
  return result;
}

#endif  // ZTRACING_SRC_MATH_H_
