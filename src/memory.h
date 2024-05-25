#pragma once

#include "core.h"

// Allocate a block of memory whose size is at least size bytes, assert non-null
// and initialize it to 0.
static void *MemoryAlloc(usize size);
static void *MemoryAllocNoZero(usize size);
static void *MemoryRealloc(void *ptr, usize new_size);
static void MemoryFree(void *ptr);
// Return current allocated memory in bytes.
static usize MemoryGetAlloc();
static char *MemoryCopyString(const char *str);

struct Arena;
static Arena *ArenaCreate();
static void ArenaDestroy(Arena *arena);
static void ArenaClear(Arena *arena);

static void *ArenaAllocNoZero(Arena *arena, usize size);
static void *ArenaAlloc(Arena *arena, usize size);
static void *ArenaRealloc(Arena *arena, void *ptr, usize new_size);
static void ArenaFree(Arena *arena, void *ptr);

static char *ArenaFormatString(Arena *arena, const char *fmt, ...);

#define ArenaAllocArray(arena, type, count)                                    \
    ((type *)ArenaAlloc(arena, sizeof(type) * (count)))

#define ArenaAllocStruct(arena, type) ArenaAllocArray(arena, type, 1)

struct DynArray {
    Arena *arena;
    usize item_size;
    usize cap;
    usize len;
    void *items;
};

static DynArray *DynArrayCreate(Arena *arena, usize item_size, usize init_cap);
static void DynArrayDestroy(DynArray *da);
static void *DynArrayAppend(DynArray *da);
static void *DynArrayGet(DynArray *da, usize index);
static void DynArrayRemove(DynArray *da, usize index);
