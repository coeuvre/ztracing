#include "src/allocator.h"

#include <stdio.h>
#include <stdlib.h>

#include "src/logging.h"

static void* allocator_default_alloc(void* ctx, void* ptr, size_t old_size,
                                     size_t new_size) {
  (void)ctx;
  (void)old_size;
  void* result = nullptr;
  if (new_size == 0) {
    free(ptr);
  } else {
    void* new_ptr = realloc(ptr, new_size);
    if (new_ptr == nullptr) {
      LOG_ERROR("out of memory (requesting %zu bytes)", new_size);
      abort();
    }
    result = new_ptr;
  }
  return result;
}

allocator_t allocator_get_default() {
  allocator_t a = {allocator_default_alloc, nullptr};
  return a;
}

static void* counting_alloc(void* ctx, void* ptr, size_t old_size,
                            size_t new_size) {
  counting_allocator_t* ca = (counting_allocator_t*)ctx;
  void* new_ptr = ca->parent.alloc(ca->parent.ctx, ptr, old_size, new_size);

  if (ptr == nullptr) {
    // Allocation
    if (new_ptr != nullptr) {
      atomic_fetch_add_explicit(&ca->allocated_bytes, new_size,
                                memory_order_relaxed);
    }
  } else if (new_size == 0) {
    // Free
    atomic_fetch_sub_explicit(&ca->allocated_bytes, old_size,
                              memory_order_relaxed);
  } else {
    // Reallocation
    if (new_ptr != nullptr) {
      atomic_fetch_sub_explicit(&ca->allocated_bytes, old_size,
                                memory_order_relaxed);
      atomic_fetch_add_explicit(&ca->allocated_bytes, new_size,
                                memory_order_relaxed);
    }
  }

  return new_ptr;
}

counting_allocator_t counting_allocator_init(allocator_t parent) {
  counting_allocator_t ca = {.parent = parent, .allocated_bytes = 0};
  return ca;
}

allocator_t counting_allocator_get_allocator(counting_allocator_t* ca) {
  allocator_t a = {counting_alloc, ca};
  return a;
}

size_t counting_allocator_get_allocated_bytes(counting_allocator_t* ca) {
  size_t bytes =
      atomic_load_explicit(&ca->allocated_bytes, memory_order_relaxed);
  return bytes;
}
