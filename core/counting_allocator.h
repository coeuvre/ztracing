#ifndef CORE_COUNTING_ALLOCATOR_H
#define CORE_COUNTING_ALLOCATOR_H

#include "core/allocator.h"

#ifdef __cplusplus
#include <atomic>
#ifndef _Atomic
#define _Atomic(T) std::atomic<T>
#endif
#else
#include <stdatomic.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct counting_allocator {
  allocator_t super;
  allocator_t* backing;
  _Atomic(size_t) allocated_bytes;
} counting_allocator_t;

void counting_allocator_init(counting_allocator_t* ca, allocator_t* backing);
size_t counting_allocator_get_allocated_bytes(counting_allocator_t* ca);

static inline allocator_t* counting_allocator_get_allocator(
    counting_allocator_t* ca) {
  return (allocator_t*)ca;
}

#ifdef __cplusplus
}
#endif

#endif  // CORE_COUNTING_ALLOCATOR_H
