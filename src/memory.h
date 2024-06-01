#pragma once

#include "core.h"

// Allocate a block of memory whose size is at least size bytes, assert non-null
// and initialize it to 0.
static void *AllocateMemory(usize size);
static void *AllocateMemoryNoZero(usize size);
static void *ReallocateMemory(void *ptr, usize new_size);
static void DeallocateMemory(void *ptr);
// Return current allocated memory in bytes.
static usize GetAllocatedBytes();
static void CopyMemory(void *dst, const void *src, usize size);
static char *CopyString(const char *str);

struct MemoryBlock {
    MemoryBlock *prev;
    u8 *base;
    usize size;
    usize used;
};

struct Arena {
    MemoryBlock *block;
    u32 temp_arena_count;
};

static void *BootstrapPushSize(usize struct_size, usize offset);

#define BootstrapPushStruct(Type, field)                                       \
    (Type *)BootstrapPushSize(sizeof(Type), offsetof(Type, field))

static void *PushSize(Arena *arena, usize size);

#define PushArray(arena, Type, size)                                           \
    (Type *)PushSize(arena, sizeof(Type) * size)

#define PushStruct(arena, Type) (Type *)PushSize(arena, sizeof(Type))

static Buffer PushBuffer(Arena *arena, usize size);

static Buffer PushBuffer(Arena *arena, Buffer src);

static Buffer PushFormat(Arena *arena, const char *fmt, ...);

#define PushFormatZ(arena, fmt, ...)                                           \
    ((char *)PushFormat(arena, fmt, ##__VA_ARGS__).data)

struct TempArena {
    Arena *arena;
    MemoryBlock *block;
    usize used;
};

static TempArena BeginTempArena(Arena *arena);
static void EndTempArena(TempArena temp_arena);

static void Clear(Arena *arena);
