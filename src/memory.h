#pragma once

#include "core.h"

// Allocate a block of memory whose size is at least size bytes, assert non-null
// and initialize it to 0.
void *AllocateMemory(usize size);
void *AllocateMemoryNoZero(usize size);
void *ReallocateMemory(void *ptr, usize new_size);
void DeallocateMemory(void *ptr);
// Return current allocated memory in bytes.
usize GetAllocatedBytes();
void CopyMemory(void *dst, const void *src, usize size);
char *CopyString(const char *str);

struct MemoryBlock {
    MemoryBlock *prev;
    MemoryBlock *next;
    u8 *base;
    usize size;
    usize used;
};

struct Arena {
    MemoryBlock *block;
    u32 temp_arena_count;
};

void *BootstrapPushSize(usize struct_size, usize offset);

#define BootstrapPushStruct(Type, field)                                       \
    (Type *)BootstrapPushSize(sizeof(Type), offsetof(Type, field))

void *PushSize(Arena *arena, usize size);

#define PushArray(arena, Type, size)                                           \
    (Type *)PushSize(arena, sizeof(Type) * size)

#define PushStruct(arena, Type) (Type *)PushSize(arena, sizeof(Type))

Buffer PushBuffer(Arena *arena, usize size);

Buffer PushBuffer(Arena *arena, Buffer src);

Buffer PushFormat(Arena *arena, const char *fmt, ...);

#define PushFormatZ(arena, fmt, ...)                                           \
    ((char *)PushFormat(arena, fmt, ##__VA_ARGS__).data)

struct TempArena {
    Arena *arena;
    MemoryBlock *block;
    usize used;
};

TempArena BeginTempArena(Arena *arena);
void EndTempArena(TempArena temp_arena);

void ClearArena(Arena *arena);
