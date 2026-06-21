#ifndef ZTRACING_SRC_ARENA_H_
#define ZTRACING_SRC_ARENA_H_

#include <stddef.h>

#include "src/allocator.h"

typedef struct arena_block arena_block_t;

typedef struct arena {
  allocator_t backing_allocator;
  arena_block_t* first_block;
  arena_block_t* current_block;
  size_t block_size;
} arena_t;

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the arena. Uses backing_allocator to allocate internal blocks.
// If block_size is 0, a default block size (e.g. 64KB) is used.
void arena_init(arena_t* arena, allocator_t backing_allocator,
                size_t block_size);

// Deallocate all blocks in the arena. The arena itself is NOT freed.
void arena_deinit(arena_t* arena);

// Allocate 'size' bytes from the arena. Returns nullptr on failure.
// Guaranteed to return 8-byte aligned memory.
void* arena_alloc(arena_t* arena, size_t size);

// Reset the arena, making all previously allocated memory available again.
// This is O(1) and does not free the backing blocks.
void arena_reset(arena_t* arena);

// Get an allocator_t interface that wraps this arena.
allocator_t arena_get_allocator(arena_t* arena);

#ifdef __cplusplus
}
#endif

#endif  // ZTRACING_SRC_ARENA_H_
