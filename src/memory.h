#ifndef ZTRACING_SRC_MEMORY_H_
#define ZTRACING_SRC_MEMORY_H_

#include <string.h>

#include "src/types.h"

#define KB(n) (((u64)(n)) << 10)
#define MB(n) (((u64)(n)) << 20)
#define GB(n) (((u64)(n)) << 30)
#define TB(n) (((u64)(n)) << 40)

static inline void memory_zero(void *ptr, usize size) { memset(ptr, 0, size); }

void *memory_alloc(usize size);
void memory_free(void *ptr, usize size);
usize memory_get_allocated_bytes(void);

static inline void *memory_copy(void *dst, const void *src, usize size) {
  return memcpy(dst, src, size);
}

typedef struct MemoryBlock MemoryBlock;
struct MemoryBlock {
  MemoryBlock *prev;
  MemoryBlock *next;

  u8 *begin;
  u8 *end;
  u8 *pos;
};

typedef struct Arena Arena;
struct Arena {
  MemoryBlock *current_block;
  u32 temp_count;
};

typedef struct TempMemory TempMemory;
struct TempMemory {
  Arena *arena;
  MemoryBlock *block;
  u8 *pos;
};

enum ArenaPushFlag {
  ARENA_PUSH_NO_ZERO = (1 << 0),
};

void arena_free(Arena *arena);
void arena_clear(Arena *arena);
void *arena_push(Arena *arena, usize size, u32 flags);
void arena_pop(Arena *arena, usize size);
// Get a pointer from the stack that is `size` down away from the current top.
void *arena_seek(Arena *arena, usize size);

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

TempMemory temp_memory_begin(Arena *arena);
void temp_memory_end(TempMemory temp);

Arena *arena_get_scratch(Arena **conflicts, usize len);

typedef TempMemory Scratch;

#define scratch_begin(conflicts, len) \
  temp_memory_begin(arena_get_scratch((conflicts), (len)))
#define scratch_end(temp) temp_memory_end(temp)

#endif  // ZTRACING_SRC_MEMORY_H_
