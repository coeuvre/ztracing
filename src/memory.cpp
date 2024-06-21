static void *AllocateMemory(isize size);
static void *ReallocateMemory(void *ptr, isize old_size, isize new_size);
static void DeallocateMemory(void *ptr, isize size);
static i64 GetAllocatedBytes();

// Memory must not overlap.
static inline void CopyMemory(void *dst, const void *src, isize size) {
  memcpy(dst, src, size);
}

// Memory can overlap.
static inline void MoveMemory(void *dst, const void *src, isize size) {
  memmove(dst, src, size);
}

struct MemoryBlock {
  u8 *begin;
  u8 *end;
  MemoryBlock *prev;
  MemoryBlock *next;
};

static MemoryBlock *AllocateMemoryBlock(isize size) {
  u8 *begin = (u8 *)AllocateMemory(size + sizeof(MemoryBlock));
  u8 *end = begin + size;
  MemoryBlock *block = (MemoryBlock *)end;
  block->begin = begin;
  block->end = end;
  block->prev = 0;
  block->next = 0;
  return block;
}

struct Arena {
  u8 *begin;
  u8 *end;
};

static isize kInitBlockSze = 1024;
static isize kMaxBlockSze = 1024 * 1024;

static Arena InitArena() {
  Arena arena = {};
  MemoryBlock *block = AllocateMemoryBlock(kInitBlockSze);
  arena.begin = block->begin;
  arena.end = block->end;
  return arena;
}

static void *PushSize(Arena *arena, isize size, bool zero) {
  DEBUG_ASSERT(size >= 0);
  DEBUG_ASSERT(arena->begin);

  while (arena->begin + size > arena->end) {
    MemoryBlock *block = (MemoryBlock *)arena->end;
    if (block->next) {
      MemoryBlock *next = block->next;
      arena->begin = next->begin;
      arena->end = next->end;
    } else {
      isize new_size = (block->end - block->begin) << 1;
      while (new_size < size && new_size < kMaxBlockSze) {
        new_size <<= 1;
      }
      new_size = MIN(new_size, kMaxBlockSze);
      new_size = MAX(new_size, size);
      MemoryBlock *new_block = AllocateMemoryBlock(new_size);
      block->next = new_block;
      new_block->prev = block;

      arena->begin = new_block->begin;
      arena->end = new_block->end;
      break;
    }
  }

  DEBUG_ASSERT(arena->begin + size <= arena->end);
  void *result = arena->begin;
  arena->begin += size;

  if (zero) {
    memset(result, 0, size);
  }

  return result;
}

static void *PushSize(Arena *arena, isize size) {
  return PushSize(arena, size, /* zero = */ true);
}

static void *BootstrapPushSize(isize struct_size, isize offset) {
  Arena arena = InitArena();
  void *result = PushSize(&arena, struct_size);
  *(Arena *)((u8 *)result + offset) = arena;
  return result;
}

#define BootstrapPushStruct(Type, field) \
  (Type *)BootstrapPushSize(sizeof(Type), offsetof(Type, field))

#define PushArray(arena, Type, size) \
  (Type *)PushSize(arena, sizeof(Type) * size)

#define PushStruct(arena, Type) (Type *)PushSize(arena, sizeof(Type))

static Buffer PushBufferNoZero(Arena *arena, isize size) {
  Buffer buffer = {};
  buffer.data = (u8 *)PushSize(arena, size, /* zero = */ false);
  buffer.size = size;
  return buffer;
}

static Buffer PushBuffer(Arena *arena, Buffer src) {
  Buffer dst = PushBufferNoZero(arena, src.size);
  CopyMemory(dst.data, src.data, src.size);
  return dst;
}

static inline isize GetRemaining(Arena *arena) {
  isize result = arena->end - arena->begin;
  return result;
}

static Buffer PushFormat(Arena *arena, const char *fmt, ...) {
  Buffer result = {};

  Arena scratch = *arena;
  isize size = GetRemaining(&scratch);
  char *buf = (char *)PushSize(&scratch, size, /* zero= */ false);

  va_list args;
  va_start(args, fmt);
  isize num_chars = vsnprintf(buf, size, fmt, args);
  va_end(args);

  result.data = (u8 *)buf;
  result.size = num_chars;
  if (num_chars <= size) {
    result.data = (u8 *)PushSize(arena, num_chars, /* zero= */ false);
    ASSERT(result.data == (u8 *)buf);
  } else {
    result.data = (u8 *)PushSize(arena, num_chars + 1, /* zero= */ false);
    va_start(args, fmt);
    vsnprintf((char *)result.data, num_chars + 1, fmt, args);
    va_end(args);
  }

  return result;
}

#define PushFormatZ(arena, fmt, ...) \
  ((char *)PushFormat(arena, fmt, ##__VA_ARGS__).data)

static void ClearArena(Arena *arena) {
  ASSERT(arena && arena->end);
  MemoryBlock *block = (MemoryBlock *)arena->end;
  *arena = {};
  while (block) {
    MemoryBlock *prev = block->prev;
    MemoryBlock *next = block->next;
    if (prev) {
      prev->next = next;
    }
    if (next) {
      next->prev = prev;
    }

    DeallocateMemory(block->begin,
                     block->end - block->begin + sizeof(MemoryBlock));

    block = prev ? prev : next;
  }
}

static inline u64 Hash(Buffer buffer) {
  u64 h = 0x100;
  for (isize i = 0; i < buffer.size; i++) {
    h ^= buffer.data[i];
    h *= 1111111111111111111u;
  }
  return h;
}

struct HashNode {
  HashNode *child[4];
  Buffer key;
  void *value;
};

struct HashMap {
  HashNode *root;
};

static void **Upsert(Arena *arena, HashMap *m, Buffer key) {
  HashNode **node = &m->root;
  for (u64 hash = Hash(key); *node; hash <<= 2) {
    if (Equal(key, (*node)->key)) {
      return &(*node)->value;
    }
    node = &(*node)->child[hash >> 62];
  }
  *node = PushStruct(arena, HashNode);
  (*node)->key = PushBuffer(arena, key);
  return &(*node)->value;
}

static inline Buffer GetKey(void **value_ptr) {
  HashNode *node = (HashNode *)((u8 *)value_ptr - offsetof(HashNode, value));
  return node->key;
}

struct HashMapIterNode {
  HashNode *node;
  HashMapIterNode *next;
};

struct HashMapIter {
  Arena *arena;
  HashMapIterNode *next;
  HashMapIterNode *first_free;
};

static HashMapIter InitHashMapIter(Arena *arena, HashMap *m) {
  HashMapIter iter = {};
  iter.arena = arena;
  if (m->root) {
    HashMapIterNode *next = PushStruct(arena, HashMapIterNode);
    next->node = m->root;
    iter.next = next;
  }
  return iter;
}

static void *IterNext(HashMapIter *iter) {
  void *result = 0;

  HashNode *node = 0;
  if (iter->next) {
    node = iter->next->node;
    HashMapIterNode *free = iter->next;
    iter->next = iter->next->next;

    free->next = iter->first_free;
    iter->first_free = free;
  }

  if (node) {
    result = node->value;
    ASSERT(result);
    for (isize i = 0; i < 4; ++i) {
      HashNode *child_node = node->child[i];
      if (child_node) {
        HashMapIterNode *next = 0;
        if (iter->first_free) {
          next = iter->first_free;
          iter->first_free = next->next;
        } else {
          next = PushStruct(iter->arena, HashMapIterNode);
        }
        next->node = child_node;
        next->next = iter->next;
        iter->next = next;
      }
    }
  }

  return result;
}
