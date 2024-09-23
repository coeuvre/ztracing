typedef struct UnicodeDecode UnicodeDecode;
struct UnicodeDecode {
    u32 codepoint;
    u32 increment;
};

static UnicodeDecode
utf8_decode(u8 *ptr, usize len) {
#define valid_prefix(b) (((b) & 0b11000000) == 0b10000000)
    UnicodeDecode result = {.codepoint = 0xFFFD, .increment = 1};
    if (len >= 1) {
        u32 b0 = ptr[0];
        if ((b0 & 0b10000000) == 0) {
            result.codepoint = b0 & 0b01111111;
            result.increment = 1;
        } else if (((b0 & 0b11100000) == 0b11000000) && len >= 2) {
            u32 b1 = ptr[1];
            if (valid_prefix(b1)) {
                result.codepoint = ((b0 & 0b00011111) << 6) | (b1 & 0b00111111);
                result.increment = 2;
            }
        } else if (len >= 3 && ((b0 & 0b11110000) == 0b11100000)) {
            u32 b1 = ptr[1];
            u32 b2 = ptr[2];
            if (valid_prefix(b1) && valid_prefix(b2)) {
                result.codepoint = ((b0 & 0b00001111) << 12) |
                                   ((b1 & 0b00111111) << 6) | (b2 & 0b00111111);
                result.increment = 3;
            }
        } else if (len >= 4 && ((b0 & 0b11111000) == 0b11110000)) {
            u32 b1 = ptr[1];
            u32 b2 = ptr[2];
            u32 b3 = ptr[3];
            if (valid_prefix(b1) && valid_prefix(b2) && valid_prefix(b3)) {
                result.codepoint = ((b0 & 0b00000111) << 16) |
                                   ((b1 & 0b00111111) << 12) |
                                   ((b2 & 0b00111111) << 6) | (b3 & 0b00111111);
                result.increment = 4;
            }
        }
    }
    return result;
}

static Str32
push_str32_from_str8(Arena *arena, Str8 str) {
    usize cap = str.len + 1;
    u32 *ptr = push_array_no_zero(arena, u32, cap);
    u32 len = 0;
    u8 *end = str.ptr + str.len;
    UnicodeDecode decode;
    for (u8 *cursor = str.ptr; cursor < end; cursor += decode.increment) {
        decode = utf8_decode(cursor, end - cursor);
        ptr[len++] = decode.codepoint;
    }
    ptr[len++] = 0;
    debug_assert(len <= cap);
    pop_arena(arena, (cap - len) * 4);
    return str32(ptr, len - 1);
}
