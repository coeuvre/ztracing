#include "src/memory.h"

#include <stdlib.h>

#include "src/assert.h"
#include "src/types.h"

static inline void *AlignPow2(void *ptr, usize align) {
  void *result = (void *)(((usize)ptr + align - 1) & (~(align - 1)));
  return result;
}

static inline usize NextPow2USize(usize value) {
  usize result = 1;
  while (result <= value) {
    result <<= 1;
  }
  return result;
}

static usize g_allocated_bytes;

void *AllocMemory(usize size) {
  g_allocated_bytes += size;
  void *result = malloc(size);
  ASSERT(result);
  return result;
}

void FreeMemory(void *ptr, usize size) {
  g_allocated_bytes -= size;
  free(ptr);
}

usize GetAllocatedBytes(void) { return g_allocated_bytes; }

const usize kInitMemoryBlockSize = KB(4);

static MemoryBlock *AllocMemoryBlock(usize size) {
  usize block_size =
      MaxUSize(NextPow2USize(sizeof(MemoryBlock) + size), kInitMemoryBlockSize);

  MemoryBlock *block = (MemoryBlock *)AllocMemory(block_size);
  block->prev = 0;
  block->begin = (u8 *)(block + 1);
  block->end = (u8 *)block + block_size;
  block->pos = block->begin;
  return block;
}

static void FreeMemoryBlock(MemoryBlock *block) {
  usize block_size = block->end - (u8 *)block;
  FreeMemory(block, block_size);
}

static void FreeLastMemoryBlock(Arena *arena) {
  MemoryBlock *block = arena->current_block;
  arena->current_block = block->prev;
  FreeMemoryBlock(block);
}

void FreeArena(Arena *arena) {
  ASSERT(arena->temp_count == 0);
  while (arena->current_block) {
    b32 is_last_block = arena->current_block->prev == 0;
    FreeLastMemoryBlock(arena);
    if (is_last_block) {
      break;
    }
  }
}

static void PopArenaTo(Arena *arena, MemoryBlock *block, u8 *pos) {
  while (arena->current_block != block) {
    FreeLastMemoryBlock(arena);
  }
  if (arena->current_block) {
    DEBUG_ASSERT(arena->current_block->pos >= pos);
    arena->current_block->pos = pos;
  }
}

void ResetArena(Arena *arena) { PopArenaTo(arena, 0, 0); }

void *PushArena(Arena *arena, usize size, u32 flags) {
  u8 *result = 0;
  usize align = 8;

  MemoryBlock *block = arena->current_block;
  if (block) {
    result = (u8 *)AlignPow2(block->pos, align);
    if (result + size > block->end) {
      result = 0;
    }
  }

  if (!result) {
    MemoryBlock *new_block = AllocMemoryBlock(size);
    new_block->prev = arena->current_block;
    block = new_block;

    arena->current_block = block;
    result = (u8 *)AlignPow2(block->pos, align);
  }

  DEBUG_ASSERT(result + size <= block->end);
  block->pos = result + size;

  if (!(flags & kPushArenaNoZero)) {
    ZeroMemory(result, size);
  }

  return result;
}

void PopArena(Arena *arena, usize size) {
  for (;;) {
    MemoryBlock *block = arena->current_block;
    DEBUG_ASSERT(block);
    if (block->pos - size >= block->begin) {
      block->pos -= size;
      break;
    } else {
      size -= block->pos - block->begin;
      FreeLastMemoryBlock(arena);
    }
  }
}

TempMemory BeginTempMemory(Arena *arena) {
  TempMemory temp;
  temp.arena = arena;
  temp.block = arena->current_block;
  temp.pos = temp.block ? temp.block->pos : 0;
  ++arena->temp_count;
  return temp;
}

void EndTempMemory(TempMemory temp) {
  Arena *arena = temp.arena;
  PopArenaTo(arena, temp.block, temp.pos);
  DEBUG_ASSERT(arena->temp_count > 0);
  --arena->temp_count;
}

thread_local Arena t_scratch_arenas[2];

Arena *GetScratchArena(Arena **conflicts, usize len) {
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
