#ifndef ZTRACING_SRC_ARRAY_LIST_H_
#define ZTRACING_SRC_ARRAY_LIST_H_

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "src/allocator.h"

template <typename T>
struct ArrayList {
  T* data;
  size_t size;
  size_t capacity;

  T& operator[](size_t index) { return data[index]; }
  const T& operator[](size_t index) const { return data[index]; }
};

inline size_t array_list_calculate_new_capacity(size_t current_capacity,
                                                size_t min_capacity) {
  size_t new_capacity = current_capacity == 0 ? 8 : current_capacity * 2;
  if (current_capacity > 1024 * 1024) {
    size_t grow_by = current_capacity / 4;
    if (grow_by < 1024 * 1024) grow_by = 1024 * 1024;
    new_capacity = current_capacity + grow_by;
  }
  if (new_capacity < min_capacity) new_capacity = min_capacity;
  // Check for overflow of size_t itself.
  if (new_capacity < current_capacity) new_capacity = (size_t)-1;
  return new_capacity;
}

template <typename T>
inline void array_list_reserve(ArrayList<T>* al, Allocator a,
                               size_t new_capacity) {
  if (new_capacity <= al->capacity) return;

  // Check for overflow in size calculation
  if (new_capacity > (size_t)-1 / sizeof(T)) abort();

  void* new_data = allocator_realloc(a, al->data, al->capacity * sizeof(T),
                                     new_capacity * sizeof(T));
  al->data = static_cast<T*>(new_data);
  al->capacity = new_capacity;
}

template <typename T>
inline void array_list_push_back(ArrayList<T>* al, Allocator a, const T& item) {
  if (al->size == al->capacity) {
    size_t new_capacity =
        array_list_calculate_new_capacity(al->capacity, al->size + 1);
    array_list_reserve(al, a, new_capacity);
  }
  al->data[al->size++] = item;
}

template <typename T>
inline void array_list_append(ArrayList<T>* al, Allocator a, const T* items,
                              size_t count) {
  if (al->size + count > al->capacity) {
    size_t new_capacity =
        array_list_calculate_new_capacity(al->capacity, al->size + count);
    array_list_reserve(al, a, new_capacity);
  }
  memcpy(al->data + al->size, items, count * sizeof(T));
  al->size += count;
}

template <typename T>
inline void array_list_pop_back(ArrayList<T>* al) {
  if (al->size > 0) {
    al->size--;
  }
}

template <typename T>
inline void array_list_clear(ArrayList<T>* al) {
  al->size = 0;
}

template <typename T>
inline void array_list_deinit(ArrayList<T>* al, Allocator a) {
  if (al->data != nullptr) {
    allocator_free(a, al->data, al->capacity * sizeof(T));
  }
  memset(al, 0, sizeof(ArrayList<T>));
}

#endif  // ZTRACING_SRC_ARRAY_LIST_H_
