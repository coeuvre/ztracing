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

static Arena *
alloc_arena() {
#define INIT_MEMORY_BLOCK_SIZE KB(4)
    MemoryBlock *block = alloc_memory_block(INIT_MEMORY_BLOCK_SIZE);

    Arena *result = (Arena *)block->pos;
    result->current_block = block;
    result->temp_count = 0;

    block->pos = (u8 *)(result + 1);
    assert(block->pos <= block->end);

    return result;
}

static void
free_last_block(Arena *arena) {
    MemoryBlock *free_block = arena->current_block;
    arena->current_block = free_block->prev;
    free(free_block);
}

static void
free_arena(Arena *arena) {
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
    debug_assert(block);
    if (block->pos + size > block->end) {
        usize block_size = max_u32(next_pow2_u32(size), INIT_MEMORY_BLOCK_SIZE);
        MemoryBlock *new_block = alloc_memory_block(block_size);
        new_block->prev = block;
        block = new_block;
        arena->current_block = block;
    }

    debug_assert(block->pos + size <= block->end);
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
        debug_assert(arena->current_block->pos >= temp.pos);
        arena->current_block->pos = temp.pos;
    }

    debug_assert(arena->temp_count > 0);
    --arena->temp_count;
}

thread_local Arena *t_scratches[2];

static Arena *
get_scratch(Arena **conflicts, usize len) {
    Arena *result = 0;
    for (u32 i = 0; i < array_count(t_scratches); ++i) {
        Arena *candidate = t_scratches[i];
        if (!candidate) {
            result = alloc_arena();
            t_scratches[i] = result;
            break;
        }

        b32 conflict = 0;
        for (u32 j = 0; j < len; ++j) {
            if (conflicts[j] == candidate) {
                conflict = 1;
                break;
            }
        }

        if (!conflict) {
            result = candidate;
            break;
        }
    }
    assert(result);
    return result;
}
