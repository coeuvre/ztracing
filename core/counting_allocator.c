#include "core/counting_allocator.h"

static void* counting_alloc(allocator_t* self, size_t size, size_t alignment) {
  counting_allocator_t* ca = (counting_allocator_t*)self;
  void* ptr = ca->backing->alloc(ca->backing, size, alignment);
  if (ptr) {
    atomic_fetch_add_explicit(&ca->allocated_bytes, size, memory_order_relaxed);
  }
  return ptr;
}

static void* counting_realloc(allocator_t* self, void* ptr, size_t old_size,
                              size_t new_size, size_t alignment) {
  counting_allocator_t* ca = (counting_allocator_t*)self;
  void* new_ptr =
      ca->backing->realloc(ca->backing, ptr, old_size, new_size, alignment);
  if (new_ptr || new_size == 0) {
    ptrdiff_t diff = (ptrdiff_t)new_size - (ptrdiff_t)old_size;
    if (diff > 0) {
      atomic_fetch_add_explicit(&ca->allocated_bytes, (size_t)diff,
                                memory_order_relaxed);
    } else if (diff < 0) {
      atomic_fetch_sub_explicit(&ca->allocated_bytes, (size_t)(-diff),
                                memory_order_relaxed);
    }
  }
  return new_ptr;
}

static void counting_dealloc(allocator_t* self, void* ptr, size_t size,
                             size_t alignment) {
  counting_allocator_t* ca = (counting_allocator_t*)self;
  ca->backing->dealloc(ca->backing, ptr, size, alignment);
  atomic_fetch_sub_explicit(&ca->allocated_bytes, size, memory_order_relaxed);
}

void counting_allocator_init(counting_allocator_t* ca, allocator_t* backing) {
  ca->super.alloc = counting_alloc;
  ca->super.realloc = counting_realloc;
  ca->super.dealloc = counting_dealloc;
  ca->super.beg = nullptr;
  ca->super.end = nullptr;
  ca->super.page_size = 0;
  ca->backing = backing;
  atomic_init(&ca->allocated_bytes, 0);
}

size_t counting_allocator_get_allocated_bytes(counting_allocator_t* ca) {
  return atomic_load_explicit(&ca->allocated_bytes, memory_order_relaxed);
}
