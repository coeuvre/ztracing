#pragma once

// -----------------------------------------------------------------------------
// Memory Ops

#define align_pow2(x, b) (((x) + (b) - 1) & (~((b) - 1)))

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
    PushArena_NoZero = 0x1,
};

static Arena *alloc_arena(void);
static void free_arena(Arena *arena);
static void *push_arena_(Arena *arena, usize size, u32 flags);
static void pop_arena(Arena *arena, usize size);

#define push_arena(arena, size) push_arena_(arena, size, 0)
#define push_array(arena, Type, len)                                           \
    (Type *)push_arena_(arena, sizeof(Type) * len, 0)
#define push_array_no_zero(arena, Type, len)                                   \
    (Type *)push_arena_(arena, sizeof(Type) * len, PushArena_NoZero)

static TempMemory begin_temp_memory(Arena *arena);
static void end_temp_memory(TempMemory temp);

static Arena *get_scratch(Arena **conflicts, usize len);

#define begin_scratch(conflicts, len)                                          \
    begin_temp_memory(get_scratch((conflicts), (len)))
#define end_scratch(temp) end_temp_memory(temp)
