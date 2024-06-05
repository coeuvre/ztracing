#pragma once

#include "core.h"

#include <string.h>

void *AllocateMemory(isize size);
void *ReallocateMemory(void *ptr, isize old_size, isize new_size);
void DeallocateMemory(void *ptr, isize size);
// Return current allocated memory in bytes.
isize GetAllocatedBytes();

static inline void
CopyMemory(void *dst, const void *src, isize size) {
    memcpy(dst, src, size);
}

struct MemoryBlock {
    MemoryBlock *prev;
    MemoryBlock *next;
    u8 *begin;
    u8 *at;
    u8 *end;
};

struct Arena {
    MemoryBlock *block;
    u32 temp_arena_count;
};

void *BootstrapPushSize(isize struct_size, isize offset);

#define BootstrapPushStruct(Type, field)                                       \
    (Type *)BootstrapPushSize(sizeof(Type), offsetof(Type, field))

void *PushSize(Arena *arena, isize size);

#define PushArray(arena, Type, size)                                           \
    (Type *)PushSize(arena, sizeof(Type) * size)

#define PushStruct(arena, Type) (Type *)PushSize(arena, sizeof(Type))

Buffer PushBuffer(Arena *arena, isize size);

Buffer PushBuffer(Arena *arena, Buffer src);

Buffer PushFormat(Arena *arena, const char *fmt, ...);

#define PushFormatZ(arena, fmt, ...)                                           \
    ((char *)PushFormat(arena, fmt, ##__VA_ARGS__).data)

struct TempArena {
    Arena *arena;
    MemoryBlock *block;
    u8 *at;
};

TempArena BeginTempArena(Arena *arena);
void EndTempArena(TempArena temp_arena);

void ClearArena(Arena *arena);
