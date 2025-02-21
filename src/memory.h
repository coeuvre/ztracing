#ifndef ZTRACING_SRC_MEMORY_H_
#define ZTRACING_SRC_MEMORY_H_

#include <string.h>

#include "src/platform.h"
#include "src/types.h"

#define KB(n) (((u64)(n)) << 10)
#define MB(n) (((u64)(n)) << 20)
#define GB(n) (((u64)(n)) << 30)
#define TB(n) (((u64)(n)) << 40)

static inline void memory_zero(void *ptr, usize size) { memset(ptr, 0, size); }

static inline void *memory_alloc(usize size) {
  void *result = platform_memory_alloc(size);
  memory_zero(result, size);
  return result;
}

static inline void *memory_alloc_no_zero(usize size) {
  return platform_memory_alloc(size);
}

static inline void memory_free(void *ptr, usize size) {
  return platform_memory_free(ptr, size);
}

static inline void *memory_copy(void *dst, const void *src, usize size) {
  return memcpy(dst, src, size);
}

typedef struct Arena Arena;
struct Arena {
  u8 *begin;
  u8 *end;
};

enum ArenaPushFlag {
  ARENA_PUSH_NO_ZERO = (1 << 0),
};

void arena_free(Arena *arena);
void arena_clear(Arena *arena);
void *arena_push(Arena *arena, usize size, u32 flags);
/// Returns top pointer after pop.
void *arena_pop(Arena *arena, usize size);
// Get a pointer from the stack that is `size` down away from the current top.
void *arena_seek(Arena *arena, usize size);
bool arena_is_empty(Arena *arena);

#define arena_push_array(arena, Type, len) \
  (Type *)arena_push(arena, sizeof(Type) * len, 0)
#define arena_push_array_no_zero(arena, Type, len) \
  (Type *)arena_push(arena, sizeof(Type) * len, ARENA_PUSH_NO_ZERO)

#define arena_push_struct(arena, Type) arena_push_array(arena, Type, 1)
#define arena_push_struct_no_zero(arena, Type) \
  arena_push_array_no_zero(arena, Type, 1)

static inline void *arena_dup(Arena *arena, void *src, usize size) {
  void *dst = arena_push(arena, size, ARENA_PUSH_NO_ZERO);
  memory_copy(dst, src, size);
  return dst;
}

#define arena_dup_struct(arena, src) arena_dup(arena, src, sizeof(*(src)))

typedef struct Scratch {
  Arena checkpoint;
  Arena *arena;
} Scratch;

Scratch scratch_begin(Arena **conflicts, usize len);
static inline void scratch_end(Scratch scratch) {
  *scratch.arena = scratch.checkpoint;
}

void scratch_free(void);

#endif  // ZTRACING_SRC_MEMORY_H_
