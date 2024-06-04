#include "memory.h"

#include <stdarg.h>
#include <stdio.h>

struct MemoryBlock {
    MemoryBlock *prev;
    MemoryBlock *next;
    u8 *base;
    usize size;
    usize used;
};

static usize INIT_BLOCK_SIZE = 4 * 1024;

void *
BootstrapPushSize(usize struct_size, usize offset) {
    Arena arena = {};
    void *result = PushSize(&arena, struct_size);
    *(Arena *)((u8 *)result + offset) = arena;
    return result;
}

static void *
PushSize(Arena *arena, usize size, bool zero) {
    while (arena->block && arena->block->used + size > arena->block->size &&
           arena->block->next) {
        arena->block = arena->block->next;
    }

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
        block->next = 0;
        block->base = (u8 *)(block + 1);
        block->size = new_size;
        block->used = 0;
        if (arena->block) {
            ASSERT(!arena->block->next);
            arena->block->next = block;
        }

        arena->block = block;
    }

    ASSERT(arena->block && arena->block->used + size <= arena->block->size);
    void *result = arena->block->base + arena->block->used;
    arena->block->used += size;

    if (zero) {
        memset(result, 0, size);
    }

    return result;
}

void *
PushSize(Arena *arena, usize size) {
    return PushSize(arena, size, /* zero = */ true);
}

Buffer
PushBuffer(Arena *arena, usize size) {
    Buffer buffer = {};
    buffer.data = (u8 *)PushSize(arena, size, /* zero = */ false);
    buffer.size = size;
    return buffer;
}

Buffer
PushBuffer(Arena *arena, Buffer src) {
    Buffer dst = PushBuffer(arena, src.size);
    CopyMemory(dst.data, src.data, src.size);
    return dst;
}

static usize
GetRemaining(Arena *arena) {
    usize result = 0;
    if (arena->block) {
        result = arena->block->size - arena->block->used;
    }
    return result;
}

Buffer
PushFormat(Arena *arena, const char *fmt, ...) {
    Buffer result = {};

    va_list args;

    TempArena temp_arena = BeginTempArena(arena);
    usize size = GetRemaining(arena);
    char *buf = (char *)PushSize(arena, size, /* zero= */ false);
    va_start(args, fmt);
    result.size = vsnprintf(buf, size, fmt, args) + 1;
    va_end(args);
    EndTempArena(temp_arena);

    result.data = (u8 *)buf;
    if (result.size < size) {
        result.data = (u8 *)PushSize(arena, size, /* zero= */ false);
        ASSERT(result.data == (u8 *)buf);
    } else if (result.size > size) {
        result.data = (u8 *)PushSize(arena, result.size, /* zero= */ false);
        va_start(args, fmt);
        vsnprintf((char *)result.data, result.size, fmt, args);
        va_end(args);
    }

    return result;
}

TempArena
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

void
EndTempArena(TempArena temp_arena) {
    Arena *arena = temp_arena.arena;
    if (temp_arena.block) {
        while (arena->block != temp_arena.block) {
            arena->block->used = 0;
            arena->block = arena->block->prev;
        }
        ASSERT(arena->block->used >= temp_arena.used);
        arena->block->used = temp_arena.used;
    } else if (arena->block) {
        while (arena->block->prev) {
            arena->block->used = 0;
            arena->block = arena->block->prev;
        }
        arena->block->used = 0;
    }

    ASSERT(arena->temp_arena_count > 0);
    --arena->temp_arena_count;
}

static void
FreeLastBlock(Arena *arena) {
    MemoryBlock *block = arena->block;
    arena->block = block->prev;
    DeallocateMemory(block);
    if (arena->block) {
        arena->block->next = 0;
    }
}

void
ClearArena(Arena *arena) {
    ASSERT(arena->temp_arena_count == 0);

    while (arena->block && arena->block->next) {
        arena->block = arena->block->next;
    }

    bool has_block = arena->block != 0;
    while (has_block) {
        // The arena itself might be stored in the last block, we must ensure
        // not looking at it after freeing
        has_block = arena->block->prev != 0;
        FreeLastBlock(arena);
    }
}
