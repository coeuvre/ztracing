#ifndef ZTRACING_SRC_ALLOCATOR_H_
#define ZTRACING_SRC_ALLOCATOR_H_

#include <stddef.h>

// AllocFn defines a generic allocation function signature:
// To allocate a new block: ptr is NULL, old_size is 0, new_size is the size.
// To reallocate a block: ptr is the old block, old_size is its size, new_size
// is the new size. To free a block: ptr is the block, old_size is its size,
// new_size is 0.
typedef void* (*AllocFn)(void* ctx, void* ptr, size_t old_size,
                         size_t new_size);

struct Allocator {
  AllocFn alloc;
  void* ctx;
};

inline void* allocator_alloc(Allocator a, size_t size) {
  return a.alloc(a.ctx, nullptr, 0, size);
}

inline void* allocator_realloc(Allocator a, void* ptr, size_t old_size,
                               size_t new_size) {
  return a.alloc(a.ctx, ptr, old_size, new_size);
}

inline void allocator_free(Allocator a, void* ptr, size_t size) {
  a.alloc(a.ctx, ptr, size, 0);
}

// Default allocator using malloc, realloc, and free.
Allocator allocator_get_default();

#endif  // ZTRACING_SRC_ALLOCATOR_H_
