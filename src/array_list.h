#ifndef ZTRACING_SRC_ARRAY_LIST_H_
#define ZTRACING_SRC_ARRAY_LIST_H_

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "src/allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct array_list {
  void* ptr;
  size_t len;
  size_t cap;
  size_t elem_size;
} array_list_t;

inline array_list_t array_list_init(size_t elem_size) {
  array_list_t al = {.elem_size = elem_size};
  return al;
}

inline size_t array_list_calculate_new_capacity(size_t current_capacity,
                                                size_t min_capacity) {
  size_t new_capacity = current_capacity == 0 ? 8 : current_capacity * 2;
  if (current_capacity > 1024 * 1024) {
    size_t grow_by = current_capacity / 4;
    if (grow_by < 1024 * 1024) {
      grow_by = 1024 * 1024;
    }
    new_capacity = current_capacity + grow_by;
  }
  if (new_capacity < min_capacity) {
    new_capacity = min_capacity;
  }
  // Check for overflow of size_t itself.
  if (new_capacity < current_capacity) {
    new_capacity = (size_t)-1;
  }
  return new_capacity;
}

inline void array_list_reserve(array_list_t* al, size_t new_capacity,
                               allocator_t a) {
  if (new_capacity > al->cap) {
    // Check for overflow in size calculation
    if (new_capacity > (size_t)-1 / al->elem_size) {
      abort();
    }

    void* new_ptr = allocator_realloc(a, al->ptr, al->cap * al->elem_size,
                                      new_capacity * al->elem_size);
    al->ptr = new_ptr;
    al->cap = new_capacity;
  }
}

inline void* array_list_push(array_list_t* al, allocator_t a) {
  if (al->len == al->cap) {
    size_t new_capacity =
        array_list_calculate_new_capacity(al->cap, al->len + 1);
    array_list_reserve(al, new_capacity, a);
  }
  void* slot = (char*)al->ptr + al->len * al->elem_size;
  al->len++;
  return slot;
}

inline void* array_list_pop(array_list_t* al) {
  void* slot = nullptr;
  if (al->len > 0) {
    al->len--;
    slot = (char*)al->ptr + al->len * al->elem_size;
  }
  return slot;
}

inline void array_list_clear(array_list_t* al) { al->len = 0; }

inline void array_list_resize(array_list_t* al, size_t new_size,
                              allocator_t a) {
  if (new_size > al->cap) {
    array_list_reserve(al, new_size, a);
  }
  al->len = new_size;
}

inline void array_list_deinit(array_list_t* al, allocator_t a) {
  if (al->ptr != nullptr) {
    allocator_free(a, al->ptr, al->cap * al->elem_size);
  }
  array_list_t empty = {};
  *al = empty;
}

inline void* array_list_get(const array_list_t* al, size_t index) {
  void* result = (char*)al->ptr + index * al->elem_size;
  return result;
}

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
// C++ Template compatibility wrapper
template <typename T>
struct ArrayList {
  T* data;
  size_t size;
  size_t capacity;
  size_t element_size;

  T& operator[](size_t index) { return data[index]; }
  const T& operator[](size_t index) const { return data[index]; }
};

template <typename T>
inline void array_list_reserve(ArrayList<T>* al, allocator_t a,
                               size_t new_capacity) {
  al->element_size = sizeof(T);
  array_list_reserve(reinterpret_cast<array_list_t*>(al), new_capacity, a);
}

template <typename T>
inline void array_list_push_back(ArrayList<T>* al, allocator_t a,
                                 const T& item) {
  al->element_size = sizeof(T);
  void* slot = array_list_push(reinterpret_cast<array_list_t*>(al), a);
  *static_cast<T*>(slot) = item;
}

template <typename T>
inline void array_list_append(ArrayList<T>* al, allocator_t a, const T* items,
                              size_t count) {
  if (count > 0) {
    al->element_size = sizeof(T);
    array_list_t* impl = reinterpret_cast<array_list_t*>(al);
    array_list_resize(impl, impl->len + count, a);
    char* dst = (char*)impl->ptr + (impl->len - count) * impl->elem_size;
    memcpy(dst, items, count * impl->elem_size);
  }
}

template <typename T>
inline void array_list_pop_back(ArrayList<T>* al) {
  (void)array_list_pop(reinterpret_cast<array_list_t*>(al));
}

template <typename T>
inline void array_list_clear(ArrayList<T>* al) {
  array_list_clear(reinterpret_cast<array_list_t*>(al));
}

template <typename T>
inline void array_list_resize(ArrayList<T>* al, allocator_t a,
                              size_t new_size) {
  al->element_size = sizeof(T);
  array_list_resize(reinterpret_cast<array_list_t*>(al), new_size, a);
}

template <typename T>
inline void array_list_deinit(ArrayList<T>* al, allocator_t a) {
  al->element_size = sizeof(T);
  array_list_deinit(reinterpret_cast<array_list_t*>(al), a);
}
#endif

#endif  // ZTRACING_SRC_ARRAY_LIST_H_
