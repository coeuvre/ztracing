#include "src/memory.h"

#include <stdbool.h>
#include <stdlib.h>

#include "src/assert.h"
#include "src/types.h"

typedef struct MemoryBlock MemoryBlock;
struct MemoryBlock {
  MemoryBlock *first;
  MemoryBlock *prev;
  MemoryBlock *next;
  u8 *begin;
  u8 *end;
};

#define PAGE_SIZE KB(4)
#define ALIGN 8

static inline usize usize_align_pow2(usize addr, usize align) {
  usize result = ((addr + align - 1) & (~(align - 1)));
  return result;
}

// TODO: thread-safe
static usize g_allocated_bytes;

void *memory_alloc(usize size) {
  void *result = memory_alloc_no_zero(size);
  memory_zero(result, size);
  return result;
}

void *memory_alloc_no_zero(usize size) {
  usize total_size = size + sizeof(usize);
  g_allocated_bytes += total_size;
  usize *result = malloc(total_size);
  ASSERT(result);
  *result = size;
  return result + 1;
}

void memory_free(void *ptr, usize size) {
  usize total_size = size + sizeof(usize);
  g_allocated_bytes -= total_size;

  usize *result = ((usize *)ptr) - 1;
  ASSERTF(*result == size,
          "free size doesn't match allocation size: frees %d, but allocated %d",
          (int)*result, size);
  free(result);
}

usize memory_get_allocated_bytes(void) { return g_allocated_bytes; }

// Allocate a memory block which is at least `size` bytes large.
static MemoryBlock *memory_block_alloc(usize size) {
  usize block_size = usize_align_pow2(size + sizeof(MemoryBlock), PAGE_SIZE);

  u8 *memory = memory_alloc_no_zero(block_size);
  u8 *end = memory + block_size;
  MemoryBlock *block = (MemoryBlock *)(end - sizeof(MemoryBlock));
  block->prev = 0;
  block->next = 0;
  block->begin = memory;
  block->end = (u8 *)block;
  return block;
}

static inline void memory_block_free(MemoryBlock *block) {
  usize block_size = block->end - block->begin + sizeof(MemoryBlock);
  memory_free(block->begin, block_size);
}

static inline MemoryBlock *arena_get_memory_block(Arena *arena) {
  return (MemoryBlock *)arena->end;
}

void arena_free(Arena *arena) {
  MemoryBlock *block = arena_get_memory_block(arena);

  for (MemoryBlock *tail = block ? block->next : 0; tail;) {
    MemoryBlock *next = tail->next;
    memory_block_free(tail);
    tail = next;
  }

  while (block) {
    MemoryBlock *prev = block->prev;
    memory_block_free(block);
    block = prev;
  }

  *arena = (Arena){0};
}

void arena_clear(Arena *arena) {
  MemoryBlock *block = arena_get_memory_block(arena);
  if (!block) {
    return;
  }
  while (block->prev) {
    block = block->prev;
  }
  arena->begin = block->begin;
  arena->end = block->end;
}

void *arena_push(Arena *arena, usize size, u32 flags) {
  u8 *p = (u8 *)usize_align_pow2((usize)arena->begin, ALIGN);
  while (!p || p + size > arena->end) {
    MemoryBlock *memory_block = arena_get_memory_block(arena);
    MemoryBlock *next_memory_block = memory_block ? memory_block->next : 0;
    if (!next_memory_block) {
      next_memory_block = memory_block_alloc(size);
      next_memory_block->prev = memory_block;
      if (memory_block) {
        next_memory_block->first = memory_block->first;
        memory_block->next = next_memory_block;
      } else {
        next_memory_block->first = next_memory_block;
      }
    }

    arena->begin = next_memory_block->begin;
    arena->end = next_memory_block->end;

    p = (u8 *)usize_align_pow2((usize)arena->begin, ALIGN);
  }

  arena->begin = p + size;

  if (!(flags & ARENA_PUSH_NO_ZERO)) {
    memory_zero(p, size);
  }

  return p;
}

void *arena_pop(Arena *arena, usize size) {
  MemoryBlock *block = arena_get_memory_block(arena);
  while (block) {
    u8 *new_begin = arena->begin - size;
    if (new_begin >= block->begin) {
      arena->begin = new_begin;
      return new_begin;
    } else {
      size -= arena->begin - block->begin;
      block = block->prev;
      if (!block) {
        arena->begin = arena->end = 0;
        return 0;
      }
      arena->begin = arena->end = block->end;
    }
  }
  UNREACHABLE;
}

void *arena_seek(Arena *arena, usize size) {
  Arena temp = *arena;
  arena_pop(&temp, size);
  return temp.begin;
}

bool arena_is_empty(Arena *arena) {
  MemoryBlock *block = arena_get_memory_block(arena);
  if (!block) {
    return true;
  }

  if (block->prev) {
    return false;
  }

  return arena->begin == block->begin;
}

static bool arena_overlap(Arena *a, Arena *b) {
  MemoryBlock *a_block = arena_get_memory_block(a);
  MemoryBlock *b_block = arena_get_memory_block(b);
  if (!(a_block && b_block)) {
    return false;
  }

  return a_block->first == b_block->first;
}

THREAD_LOCAL Arena t_scrach_arenas[2];

Scratch scratch_begin(Arena **conflicts, usize len) {
  for (u32 scratch_arena_index = 0;
       scratch_arena_index < ARRAY_COUNT(t_scrach_arenas);
       ++scratch_arena_index) {
    Arena *scratch = t_scrach_arenas + scratch_arena_index;

    bool overlap = false;
    for (u32 conflict_index = 0; conflict_index < len; ++conflict_index) {
      Arena *conflict = conflicts[conflict_index];
      if (arena_overlap(scratch, conflict)) {
        overlap = true;
        break;
      }
    }

    if (!overlap) {
      if (!scratch->begin) {
        arena_push(scratch, 4, 0);
        arena_pop(scratch, 4);
      }
      return (Scratch){
          .checkpoint = *scratch,
          .arena = scratch,
      };
    }
  }

  UNREACHABLE;
}

void scratch_free(void) {
  for (u32 scratch_arena_index = 0;
       scratch_arena_index < ARRAY_COUNT(t_scrach_arenas);
       ++scratch_arena_index) {
    Arena *scratch = t_scrach_arenas + scratch_arena_index;
    arena_free(scratch);
  }
}
