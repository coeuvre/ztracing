#include "memory.h"

#include <string.h>

static char *MemStrDup(const char *str) {
    usize size = strlen(str);
    char *result = (char *)MemAlloc(size);
    if (result) {
        memcpy(result, str, size);
    }
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
    Arena *arena =
        (Arena *)MemAlloc(sizeof(Arena) + sizeof(ArenaBlock) + INIT_BLOCK_SIZE);
    ASSERT(arena, "");
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
            MemFree(arena->block);
            arena->block = prev;
        } else {
            done = true;
        }
    }
    MemFree(arena);
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

static void *ArenaBlockPush(ArenaBlock *block, usize size) {
    void *result = 0;
    if (block->top + size <= block->cap) {
        u8 *base = (u8 *)(block + 1);
        result = base + block->top;
        block->top += size;

        memset(result, 0, size);
    }
    return result;
}

static void *ArenaPush(Arena *arena, usize size) {
    void *result = 0;
    while (!result) {
        result = ArenaBlockPush(arena->block, size);
        if (!result) {
            usize new_block_cap = arena->block->cap << 1;
            while (new_block_cap <= size) {
                new_block_cap <<= 1;
            }

            ArenaBlock *new_block =
                (ArenaBlock *)MemAlloc(sizeof(ArenaBlock) + new_block_cap);
            ASSERT(new_block, "");
            new_block->cap = new_block_cap;
            new_block->top = 0;
            new_block->prev = arena->block;
            new_block->next = 0;

            arena->block->next = new_block;
            arena->block = new_block;
        }
    }
    return result;
}

static void ArenaPop(Arena *arena, usize size) {
    ASSERT(size <= arena->block->top, "");
    arena->block->top -= size;
    while (arena->block->top == 0 && arena->block->prev) {
        arena->block = arena->block->prev;
    }
}

static char *ArenaPushStr(Arena *arena, const char *str) {
    usize size = strlen(str) + 1;
    char *result = ArenaPushArray(arena, char, size);
    memcpy(result, str, size);
    return result;
}

static void ArenaPopStr(Arena *arena, char *str) {
    usize size = strlen(str) + 1;
    ArenaPop(arena, size);
}

struct ArenaTemp {
    ArenaBlock *block;
    usize top;
};

static ArenaTemp *ArenaPushTemp(Arena *arena) {
    ArenaBlock *block = arena->block;
    usize top = block->top;
    ArenaTemp *temp = ArenaPushStruct(arena, ArenaTemp);
    temp->block = block;
    temp->top = top;
    return temp;
}

static void ArenaPopTemp(Arena *arena, ArenaTemp *temp) {
    arena->block = temp->block;
    arena->block->top = temp->top;

    ArenaBlock *block = arena->block->next;
    while (block) {
        block->top = 0;
        block = block->next;
    }
}
