#include "core/arena.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Minimum additional chunk size (64 KB).
#define ARENA_MIN_CHUNK ((size_t)(64 * 1024))

// ─── helpers ────────────────────────────────────────────────────────────────

// Round a size up to multiples of ARENA_MIN_CHUNK, then (if the backing
// allocator is a page allocator) to page_size.
static size_t backing_round_alloc_size(allocator_t* backing, size_t size) {
  size_t mc = ARENA_MIN_CHUNK;
  size = (size + mc - 1) & ~(mc - 1);
  if (backing->page_size != 0) {
    size_t ps = backing->page_size;
    size = (size + ps - 1) & ~(ps - 1);
  }
  return size;
}

// ─── chunk helpers ──────────────────────────────────────────────────────────

// Create a standalone chunk (arena struct is NOT embedded in it).
// Memory is obtained from a->backing.
static arena_chunk_t* chunk_create(arena_t* a, size_t data_size) {
  if (data_size > SIZE_MAX - sizeof(arena_chunk_t)) {
    fprintf(stderr, "arena: chunk size overflow\n");
    abort();
  }
  size_t total =
      backing_round_alloc_size(a->backing, sizeof(arena_chunk_t) + data_size);
  void* mem = allocator_alloc_uninitialized(a->backing, total);
  arena_chunk_t* c = (arena_chunk_t*)mem;
  c->data = (char*)mem + sizeof(arena_chunk_t);
  c->size = total - sizeof(arena_chunk_t);
  c->next = nullptr;
  c->prev = nullptr;
  return c;
}

// The first chunk is embedded right after the arena struct.
arena_chunk_t* arena_get_first_chunk(arena_t* a) {
  return (arena_chunk_t*)((char*)a + sizeof(arena_t));
}

static void chunk_free_all(arena_t* a) {
  arena_chunk_t* first = arena_get_first_chunk(a);
  arena_chunk_t* cur = first->next;
  while (cur) {
    arena_chunk_t* next = cur->next;
    allocator_free(a->backing, cur, sizeof(arena_chunk_t) + cur->size);
    cur = next;
  }
}

// ─── allocator callbacks (slow-path) ────────────────────────────────────────

static void* arena_alloc(allocator_t* self, size_t size, size_t alignment) {
  arena_t* a = (arena_t*)self;

  if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
    fprintf(stderr, "Invalid alignment: %zu\n", alignment);
    abort();
  }

  // Advance through pre-existing free chunks (e.g. after a reset).
  // Chunks are appended at the tail, so current->next is always a
  // free chunk — either created fresh or reclaimed from a reset.
  if (a->current->next) {
    a->current = a->current->next;
    a->super.beg = a->current->data;
    a->super.end = a->current->data + a->current->size;
    return allocator_alloc_align_uninitialized(self, size, alignment);
  }

  // No more free chunks — create a new one and append at the tail.
  size_t prev_size = a->current->size;
  size_t data_size = prev_size * 2;
  size_t min_data = size + alignment - 1;
  if (data_size < min_data) {
    data_size = min_data;
  }
  if (data_size < ARENA_MIN_CHUNK) {
    data_size = ARENA_MIN_CHUNK;
  }

  arena_chunk_t* c = chunk_create(a, data_size);
  // Append at the tail (current is always the last chunk).
  a->current->next = c;
  c->prev = a->current;
  a->current = c;
  a->super.beg = c->data;
  a->super.end = c->data + c->size;

  a->peak += sizeof(arena_chunk_t) + c->size;

  return allocator_alloc_align_uninitialized(self, size, alignment);
}

static void* arena_realloc(allocator_t* self, void* ptr, size_t old_size,
                           size_t new_size, size_t alignment) {
  if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
    fprintf(stderr, "Invalid alignment: %zu\n", alignment);
    abort();
  }

  void* new_ptr =
      allocator_alloc_align_uninitialized(self, new_size, alignment);
  if (new_ptr && old_size > 0) {
    memcpy(new_ptr, ptr, old_size < new_size ? old_size : new_size);
  }
  return new_ptr;
}

