static MemoryBlock *
alloc_memory_block(usize size) {
    MemoryBlock *block = malloc(sizeof(MemoryBlock *) + size);
    assert(block);
    block->prev = 0;
    block->begin = (u8 *)(block + 1);
    block->end = block->begin + size;
    block->pos = block->begin;
    return block;
}

static inline void
free_last_block(Arena *arena) {
    MemoryBlock *free_block = arena->current_block;
    arena->current_block = free_block->prev;
    free(free_block);
}

static void
clear_arena(Arena *arena) {
    assert(arena->temp_count == 0);
    while (arena->current_block) {
        b32 is_last_block = arena->current_block->prev == 0;
        free_last_block(arena);
        if (is_last_block) {
            break;
        }
    }
}

static void *
push_size_(Arena *arena, usize size, u32 flags) {
    MemoryBlock *block = arena->current_block;
    if (!block || block->pos + size > block->end) {
#define INIT_MEMORY_BLOCK_SIZE KB(4)
        usize block_size = max_u32(next_pow2_u32(size), INIT_MEMORY_BLOCK_SIZE);
        MemoryBlock *new_block = alloc_memory_block(block_size);
        new_block->prev = block;
        block = new_block;
        arena->current_block = block;
    }

    debug_assert(block && block->pos + size <= block->end);
    void *result = block->pos;
    block->pos += size;

    if (!(flags & PushArenaFlag_NoZero)) {
        zero_memory(result, size);
    }

    return result;
}

static TempMemory
begin_temp_memory(Arena *arena) {
    TempMemory temp;
    temp.arena = arena;
    temp.block = arena->current_block;
    temp.pos = temp.block ? temp.block->pos : 0;
    ++arena->temp_count;
    return temp;
}

static void
end_temp_memory(TempMemory temp) {
    Arena *arena = temp.arena;
    while (arena->current_block != temp.block) {
        free_last_block(arena);
    }

    if (arena->current_block) {
        assert(arena->current_block->pos >= temp.pos);
        arena->current_block->pos = temp.pos;
    }

    assert(arena->temp_count > 0);
    --arena->temp_count;
}
