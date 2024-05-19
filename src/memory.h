#pragma once

#include "core.h"

static void *MemAlloc(usize size);
static void *MemCAlloc(usize sum, usize size);
static void *MemReAlloc(void *ptr, usize new_size);
static void MemFree(void *ptr);
static usize MemGetAllocatedBytes();
static char *MemStrDup(const char *str);

struct Arena;
static Arena *ArenaCreate();
static void ArenaDestroy(Arena *arena);
static void ArenaClear(Arena *arena);

static void *ArenaPushNoZero(Arena *arena, usize size);
static void *ArenaPush(Arena *arena, usize size);
static void ArenaPop(Arena *arena, usize size);

static char *ArenaPushStr(Arena *arena, const char *fmt, ...);
static void ArenaPopStr(Arena *arena, char *str);

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
