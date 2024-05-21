#include "memory.h"

#include <stdio.h>
#include <string.h>

static char *MemoryCopyString(const char *str) {
    usize size = strlen(str);
    char *result = (char *)MemoryAllocNoZero(size + 1);
    ASSERT(result, "");
    memcpy(result, str, size);
    result[size] = 0;
    return result;
}

usize INIT_BLOCK_SIZE = 4 * 1024;

struct ArenaSlice {
    usize prev_top;
    usize size;
};

struct ArenaBlock {
    usize cap;
    usize top;
    ArenaBlock *prev;
    ArenaBlock *next;
};

struct Arena {
    ArenaBlock *block;
};

static ArenaSlice *GetSlice(ArenaBlock *block, usize offset) {
    u8 *base = (u8 *)(block + 1);
    ArenaSlice *slice = (ArenaSlice *)(base + offset) - 1;
    return slice;
}

static usize ArenaBlockGetAvail(ArenaBlock *block) {
    usize result = 0;
    usize p = block->top + sizeof(ArenaSlice);
    if (p < block->cap) {
        result = block->cap - p;
    }
    return result;
}

static Arena *ArenaCreate() {
    Arena *arena = (Arena *)MemoryAllocNoZero(
        sizeof(Arena) + sizeof(ArenaBlock) + INIT_BLOCK_SIZE
    );
    arena->block = (ArenaBlock *)(arena + 1);

    ArenaBlock *block = arena->block;
    block->cap = INIT_BLOCK_SIZE;
    block->top = sizeof(ArenaSlice);
    block->prev = 0;
    block->next = 0;

    ArenaSlice *slice = GetSlice(block, block->top);
    slice->prev_top = 0;
    slice->size = 0;

    return arena;
}

static void ArenaDestroy(Arena *arena) {
    bool done = false;
    while (arena->block) {
        ArenaBlock *block = arena->block;
        arena->block = block->prev;
        if (block->prev) {
            MemoryFree(block);
        }
    }
    MemoryFree(arena);
}

static void ArenaClear(Arena *arena) {
    bool done = false;
    while (!done) {
        ArenaBlock *block = arena->block;

        block->top = sizeof(ArenaSlice);
        ArenaSlice *slice = GetSlice(block, block->top);
        slice->prev_top = 0;
        slice->size = 0;

        if (block->prev) {
            arena->block = block->prev;
        } else {
            done = true;
        }
    }
}

static void *ArenaBlockPush(ArenaBlock *block, usize size, bool zero) {
    void *result = 0;
    usize total_size = sizeof(ArenaSlice) + size;
    if (block->top + total_size <= block->cap) {
        ArenaSlice *slice = GetSlice(block, block->top);
        ASSERT(slice->size == 0, "");
        slice->size = size;

        ArenaSlice *next_slice = GetSlice(block, block->top + total_size);
        next_slice->prev_top = block->top;
        next_slice->size = 0;

        block->top += total_size;

        result = slice + 1;
        if (zero) {
            memset(result, 0, size);
        }
    }
    return result;
}

static void *ArenaAlloc(Arena *arena, usize size, bool zero) {
    void *result = 0;
    if (size > 0) {
        while (!result) {
            result = ArenaBlockPush(arena->block, size, zero);
            if (!result) {
                if (arena->block->next) {
                    arena->block = arena->block->next;
                } else {
                    usize new_block_cap = arena->block->cap << 1;
                    while (new_block_cap <= size) {
                        new_block_cap <<= 1;
                    }

                    ArenaBlock *new_block = (ArenaBlock *)MemoryAllocNoZero(
                        sizeof(ArenaBlock) + new_block_cap
                    );
                    new_block->cap = new_block_cap;
                    new_block->top = sizeof(ArenaSlice);
                    new_block->prev = arena->block;
                    new_block->next = 0;

                    ArenaSlice *new_slice = GetSlice(new_block, new_block->top);
                    new_slice->prev_top = 0;
                    new_slice->size = 0;

                    arena->block->next = new_block;
                    arena->block = new_block;
                }
            }
        }
    }
    return result;
}

static void *ArenaAlloc(Arena *arena, usize size) {
    return ArenaAlloc(arena, size, /* zero= */ true);
}

static void *ArenaAllocNoZero(Arena *arena, usize size) {
    return ArenaAlloc(arena, size, /* zero= */ false);
}

static void ArenaFree(Arena *arena, void *ptr) {
    if (ptr) {
        ArenaSlice *slice = (ArenaSlice *)ptr - 1;
        slice->size = 0;
    }

    bool done = false;
    while (!done) {
        ArenaBlock *block = arena->block;
        ArenaSlice *slice = GetSlice(block, block->top);
        if (slice->prev_top > sizeof(ArenaSlice)) {
            ArenaSlice *prev_slice = GetSlice(block, slice->prev_top);
            if (prev_slice->size == 0) {
                block->top = slice->prev_top;
            } else {
                done = true;
            }
        } else if (block->prev) {
            arena->block = block->prev;
        } else {
            done = true;
        }
    }
}

static char *ArenaFormatString(Arena *arena, const char *fmt, ...) {
    va_list args;

    usize size = ArenaBlockGetAvail(arena->block);
    char *result =
        (char *)ArenaBlockPush(arena->block, size, /* zero= */ false);

    va_start(args, fmt);
    usize actual_size = vsnprintf(result, size, fmt, args) + 1;
    va_end(args);

    if (actual_size < size) {
        ArenaFree(arena, result);
        result = (char *)ArenaAllocNoZero(arena, actual_size);
    } else if (actual_size > size) {
        ArenaFree(arena, result);

        result = (char *)ArenaAllocNoZero(arena, actual_size);
        va_start(args, fmt);
        vsnprintf(result, actual_size, fmt, args);
        va_end(args);
    }

    return result;
}
