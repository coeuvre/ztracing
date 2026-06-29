#include "core/darray.h"

#include "core/allocator.h"

void darray_reserve_(void** ptr_ptr, size_t* cap_ptr, size_t new_cap,
                     size_t elem_size, allocator_t* a) {
  if (new_cap > *cap_ptr) {
    *ptr_ptr = allocator_realloc_uninitialized(
        a, *ptr_ptr, *cap_ptr * elem_size, new_cap * elem_size);
    *cap_ptr = new_cap;
  }
}

void darray_deinit_(void** ptr_ptr, size_t* len_ptr, size_t* cap_ptr,
                    size_t elem_size, allocator_t* a) {
  if (*ptr_ptr) {
    allocator_free(a, *ptr_ptr, *cap_ptr * elem_size);
    *ptr_ptr = nullptr;
    *len_ptr = 0;
    *cap_ptr = 0;
  }
}

void* darray_into_array_(void** ptr_ptr, size_t* len_ptr, size_t* cap_ptr,
                         size_t elem_size, allocator_t* a, size_t* out_count) {
  if (out_count) {
    *out_count = *len_ptr;
  }
  darray_compact_(ptr_ptr, len_ptr, cap_ptr, elem_size, a);
  void* res = *ptr_ptr;
  *ptr_ptr = nullptr;
  *len_ptr = 0;
  *cap_ptr = 0;
  return res;
}

void darray_compact_(void** ptr_ptr, size_t* len_ptr, size_t* cap_ptr,
                     size_t elem_size, allocator_t* a) {
  if (*ptr_ptr && *len_ptr < *cap_ptr) {
    if (*len_ptr == 0) {
      allocator_free(a, *ptr_ptr, *cap_ptr * elem_size);
      *ptr_ptr = nullptr;
      *cap_ptr = 0;
    } else {
      *ptr_ptr = allocator_realloc_uninitialized(
          a, *ptr_ptr, *cap_ptr * elem_size, *len_ptr * elem_size);
      *cap_ptr = *len_ptr;
    }
  }
}

size_t darray_grow_capacity_(size_t cap, size_t min_cap) {
  size_t new_cap = cap == 0 ? 8 : cap * 2;
  if (cap > 1024 * 1024) {
    size_t grow_by = cap / 4;
    if (grow_by < 1024 * 1024) {
      grow_by = 1024 * 1024;
    }
    new_cap = cap + grow_by;
  }
  if (new_cap < min_cap) {
    new_cap = min_cap;
  }
  if (new_cap < cap) {
    new_cap = (size_t)-1;
  }
  return new_cap;
}

