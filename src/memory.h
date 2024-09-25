#ifndef ZTRACING_SRC_MEMORY_H_
#define ZTRACING_SRC_MEMORY_H_

#include <string.h>

#include "src/types.h"

#define KB(n) (((u64)(n)) << 10)
#define MB(n) (((u64)(n)) << 20)
#define GB(n) (((u64)(n)) << 30)
#define TB(n) (((u64)(n)) << 40)

static inline void ZeroMemory(void *ptr, usize size) { memset(ptr, 0, size); }

struct MemoryBlock {
  MemoryBlock *prev;

  u8 *begin;
  u8 *end;
  u8 *pos;
};

struct Arena {
  MemoryBlock *current_block;
  u32 temp_count;
};

struct TempMemory {
  Arena *arena;
  MemoryBlock *block;
  u8 *pos;
};

enum PushArenaFlag {
  kPushArenaNoZero = (1 << 0),
};

Arena *AllocArena(void);
void FreeArena(Arena *arena);
void *PushArena(Arena *arena, usize size, u32 flags);
void PopArena(Arena *arena, usize size);

#define PushArray(arena, Type, len) \
  (Type *)PushArena(arena, sizeof(Type) * len, 0)
#define PushArrayNoZero(arena, Type, len) \
  (Type *)PushArena(arena, sizeof(Type) * len, kPushArenaNoZero)

TempMemory BeginTempMemory(Arena *arena);
void EndTempMemory(TempMemory temp);

Arena *GetScratchArena(Arena **conflicts, usize len);

#define BeginScratch(conflicts, len) \
  BeginTempMemory(GetScratchArena((conflicts), (len)))
#define EndScratch(temp) EndTempMemory(temp)

#endif  // ZTRACING_SRC_MEMORY_H_
