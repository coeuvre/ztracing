#include "src/arena.h"

#include <stdint.h>
#include <string.h>

#include "src/assert.h"

#define ARENA_DEFAULT_BLOCK_SIZE (64 * 1024)
#define ARENA_ALIGNMENT 8

typedef struct arena_block {
  struct arena_block* next;
  size_t capacity;
  size_t offset;
  uint8_t data[];
} arena_block_t;

static inline size_t align_up(size_t size, size_t alignment) {
  return (size + alignment - 1) & ~(alignment - 1);
}

void arena_init(arena_t* arena, allocator_t backing_allocator,
                size_t block_size) {
  arena->backing_allocator = backing_allocator;
  arena->first_block = nullptr;
  arena->current_block = nullptr;
  arena->block_size = block_size > 0 ? block_size : ARENA_DEFAULT_BLOCK_SIZE;
}

void arena_deinit(arena_t* arena) {
  arena_block_t* block = arena->first_block;
  while (block != nullptr) {
    arena_block_t* next = block->next;
    allocator_free(arena->backing_allocator, block,
                   sizeof(arena_block_t) + block->capacity);
    block = next;
  }
  arena->first_block = nullptr;
  arena->current_block = nullptr;
}

static arena_block_t* arena_new_block(arena_t* arena, size_t capacity) {
  size_t alloc_size = sizeof(arena_block_t) + capacity;
  arena_block_t* block =
      (arena_block_t*)allocator_alloc(arena->backing_allocator, alloc_size);
  if (block == nullptr) return nullptr;
  block->next = nullptr;
  block->capacity = capacity;
  block->offset = 0;
  return block;
}

void* arena_alloc(arena_t* arena, size_t size) {
  if (size == 0) return nullptr;

  size = align_up(size, ARENA_ALIGNMENT);

  arena_block_t* block = arena->current_block;

  // If we have a block, try to allocate from it
  if (block != nullptr) {
    if (block->offset + size <= block->capacity) {
      void* ptr = &block->data[block->offset];
      block->offset += size;
      return ptr;
    }

    // If not enough space, try next block in the chain if it exists
    if (block->next != nullptr) {
      arena->current_block = block->next;
      return arena_alloc(arena, size);
    }
  }

  // No block or no space in the chain. Allocate a new block.
  size_t new_capacity = arena->block_size;
  if (size > new_capacity) {
    new_capacity = size;
  }

  arena_block_t* new_blk = arena_new_block(arena, new_capacity);
  CHECK(new_blk != nullptr);

  if (arena->first_block == nullptr) {
    arena->first_block = new_blk;
  } else {
    // Append to the end of the chain
    arena_block_t* curr = arena->first_block;
    while (curr->next != nullptr) {
      curr = curr->next;
    }
    curr->next = new_blk;
  }

  arena->current_block = new_blk;

  void* ptr = &new_blk->data[new_blk->offset];
  new_blk->offset += size;
  return ptr;
}

void arena_reset(arena_t* arena) {
  arena_block_t* block = arena->first_block;
  while (block != nullptr) {
    block->offset = 0;
    block = block->next;
  }
  arena->current_block = arena->first_block;
}

static void* arena_allocator_fn(void* ctx, void* ptr, size_t old_size,
                                size_t new_size) {
  arena_t* arena = (arena_t*)ctx;

  // 1. Free operation
  if (new_size == 0) {
    return nullptr;
  }

  // 2. Alloc operation
  if (ptr == nullptr) {
    return arena_alloc(arena, new_size);
  }

  // 3. Realloc operation
  if (new_size <= old_size) {
    return ptr;
  }

  void* new_ptr = arena_alloc(arena, new_size);
  if (new_ptr != nullptr && old_size > 0) {
    memcpy(new_ptr, ptr, old_size);
  }
  return new_ptr;
}

allocator_t arena_get_allocator(arena_t* arena) {
  return (allocator_t){
      .alloc = arena_allocator_fn,
      .ctx = arena,
  };
}
