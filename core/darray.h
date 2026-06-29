#ifndef ZTRACING_CORE_DARRAY_H_
#define ZTRACING_CORE_DARRAY_H_

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "core/allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

#define darray_typeof(X) __typeof__(X)

// Macro to define a typed dynamic array container.
// Usage:
//   darray_t(int) my_array = {};
//   typedef darray_t(float) float_array_t;
#define darray_t(T) \
  struct {          \
    T* ptr;         \
    size_t len;     \
    size_t cap;     \
  }

// Floating-point types
typedef darray_t(float) darray_float_t;
typedef darray_t(double) darray_double_t;

// Boolean type
typedef darray_t(bool) darray_bool_t;

// Fixed-width integer types (stdint.h)
typedef darray_t(int8_t) darray_int8_t;
typedef darray_t(uint8_t) darray_uint8_t;
typedef darray_t(int16_t) darray_int16_t;
typedef darray_t(uint16_t) darray_uint16_t;
typedef darray_t(int32_t) darray_int32_t;
typedef darray_t(uint32_t) darray_uint32_t;
typedef darray_t(int64_t) darray_int64_t;
typedef darray_t(uint64_t) darray_uint64_t;

// --- Out-of-line Helpers ---
void darray_reserve_(void** ptr_ptr, size_t* cap_ptr, size_t new_cap,
                     size_t elem_size, allocator_t* a);

void darray_deinit_(void** ptr_ptr, size_t* len_ptr, size_t* cap_ptr,
                    size_t elem_size, allocator_t* a);

void* darray_into_array_(void** ptr_ptr, size_t* len_ptr, size_t* cap_ptr,
                         size_t elem_size, allocator_t* a, size_t* out_count);

void darray_compact_(void** ptr_ptr, size_t* len_ptr, size_t* cap_ptr,
                     size_t elem_size, allocator_t* a);

size_t darray_grow_capacity_(size_t cap, size_t min_cap);

// --- Public API ---

// Deinitializes the array and frees its memory, resetting it to zero value.
#define darray_deinit(da, allocator)                         \
  darray_deinit_((void**)&(da)->ptr, &(da)->len, &(da)->cap, \
                 sizeof(*(da)->ptr), (allocator))

// Pushes a single value to the end of the array.
#define darray_push(da, val, allocator)                                     \
  do {                                                                      \
    if ((da)->len == (da)->cap) {                                           \
      size_t _new_cap = darray_grow_capacity_((da)->cap, (da)->len + 1);    \
      darray_reserve_((void**)&(da)->ptr, &(da)->cap, _new_cap,             \
                      sizeof(*(da)->ptr), (allocator));                     \
    }                                                                       \
    (da)->ptr[(da)->len++] = (val);                                         \
  } while (0)

// Pops the last element from the array and returns a pointer to it.
// Returns NULL if the array is empty.
#define darray_pop(da) ((da)->len > 0 ? &(da)->ptr[--(da)->len] : NULL)

// Resets the logical length of the array to 0. Does not free memory.
#define darray_clear(da) ((da)->len = 0)

#define darray_reserve(da, new_cap, allocator)               \
  darray_reserve_((void**)&(da)->ptr, &(da)->cap, (new_cap), \
                  sizeof(*(da)->ptr), (allocator))

// Compacts the array, shrinking its capacity to match its current length.
#define darray_compact(da, allocator)                         \
  darray_compact_((void**)&(da)->ptr, &(da)->len, &(da)->cap, \
                  sizeof(*(da)->ptr), (allocator))

// Resizes the array to 'new_size'. Grows capacity if needed.
#define darray_resize(da, new_size, allocator)                  \
  do {                                                          \
    darray_reserve_((void**)&(da)->ptr, &(da)->cap, (new_size), \
                    sizeof(*(da)->ptr), (allocator));           \
    (da)->len = (new_size);                                     \
  } while (0)

// Pushes 'count' elements from 'items' to the end of the array.
#define darray_push_n(da, items, count, allocator)                         \
  do {                                                                     \
    size_t _count = (count);                                               \
    if (_count > 0) {                                                      \
      size_t _needed = (da)->len + _count;                                 \
      if (_needed > (da)->cap) {                                           \
        size_t _new_cap = darray_grow_capacity_((da)->cap, _needed);       \
        darray_reserve_((void**)&(da)->ptr, &(da)->cap, _new_cap,          \
                        sizeof(*(da)->ptr), (allocator));                  \
      }                                                                    \
      memcpy((da)->ptr + (da)->len, (items), _count * sizeof(*(da)->ptr)); \
      (da)->len = _needed;                                                 \
    }                                                                      \
  } while (0)

// Shrinks the array to its current length, detaches the pointer,
// resets the container to zero value, and returns the raw pointer.
// The caller takes ownership of the returned pointer and must free it.
// 'out_count' is an optional pointer to size_t to receive the array length.
#define darray_into_array(da, allocator, out_count)                   \
  ((darray_typeof((da)->ptr))darray_into_array_(                      \
      (void**)&(da)->ptr, &(da)->len, &(da)->cap, sizeof(*(da)->ptr), \
      (allocator), (out_count)))

// Returns a pointer to the element at 'idx'.
// Bounds checked in debug builds.
#define darray_get(da, idx) (assert((idx) < (da)->len), &(da)->ptr[idx])

#ifdef __cplusplus
}
#endif

#endif  // ZTRACING_CORE_DARRAY_H_
