#ifndef ZTRACING_SRC_ALLOCATOR_H_
#define ZTRACING_SRC_ALLOCATOR_H_

#ifndef __cplusplus
#include <stdatomic.h>
#endif
#include <stddef.h>
#include <stdlib.h>

#include "src/logging.h"

#ifdef __cplusplus
extern "C" {
#endif

// alloc_fn_t defines a generic allocation function signature:
// To allocate a new block: ptr is NULL, old_size is 0, new_size is the size.
// To reallocate a block: ptr is the old block, old_size is its size, new_size
// is the new size. To free a block: ptr is the block, old_size is its size,
// new_size is 0.
typedef void* (*alloc_fn_t)(void* ctx, void* ptr, size_t old_size,
                            size_t new_size);

typedef struct allocator {
  alloc_fn_t alloc;
  void* ctx;
} allocator_t;

static inline void* allocator_alloc(allocator_t a, size_t size) {
  void* ptr = a.alloc(a.ctx, nullptr, 0, size);
  if (size > 0 && ptr == nullptr) {
    LOG_ERROR("Fatal Error: Out of memory allocating %zu bytes.", size);
    abort();
  }
  return ptr;
}

static inline void* allocator_realloc(allocator_t a, void* ptr, size_t old_size,
                                      size_t new_size) {
  void* new_ptr = a.alloc(a.ctx, ptr, old_size, new_size);
  if (new_size > 0 && new_ptr == nullptr) {
    LOG_ERROR("Fatal Error: Out of memory reallocating from %zu to %zu bytes.",
              old_size, new_size);
    abort();
  }
  return new_ptr;
}

static inline void allocator_free(allocator_t a, void* ptr, size_t size) {
  a.alloc(a.ctx, ptr, size, 0);
}

// Default allocator using malloc, realloc, and free.
allocator_t allocator_get_default();

typedef struct counting_allocator {
  allocator_t parent;
#ifdef __cplusplus
  size_t allocated_bytes;
#else
  _Atomic size_t allocated_bytes;
#endif
} counting_allocator_t;

// Returns an initialized counting allocator that wraps a parent allocator.
counting_allocator_t counting_allocator_init(allocator_t parent);

// Returns the allocator_t interface for the given counting allocator.
allocator_t counting_allocator_get_allocator(counting_allocator_t* ca);

// Returns the total number of allocated bytes.
size_t counting_allocator_get_allocated_bytes(counting_allocator_t* ca);

#ifdef __cplusplus
}
#endif

// C++ Compatibility aliases to allow incremental migration of other files
#ifdef __cplusplus
typedef allocator_t Allocator;
typedef counting_allocator_t CountingAllocator;
#endif

#endif  // ZTRACING_SRC_ALLOCATOR_H_
