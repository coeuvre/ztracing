#pragma once

// -----------------------------------------------------------------------------
// Foreign Includes

#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// -----------------------------------------------------------------------------
// Asserts

#if COMPILER_MSVC
#define break_debugger() __debugbreak()
#elif COMPILER_CLANG || COMPILER_GCC
#define break_debugger() __builtin_trap()
#else
#error Unknown trap intrinsic for this compiler.
#endif

#define assert(x)                                                              \
    do {                                                                       \
        if (!(x)) {                                                            \
            break_debugger();                                                  \
        }                                                                      \
    } while (0)

#if BUILD_DEBUG
#define debug_assert(x) assert(x)
#else
#define debug_assert(x) (void)(x)
#endif
#define UNREACHABLE assert(!"Unreachable")
#define NOT_IMPLEMENTED assert(!"Not Implemented")

// -----------------------------------------------------------------------------
// Misc. Helper Macros

#define array_count(a) (sizeof(a) / sizeof((a)[0]))

#define KB(n) (((u64)(n)) << 10)
#define MB(n) (((u64)(n)) << 20)
#define GB(n) (((u64)(n)) << 30)
#define TB(n) (((u64)(n)) << 40)

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

// -----------------------------------------------------------------------------
// Base types

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef i8 b8;
typedef i16 b16;
typedef i32 b32;
typedef i64 b64;
typedef float f32;
typedef double f64;
typedef size_t usize;
typedef ptrdiff_t isize;

static inline u32
next_pow2_u32(u32 value) {
    u32 result = 1;
    while (result < value) {
        result <<= 1;
    }
    return result;
}

static inline u32
max_u32(u32 a, u32 b) {
    u32 result = MAX(a, b);
    return result;
}

// -----------------------------------------------------------------------------
// Strings

typedef struct String String;
struct String {
    u8 *ptr;
    usize len;
};

#define string_literal(s)                                                      \
    (String) { (u8 *)s, sizeof(s) - 1 }

// -----------------------------------------------------------------------------
// Vectors

typedef union Vec2 Vec2;
union Vec2 {
    struct {
        f32 x;
        f32 y;
    };
    f32 v[2];
};

static inline Vec2
vec2(f32 x, f32 y) {
    Vec2 result = {x, y};
    return result;
}

typedef union Vec2i Vec2i;
union Vec2i {
    struct {
        i32 x;
        i32 y;
    };
    i32 v[2];
};

static inline Vec2i
vec2i(i32 x, i32 y) {
    Vec2i result = {x, y};
    return result;
}

static inline Vec2i
vec2i_from_vec2(Vec2 value) {
    Vec2i result = {(i32)value.x, (i32)value.y};
    return result;
}

static inline b32
equal_vec2i(Vec2i a, Vec2i b) {
    b32 result = a.x == b.x && a.y == b.y;
    return result;
}

static inline Vec2i
neg_vec2i(Vec2i a) {
    Vec2i result = {-a.x, -a.y};
    return result;
}

static inline Vec2i
add_vec2i(Vec2i a, Vec2i b) {
    Vec2i result = {a.x + b.x, a.y + b.y};
    return result;
}

static inline Vec2i
sub_vec2i(Vec2i a, Vec2i b) {
    Vec2i result = {a.x - b.x, a.y - b.y};
    return result;
}

static inline Vec2i
max_vec2i(Vec2i a, Vec2i b) {
    Vec2i result = {MAX(a.x, b.x), MAX(a.y, b.y)};
    return result;
}

static inline Vec2i
min_vec2i(Vec2i a, Vec2i b) {
    Vec2i result = {MIN(a.x, b.x), MIN(a.y, b.y)};
    return result;
}

static inline Vec2
vec2_from_vec2i(Vec2i value) {
    Vec2 result = {value.x, value.y};
    return result;
}

typedef union Vec4 Vec4;
union Vec4 {
    struct {
        f32 x;
        f32 y;
        f32 z;
        f32 w;
    };
    f32 v[4];
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

static inline Rect2
rect2(Vec2 min, Vec2 max) {
    Rect2 result = {min, max};
    return result;
}

// -----------------------------------------------------------------------------
// Math

static inline f32
clamp_f32(f32 value, f32 min, f32 max) {
    f32 result = MAX(value, min);
    result = MIN(value, max);
    return result;
}

static inline i32
clamp_i32(i32 value, i32 min, i32 max) {
    i32 result = MAX(value, min);
    result = MIN(value, max);
    return result;
}

static inline Vec2
clamp_vec2(Vec2 value, Vec2 min, Vec2 max) {
    Vec2 result = {
        clamp_f32(value.x, min.x, max.x), clamp_f32(value.y, min.y, max.y)
    };
    return result;
}

static inline Vec2i
clamp_vec2i(Vec2i value, Vec2i min, Vec2i max) {
    Vec2i result = {
        clamp_i32(value.x, min.x, max.x), clamp_i32(value.y, min.y, max.y)
    };
    return result;
}

static inline f32
round_f32(f32 value) {
    f32 result = roundf(value);
    return result;
}

static inline Vec2
round_vec2(Vec2 value) {
    Vec2 result = {round_f32(value.x), round_f32(value.y)};
    return result;
}

static inline f32
floor_f32(f32 value) {
    f32 result = floorf(value);
    return result;
}
