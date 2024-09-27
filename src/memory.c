#include "src/memory.h"

#include <stdlib.h>

#include "src/assert.h"
#include "src/types.h"

static inline void *AlignPow2(void *ptr, usize align) {
  void *result = (void *)(((usize)ptr + align - 1) & (~(align - 1)));
  return result;
}

static inline u32 NextPow2U32(u32 value) {
  u32 result = 1;
  while (result <= value) {
    result <<= 1;
  }
  return result;
}

static MemoryBlock *AllocMemoryBlock(usize size) {
  MemoryBlock *block = (MemoryBlock *)malloc(sizeof(MemoryBlock) + size);
  ASSERT(block);
  block->prev = 0;
  block->begin = (u8 *)(block + 1);
  block->end = block->begin + size;
  block->pos = block->begin;
  return block;
}

const usize kInitMemoryBlockSize = KB(4);

Arena *AllocArena(void) {
  MemoryBlock *block = AllocMemoryBlock(kInitMemoryBlockSize);

  Arena *result = (Arena *)block->pos;
  result->current_block = block;
  result->temp_count = 0;

  block->pos = (u8 *)(result + 1);
  ASSERT(block->pos <= block->end);

  return result;
}

static void FreeLastBlock(Arena *arena) {
  MemoryBlock *free_block = arena->current_block;
  arena->current_block = free_block->prev;
  free(free_block);
}

void FreeArena(Arena *arena) {
  ASSERT(arena->temp_count == 0);
  while (arena->current_block) {
    b32 is_last_block = arena->current_block->prev == 0;
    FreeLastBlock(arena);
    if (is_last_block) {
      break;
    }
  }
}

void ResetArena(Arena *arena) {
  while (arena->current_block->begin != (u8 *)arena) {
    FreeLastBlock(arena);
  }
  arena->current_block->pos = arena->current_block->begin + sizeof(Arena);
}

void *PushArena(Arena *arena, usize size, u32 flags) {
  usize align = 8;
  MemoryBlock *block = arena->current_block;
  DEBUG_ASSERT(block);

  u8 *result = (u8 *)AlignPow2(block->pos, align);
  if (result + size > block->end) {
    usize wanted_size = NextPow2U32(size);
    usize block_size = MAX(wanted_size, kInitMemoryBlockSize);
    MemoryBlock *new_block = AllocMemoryBlock(block_size);
    new_block->prev = block;
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
  MemoryBlock *block = arena->current_block;
  for (;;) {
    DEBUG_ASSERT(block);
    if (block->pos - size >= block->begin) {
      block->pos -= size;
      break;
    } else {
      size -= block->pos - block->begin;
      FreeLastBlock(arena);
      block = arena->current_block;
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
  while (arena->current_block != temp.block) {
    FreeLastBlock(arena);
  }

  if (arena->current_block) {
    DEBUG_ASSERT(arena->current_block->pos >= temp.pos);
    arena->current_block->pos = temp.pos;
  }

  DEBUG_ASSERT(arena->temp_count > 0);
  --arena->temp_count;
}

thread_local Arena *t_scratch_arenas[2];

Arena *GetScratchArena(Arena **conflicts, usize len) {
  Arena *result = 0;
  for (u32 i = 0; i < ARRAY_COUNT(t_scratch_arenas); ++i) {
    Arena *candidate = t_scratch_arenas[i];
    if (!candidate) {
      result = AllocArena();
      t_scratch_arenas[i] = result;
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
  ASSERT(result);
  return result;
}
