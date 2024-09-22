#pragma once

// -----------------------------------------------------------------------------
// Memory Ops

static inline void
zero_memory(void *ptr, usize size) {
    memset(ptr, 0, size);
}

// -----------------------------------------------------------------------------
// Memory Arena

typedef struct MemoryBlock MemoryBlock;
struct MemoryBlock {
    MemoryBlock *prev;

    u8 *begin;
    u8 *end;
    u8 *pos;
};

typedef struct Arena Arena;
struct Arena {
    MemoryBlock *current_block;
    u32 temp_count;
};

typedef struct TempMemory TempMemory;
struct TempMemory {
    Arena *arena;
    MemoryBlock *block;
    u8 *pos;
};

enum PushArenaFlag {
    PushArenaFlag_NoZero = 0x1,
};

static void clear_arena(Arena *arena);
static void *push_size_(Arena *arena, usize size, u32 flags);

#define push_size(arena, size) push_size_(arena, size, 0)
#define push_array(arena, Type, len) (Type *)push_size(arena, sizeof(Type) * len)

static TempMemory begin_temp_memory(Arena *arena);
static void end_temp_memory(TempMemory temp);
