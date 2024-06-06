#include "memory.h"

#include <stdarg.h>
#include <stdio.h>

#include "core.h"

static isize INIT_BLOCK_SIZE = 4 * 1024;

void *
BootstrapPushSize(isize struct_size, isize offset) {
    Arena arena = {};
    void *result = PushSize(&arena, struct_size);
    *(Arena *)((u8 *)result + offset) = arena;
    return result;
}

static void *
PushSize(Arena *arena, isize size, bool zero) {
    ASSERT(size >= 0);

    while (arena->block && arena->block->at + size > arena->block->end &&
           arena->block->next) {
        arena->block = arena->block->next;
    }

    if (!arena->block || arena->block->at + size > arena->block->end) {
        isize new_size = INIT_BLOCK_SIZE;
        if (arena->block) {
            new_size = (arena->block->end - arena->block->begin) << 1;
        }
        while (new_size < size) {
            new_size <<= 1;
        }
        MemoryBlock *block =
            (MemoryBlock *)AllocateMemory(sizeof(MemoryBlock) + new_size);
        block->prev = arena->block;
        block->next = 0;
        block->begin = (u8 *)(block + 1);
        block->at = block->begin;
        block->end = block->begin + new_size;
        if (arena->block) {
            ASSERT(!arena->block->next);
            arena->block->next = block;
        }

        arena->block = block;
    }

    ASSERT(arena->block && arena->block->at + size <= arena->block->end);
    void *result = arena->block->at;
    arena->block->at += size;

    if (zero) {
        memset(result, 0, size);
    }

    return result;
}

void *
PushSize(Arena *arena, isize size) {
    return PushSize(arena, size, /* zero = */ true);
}

Buffer
PushBuffer(Arena *arena, isize size) {
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

static isize
GetRemaining(Arena *arena) {
    isize result = 0;
    if (arena->block) {
        result = arena->block->end - arena->block->at;
    }
    return result;
}

Buffer
PushFormat(Arena *arena, const char *fmt, ...) {
    Buffer result = {};

    va_list args;

    TempArena temp_arena = BeginTempArena(arena);
    isize size = GetRemaining(arena);
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
        temp_arena.at = temp_arena.block->at;
    }
    ++arena->temp_arena_count;
    return temp_arena;
}

void
EndTempArena(TempArena temp_arena) {
    Arena *arena = temp_arena.arena;
    if (temp_arena.block) {
        while (arena->block != temp_arena.block) {
            arena->block->at = arena->block->begin;
            arena->block = arena->block->prev;
        }
        ASSERT(arena->block->at >= temp_arena.at);
        arena->block->at = temp_arena.at;
    } else if (arena->block) {
        while (arena->block->prev) {
            arena->block->at = arena->block->begin;
            arena->block = arena->block->prev;
        }
        arena->block->at = arena->block->begin;
    }

    ASSERT(arena->temp_arena_count > 0);
    --arena->temp_arena_count;
}

static void
FreeLastBlock(Arena *arena) {
    MemoryBlock *block = arena->block;
    arena->block = block->prev;
    if (arena->block) {
        arena->block->next = 0;
    }
    DeallocateMemory(block, sizeof(MemoryBlock) + block->end - block->begin);
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

static inline u64
Hash(Buffer buffer) {
    u64 h = 0x100;
    for (isize i = 0; i < buffer.size; i++) {
        h ^= buffer.data[i];
        h *= 1111111111111111111u;
    }
    return h;
}

void **
Upsert(Arena *arena, HashMap *m, Buffer key) {
    HashNode **node = &m->root;
    for (u64 hash = Hash(key); *node; hash <<= 2) {
        if (Equal(key, (*node)->key)) {
            return &(*node)->value;
        }
        node = &(*node)->child[hash >> 62];
    }
    *node = PushStruct(arena, HashNode);
    (*node)->key = PushBuffer(arena, key);
    return &(*node)->value;
}

HashNode *
GetNext(HashMapIter *iter) {
    HashNode *result = 0;
    if (iter->next) {
        result = iter->next->node;
        HashMapIterNode *free = iter->next;
        iter->next = iter->next->next;

        free->next = iter->first_free;
        iter->first_free = free;
    }

    if (result) {
        for (isize i = 0; i < 4; ++i) {
            HashNode *node = result->child[i];
            if (node) {
                HashMapIterNode *next = 0;
                if (iter->first_free) {
                    next = iter->first_free;
                    iter->first_free = next->next;
                } else {
                    next = PushStruct(iter->arena, HashMapIterNode);
                }
                next->node = result->child[i];
                next->next = iter->next;

                iter->next = next;
            }
        }
    }
    ASSERT(!result || result->value);
    return result;
}
