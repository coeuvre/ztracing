#include "src/allocator.h"

#include <stdlib.h>

static void* allocator_default_alloc(void* ctx, void* ptr, size_t old_size,
                                     size_t new_size) {
  (void)ctx;
  (void)old_size;
  if (new_size == 0) {
    free(ptr);
    return nullptr;
  }
  return realloc(ptr, new_size);
}

Allocator allocator_get_default() { return {allocator_default_alloc, nullptr}; }
