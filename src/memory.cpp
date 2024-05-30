#include "memory.h"

#include <stdio.h>
#include <string.h>

static char *
CopyString(const char *str) {
    usize size = strlen(str);
    char *result = (char *)AllocateMemoryNoZero(size + 1);
    ASSERT(result, "");
    memcpy(result, str, size);
    result[size] = 0;
    return result;
}

static usize INIT_BLOCK_SIZE = 4 * 1024;

static void *
BootstrapPushSize(usize struct_size, usize offset) {
    Arena arena = {};
    void *result = PushSize(&arena, struct_size);
    *(Arena *)((u8 *)result + offset) = arena;
    return result;
}

static void *
PushSize(Arena *arena, usize size, bool zero) {
    if (!arena->block || arena->block->used + size > arena->block->size) {
        usize new_size = INIT_BLOCK_SIZE;
        if (arena->block) {
            new_size = arena->block->size << 1;
        }
        while (new_size < size) {
            new_size <<= 1;
        }
        MemoryBlock *block =
            (MemoryBlock *)AllocateMemory(sizeof(MemoryBlock) + new_size);
        block->prev = arena->block;
        block->base = (u8 *)(block + 1);
        block->size = new_size;
        block->used = 0;

        arena->block = block;
    }

    ASSERT(arena->block->used + size <= arena->block->size, "");
    void *result = arena->block->base + arena->block->used;
    arena->block->used += size;

    if (zero) {
        memset(result, 0, size);
    }

    return result;
}

static void *
PushSize(Arena *arena, usize size) {
    return PushSize(arena, size, /* zero = */ true);
}

static usize
GetRemaining(Arena *arena) {
    usize result = 0;
    if (arena->block) {
        result = arena->block->size - arena->block->used;
    }
    return result;
}

static char *
PushFormat(Arena *arena, const char *fmt, ...) {
    va_list args;

    TempArena temp_arena = BeginTempArena(arena);
    usize size = GetRemaining(arena);
    char *buf = (char *)PushSize(arena, size, /* zero= */ false);
    va_start(args, fmt);
    usize actual_size = vsnprintf(buf, size, fmt, args) + 1;
    va_end(args);
    EndTempArena(temp_arena);

    char *result = buf;
    if (actual_size < size) {
        result = (char *)PushSize(arena, size, /* zero= */ false);
        ASSERT(result == buf, "");
    } else if (actual_size > size) {
        result = (char *)PushSize(arena, actual_size, /* zero= */ false);
        va_start(args, fmt);
        vsnprintf(result, actual_size, fmt, args);
        va_end(args);
    }

    return result;
}

static TempArena
BeginTempArena(Arena *arena) {
    TempArena temp_arena = {};
    temp_arena.arena = arena;
    temp_arena.block = arena->block;
    if (temp_arena.block) {
        temp_arena.used = temp_arena.block->used;
    }
    ++arena->temp_arena_count;
    return temp_arena;
}

static void
FreeLastBlock(Arena *arena) {
    MemoryBlock *block = arena->block;
    arena->block = block->prev;
    DeallocateMemory(block);
}

static void
EndTempArena(TempArena temp_arena) {
    Arena *arena = temp_arena.arena;
    while (arena->block != temp_arena.block) {
        FreeLastBlock(arena);
    }

    if (arena->block) {
        ASSERT(arena->block->used >= temp_arena.used, "");
        arena->block->used = temp_arena.used;
    }

    ASSERT(arena->temp_arena_count > 0, "");
    --arena->temp_arena_count;
}

static void
Clear(Arena *arena) {
    ASSERT(arena->temp_arena_count == 0, "");

    bool has_block = arena->block != 0;
    while (has_block) {
        // The arena itself might be stored in the last block, we must ensure
        // not looking at it after freeing
        has_block = arena->block->prev != 0;
        FreeLastBlock(arena);
    }
}
