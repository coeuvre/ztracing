#include "src/memory.h"

#include <stdlib.h>

#include "src/assert.h"
#include "src/types.h"

#define kPageSize KB(4)
#define kAlign 8

static inline usize usize_align_pow2(usize addr, usize align) {
  usize result = ((addr + align - 1) & (~(align - 1)));
  return result;
}

// TODO: thread-safe
static usize g_allocated_bytes;

void *memory_alloc(usize size) {
  g_allocated_bytes += size;
  void *result = malloc(size);
  ASSERT(result);
  return result;
}

void memory_free(void *ptr, usize size) {
  g_allocated_bytes -= size;
  free(ptr);
}

usize memory_get_allocated_bytes(void) { return g_allocated_bytes; }

// Allocate a memory block which is at least `size` bytes large.
static MemoryBlock *memory_block_alloc(usize size) {
  usize block_size = usize_align_pow2(sizeof(MemoryBlock) + size, kPageSize);

  MemoryBlock *block = (MemoryBlock *)memory_alloc(block_size);
  block->prev = 0;
  block->begin = (u8 *)(block + 1);
  block->end = (u8 *)block + block_size;
  block->pos = block->begin;
  return block;
}

static inline void memory_block_free(MemoryBlock *block) {
  usize block_size = block->end - (u8 *)block;
  memory_free(block, block_size);
}

static void memory_block_free_last(Arena *arena) {
  MemoryBlock *block = arena->current_block;
  arena->current_block = block->prev;
  memory_block_free(block);
}

void arena_free(Arena *arena) {
  ASSERT(arena->temp_count == 0);
  while (arena->current_block) {
    b32 is_last_block = arena->current_block->prev == 0;
    memory_block_free_last(arena);
    if (is_last_block) {
      break;
    }
  }
}

static void arena_pop_to(Arena *arena, MemoryBlock *block, u8 *pos) {
  while (arena->current_block != block) {
    memory_block_free_last(arena);
  }
  if (arena->current_block) {
    DEBUG_ASSERT(arena->current_block->pos >= pos);
    arena->current_block->pos = pos;
  }
}

void arena_reset(Arena *arena) { arena_pop_to(arena, 0, 0); }

void *arena_push(Arena *arena, usize size, u32 flags) {
  u8 *result = 0;

  MemoryBlock *block = arena->current_block;
  if (block) {
    u8 *addr = (u8 *)usize_align_pow2((usize)block->pos, kAlign);
    if (addr + size <= block->end) {
      result = addr;
    }
  }

  if (!result) {
    MemoryBlock *new_block = memory_block_alloc(size);
    new_block->prev = arena->current_block;
    block = new_block;
    arena->current_block = block;

    result = (u8 *)usize_align_pow2((usize)block->pos, kAlign);
  }

  DEBUG_ASSERT(result + size <= block->end);
  block->pos = result + size;

  if (!(flags & kArenaPushNoZero)) {
    memory_zero(result, size);
  }

  return result;
}

void arena_pop(Arena *arena, usize size) {
  for (;;) {
    MemoryBlock *block = arena->current_block;
    DEBUG_ASSERT(block);
    if (block->pos - size >= block->begin) {
      block->pos -= size;
      break;
    } else {
      size -= block->pos - block->begin;
      memory_block_free_last(arena);
    }
  }
}

TempMemory temp_memory_begin(Arena *arena) {
  TempMemory temp;
  temp.arena = arena;
  temp.block = arena->current_block;
  temp.pos = temp.block ? temp.block->pos : 0;
  ++arena->temp_count;
  return temp;
}

void temp_memory_end(TempMemory temp) {
  Arena *arena = temp.arena;
  arena_pop_to(arena, temp.block, temp.pos);
  DEBUG_ASSERT(arena->temp_count > 0);
  --arena->temp_count;
}

THREAD_LOCAL Arena t_scratch_arenas[2];

Arena *arena_get_scratch(Arena **conflicts, usize len) {
  Arena *result = 0;
  for (u32 i = 0; i < ARRAY_COUNT(t_scratch_arenas); ++i) {
    Arena *candidate = t_scratch_arenas + i;

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
  ASSERT(result);
  return result;
}