static void arena_free(allocator_t* self, void* ptr, size_t size,
                       size_t alignment) {
  (void)self;
  (void)ptr;
  (void)size;
  (void)alignment;
}

// ─── lifecycle ──────────────────────────────────────────────────────────────

arena_t* arena_create_with_allocator(allocator_t* backing) {
  // ARENA_MIN_CHUNK must be a power of two for the round-up arithmetic.
  if (ARENA_MIN_CHUNK == 0 || (ARENA_MIN_CHUNK & (ARENA_MIN_CHUNK - 1)) != 0) {
    fprintf(stderr, "arena: ARENA_MIN_CHUNK %zu is not a power of two\n",
            ARENA_MIN_CHUNK);
    abort();
  }
  if (backing->page_size != 0 &&
      (backing->page_size & (backing->page_size - 1)) != 0) {
    fprintf(stderr,
            "arena: backing allocator page_size %zu is not a power of two\n",
            backing->page_size);
    abort();
  }

  size_t total = backing_round_alloc_size(backing, ARENA_MIN_CHUNK);

  void* mem = allocator_alloc_uninitialized(backing, total);
  arena_t* a = (arena_t*)mem;
  arena_chunk_t* c = (arena_chunk_t*)((char*)mem + sizeof(arena_t));
  c->data = (char*)mem + sizeof(arena_t) + sizeof(arena_chunk_t);
  c->size = total - sizeof(arena_t) - sizeof(arena_chunk_t);
  c->next = nullptr;
  c->prev = nullptr;

  a->backing = backing;
  a->current = c;
  a->peak = total;

  a->super.alloc = arena_alloc;
  a->super.realloc = arena_realloc;
  a->super.dealloc = arena_free;
  a->super.beg = c->data;
  a->super.end = c->data + c->size;
  a->super.page_size = 0;

  return a;
}

arena_t* arena_create(void) {
  return arena_create_with_allocator(page_allocator());
}

void arena_destroy(arena_t* a) {
  chunk_free_all(a);
  arena_chunk_t* first = arena_get_first_chunk(a);
  size_t total = sizeof(arena_t) + sizeof(arena_chunk_t) + first->size;
  allocator_free(a->backing, a, total);
}

void arena_reset(arena_t* a) {
  arena_chunk_t* first = arena_get_first_chunk(a);

  // With append-at-tail, a->head is always first.  0 or 1 extra chunk
  // means first->next is nullptr or first->next->next is nullptr.
  if (first->next == nullptr || first->next->next == nullptr) {
    a->current = first;
    a->super.beg = first->data;
    a->super.end = first->data + first->size;
    return;
  }

  // Many extra chunks — consolidate into one large chunk.
  chunk_free_all(a);
  first->next = nullptr;

  size_t first_total = sizeof(arena_t) + sizeof(arena_chunk_t) + first->size;
  if (a->peak > first_total) {
    size_t data_size = a->peak - first_total;
    if (data_size < ARENA_MIN_CHUNK) {
      data_size = ARENA_MIN_CHUNK;
    }
    arena_chunk_t* c = chunk_create(a, data_size);
    c->prev = first;
    first->next = c;
  }

  a->current = first;
  a->super.beg = first->data;
  a->super.end = first->data + first->size;

  // Peak reflects the actual post-reset allocation (≥ old peak).
  size_t new_peak = first_total;
  if (first->next) {
    new_peak += sizeof(arena_chunk_t) + first->next->size;
  }
  if (new_peak > a->peak) {
    a->peak = new_peak;
  }
}

size_t arena_get_capacity(arena_t* a) {
  return (size_t)(a->super.end - a->super.beg);
}

arena_checkpoint_t arena_get_checkpoint(arena_t* a) {
  arena_checkpoint_t cp;
  cp.chunk = a->current;
  cp.beg = a->super.beg;
  return cp;
}

void arena_reset_to_checkpoint(arena_t* a, arena_checkpoint_t checkpoint) {
  a->current = checkpoint.chunk;
  a->super.beg = checkpoint.beg;
  a->super.end = checkpoint.chunk->data + checkpoint.chunk->size;
}
