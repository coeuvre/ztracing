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

static void *ArenaPushNoZero(Arena *arena, usize size);
static void *ArenaPush(Arena *arena, usize size);
static void ArenaPop(Arena *arena, usize size);

static char *ArenaPushString(Arena *arena, const char *fmt, ...);
static void ArenaPopString(Arena *arena, char *str);

#define ArenaPushArray(arena, type, count)                                     \
    ((type *)ArenaPush(arena, sizeof(type) * (count)))
#define ArenaPopArray(arena, type, count)                                      \
    ArenaPop(arena, sizeof(type) * (count))

#define ArenaPushStruct(arena, type) ArenaPushArray(arena, type, 1)
#define ArenaPopStruct(arena, type) ArenaPopArray(arena, type, 1)

struct ArenaTemp {
    Arena *arena;
};
static ArenaTemp *ArenaPushTemp(Arena *arena);
static void ArenaPopTemp(ArenaTemp *temp);
