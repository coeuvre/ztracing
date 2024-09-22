#pragma once

// -----------------------------------------------------------------------------
// Strings

typedef struct Str8 Str8;
struct Str8 {
    u8 *ptr;
    // The number of bytes of the string, excluding NULL-terminator. The
    // underlying buffer pointed by `ptr` MUST be at least (len + 1) large to
    // hold both the content the of string AND the NULL-terminator.
    usize len;
};

static inline Str8
str8(u8 *ptr, usize len) {
    Str8 result = {ptr, len};
    return result;
}

#define str8_literal(s) str8((u8 *)(s), sizeof(s) - 1)

typedef struct Str32 Str32;
struct Str32 {
    u32 *ptr;
    usize len;
};

static inline Str32
str32(u32 *ptr, usize len) {
    Str32 result = {ptr, len};
    return result;
}

static Str32 str32_from_str8(Arena *arena, Str8 str);
