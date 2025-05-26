#ifndef ZTRACING_SRC_MEMORY_H_
#define ZTRACING_SRC_MEMORY_H_

#include <string.h>

#include "src/flick.h"
#include "src/types.h"

#define KB(n) (((u64)(n)) << 10)
#define MB(n) (((u64)(n)) << 20)
#define GB(n) (((u64)(n)) << 30)
#define TB(n) (((u64)(n)) << 40)

typedef FL_Allocator Allocator;

static inline void *Allocator_Alloc(Allocator a, FL_isize size) {
  return FL_Allocator_Alloc(a, size);
}

static inline void Allocator_Free(FL_Allocator a, void *ptr, FL_isize size) {
  FL_Allocator_Free(a, ptr, size);
}

static inline void *CopyMemory(void *dst, const void *src, isize size) {
  return memcpy(dst, src, size);
}

static inline void *MoveMemory(void *dst, const void *src, isize size) {
  return memmove(dst, src, size);
}

static inline void ZeroMemory(void *dst, isize size) { memset(dst, 0, size); }

typedef FL_ArenaOptions ArenaOptions;

typedef FL_Arena Arena;

static inline Arena *Arena_Create(const ArenaOptions *opts) {
  return FL_Arena_Create(opts);
}

#define Arena_Destroy(arena) FL_Arena_Destroy(arena);

#define Arena_Push(arena, size, alignment) FL_Arena_Push(arena, size, alignment)

#define Arena_PushStruct(arena, S) FL_Arena_PushStruct(arena, S)

#define Arena_PushArray(arena, S, n) FL_Arena_PushArray(arena, S, n)

#define Arena_Pop(arena, size) FL_Arena_Pop(arena, size)

#define Arena_Dup(arena, src, size, alignment) \
  FL_Arena_Dup(arena, src, size, alignment)

static inline void *Arena_Seek(Arena *arena, isize n) {
  Arena scratch = *arena;
  return Arena_Pop(&scratch, n);
}

#define Arena_GetAllocator(arena) FL_Arena_GetAllocator(arena)

#endif  // ZTRACING_SRC_MEMORY_H_
