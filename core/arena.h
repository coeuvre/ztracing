#ifndef CORE_ARENA_H
#define CORE_ARENA_H
#include <stddef.h>
#include <stdint.h>

#include "core/allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct arena_chunk {
  char* data;
  size_t size;
  struct arena_chunk* next;
  struct arena_chunk* prev;
} arena_chunk_t;

typedef struct arena {
  allocator_t super;     // MUST be first — enables (allocator_t*)a casting
  allocator_t* backing;  // Allocator used for chunk memory
  arena_chunk_t* current;
  size_t peak;  // Peak memory usage (cumulative allocated chunk size)
} arena_t;

// Thread safety: an arena is NOT internally synchronised.  A single arena
// must be accessed by only one thread at a time.  The typical pattern is
// to create, fill, and then hand off ownership to another thread (which
// may destroy it) — never to share a live arena between threads
// concurrently.

// Create an arena backed by the page allocator.  The arena struct and
// initial chunk are allocated via page_allocator().  Grows on demand.
// OOM → abort.
arena_t* arena_create(void);

// Create an arena with a specific backing allocator.  Chunk allocation
// requests are always rounded up to ARENA_MIN_CHUNK (64 KB) multiples.
// If the backing allocator is a page allocator (page_size != 0), the
// request is also rounded up to page_size.
arena_t* arena_create_with_allocator(allocator_t* backing);

// Destroy the arena, freeing all chunks and the arena struct.
void arena_destroy(arena_t* a);

// Reset the arena: reclaim all memory and prepare for reuse.
void arena_reset(arena_t* a);

typedef struct arena_checkpoint {
  arena_chunk_t* chunk;
  char* beg;
} arena_checkpoint_t;

// Get a checkpoint of the current arena state.
arena_checkpoint_t arena_get_checkpoint(arena_t* a);

// Restore the arena to a previously taken checkpoint.
// Any allocations made after the checkpoint was taken are invalidated.
// Pre-existing chunks allocated after the checkpoint's chunk are preserved in
// the list and will be reused on subsequent allocations.
void arena_reset_to_checkpoint(arena_t* a, arena_checkpoint_t checkpoint);

// Return the usable capacity of the current chunk.
size_t arena_get_capacity(arena_t* a);

// Return a pointer to the first (embedded) chunk.  This is the head of
// the chunk list — with append-at-tail it never changes.
arena_chunk_t* arena_get_first_chunk(arena_t* a);

// Return an struct allocator* backed by this arena.  The returned pointer is
// simply (struct allocator*)a — the arena itself is the allocator.
static inline allocator_t* arena_get_allocator(arena_t* a) {
  return (allocator_t*)a;
}

#ifdef __cplusplus
}
#endif

#endif  // CORE_ARENA_H
