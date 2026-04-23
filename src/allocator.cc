#include "src/allocator.h"

#include <stdio.h>
#include <stdlib.h>

#include "src/logging.h"

static void* allocator_default_alloc(void* ctx, void* ptr, size_t old_size,
                                     size_t new_size) {
  (void)ctx;
  (void)old_size;
  if (new_size == 0) {
    free(ptr);
    return nullptr;
  }
  void* new_ptr = realloc(ptr, new_size);
  if (new_ptr == nullptr) {
    LOG_ERROR("out of memory (requesting %zu bytes)", new_size);
    abort();
  }
  return new_ptr;
}

Allocator allocator_get_default() { return {allocator_default_alloc, nullptr}; }

static void* counting_alloc(void* ctx, void* ptr, size_t old_size,
                            size_t new_size) {
  CountingAllocator* ca = (CountingAllocator*)ctx;
  void* new_ptr = ca->parent.alloc(ca->parent.ctx, ptr, old_size, new_size);

  if (ptr == nullptr) {
    // Allocation
    if (new_ptr != nullptr) {
      ca->allocated_bytes.fetch_add(new_size, std::memory_order_relaxed);
    }
  } else if (new_size == 0) {
    // Free
    ca->allocated_bytes.fetch_sub(old_size, std::memory_order_relaxed);
  } else {
    // Reallocation
    if (new_ptr != nullptr) {
      ca->allocated_bytes.fetch_sub(old_size, std::memory_order_relaxed);
      ca->allocated_bytes.fetch_add(new_size, std::memory_order_relaxed);
    }
  }

  return new_ptr;
}

void counting_allocator_init(CountingAllocator* ca, Allocator parent) {
  ca->parent = parent;
  ca->allocated_bytes.store(0, std::memory_order_relaxed);
}

Allocator counting_allocator_get_allocator(CountingAllocator* ca) {
  return {counting_alloc, ca};
}

size_t counting_allocator_get_allocated_bytes(CountingAllocator* ca) {
  return ca->allocated_bytes.load(std::memory_order_relaxed);
}
