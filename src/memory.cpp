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

usize INIT_BLOCK_SIZE = 64 * 1024;

struct ArenaBlock {
    usize cap;
    usize top;
    ArenaBlock *prev;
    ArenaBlock *next;
};

struct Arena {
    ArenaBlock *block;
};

static Arena *ArenaCreate() {
    Arena *arena = (Arena *)MemoryAlloc(
        sizeof(Arena) + sizeof(ArenaBlock) + INIT_BLOCK_SIZE
    );
    arena->block = (ArenaBlock *)(arena + 1);

    ArenaBlock *block = arena->block;
    block->cap = INIT_BLOCK_SIZE;
    block->top = 0;
    block->prev = 0;
    block->next = 0;

    return arena;
}

static void ArenaDestroy(Arena *arena) {
    bool done = false;
    while (!done) {
        if (arena->block->prev) {
            ArenaBlock *prev = arena->block->prev;
            MemoryFree(arena->block);
            arena->block = prev;
        } else {
            done = true;
        }
    }
    MemoryFree(arena);
}

static void ArenaClear(Arena *arena) {
    bool done = false;
    while (!done) {
        arena->block->top = 0;
        if (arena->block->prev) {
            arena->block = arena->block->prev;
        } else {
            done = true;
        }
    }
}

static void *ArenaBlockPush(ArenaBlock *block, usize size, bool zero) {
    void *result = 0;
    if (block->top + size <= block->cap) {
        u8 *base = (u8 *)(block + 1);
        result = base + block->top;
        block->top += size;

        if (zero) {
            memset(result, 0, size);
        }
    }
    return result;
}

static void *ArenaPush(Arena *arena, usize size, bool zero) {
    void *result = 0;
    if (size > 0) {
        while (!result) {
            result = ArenaBlockPush(arena->block, size, zero);
            if (!result) {
                usize new_block_cap = arena->block->cap << 1;
                while (new_block_cap <= size) {
                    new_block_cap <<= 1;
                }

                ArenaBlock *new_block = (ArenaBlock *)MemoryAlloc(
                    sizeof(ArenaBlock) + new_block_cap
                );
                new_block->cap = new_block_cap;
                new_block->top = 0;
                new_block->prev = arena->block;
                new_block->next = 0;

                arena->block->next = new_block;
                arena->block = new_block;
            }
        }
    }
    return result;
}

static void *ArenaPush(Arena *arena, usize size) {
    return ArenaPush(arena, size, /* zero= */ true);
}

static void *ArenaPushNoZero(Arena *arena, usize size) {
    return ArenaPush(arena, size, /* zero= */ false);
}

static void ArenaPop(Arena *arena, usize size) {
    ASSERT(size <= arena->block->top, "");
    arena->block->top -= size;
    while (arena->block->top == 0 && arena->block->prev) {
        arena->block = arena->block->prev;
    }
}

static char *ArenaPushString(Arena *arena, const char *fmt, ...) {
    va_list args;

    usize size = arena->block->cap - arena->block->top;
    char *result = ArenaPushArray(arena, char, size);
    va_start(args, fmt);
    usize actual_size = vsnprintf(result, size, fmt, args) + 1;
    va_end(args);

    if (actual_size < size) {
        ArenaPopArray(arena, char, size);
        result = (char *)ArenaPushNoZero(arena, actual_size);
    } else if (actual_size > size) {
        ArenaPopArray(arena, char, size);
        result = ArenaPushArray(arena, char, actual_size);
        va_start(args, fmt);
        vsnprintf(result, actual_size, fmt, args);
        va_end(args);
    }

    return result;
}

static void ArenaPopString(Arena *arena, char *str) {
    usize size = strlen(str) + 1;
    ArenaPop(arena, size);
}

struct ArenaTempImpl {
    Arena *arena;
    ArenaBlock *block;
    usize top;
};

static ArenaTemp *ArenaPushTemp(Arena *arena) {
    ArenaBlock *block = arena->block;
    usize top = block->top;
    ArenaTempImpl *temp = ArenaPushStruct(arena, ArenaTempImpl);
    temp->arena = arena;
    temp->block = block;
    temp->top = top;
    return (ArenaTemp *)temp;
}

static void ArenaPopTemp(ArenaTemp *temp_) {
    ArenaTempImpl *temp = (ArenaTempImpl *)temp_;
    Arena *arena = temp->arena;
    arena->block = temp->block;
    arena->block->top = temp->top;

    ArenaBlock *block = arena->block->next;
    while (block) {
        block->top = 0;
        block = block->next;
    }
}
