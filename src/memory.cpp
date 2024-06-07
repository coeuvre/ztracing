static void *AllocateMemory(isize size);
static void *ReallocateMemory(void *ptr, isize old_size, isize new_size);
static void DeallocateMemory(void *ptr, isize size);
static isize GetAllocatedBytes();

static inline void
CopyMemory(void *dst, const void *src, isize size) {
    memcpy(dst, src, size);
}

struct MemoryBlock {
    MemoryBlock *prev;
    MemoryBlock *next;
    u8 *begin;
    u8 *at;
    u8 *end;
};

struct Arena {
    MemoryBlock *block;
    MemoryBlock *first;
    u32 temp_arena_count;
};

struct TempArena {
    Arena *arena;
    MemoryBlock *block;
    u8 *at;
};

static TempArena
BeginTempArena(Arena *arena) {
    TempArena temp_arena = {};
    temp_arena.arena = arena;
    temp_arena.block = arena->block;
    if (temp_arena.block) {
        temp_arena.at = temp_arena.block->at;
    }
    ++arena->temp_arena_count;
    return temp_arena;
}

static void
EndTempArena(TempArena temp_arena) {
    Arena *arena = temp_arena.arena;
    if (temp_arena.block) {
        DEBUG_ASSERT(temp_arena.block->at >= temp_arena.at);
        arena->block = temp_arena.block;
        arena->block->at = temp_arena.at;
    } else if (arena->first) {
        arena->block = arena->first;
        arena->block->at = arena->block->begin;
    }

    DEBUG_ASSERT(arena->temp_arena_count > 0);
    --arena->temp_arena_count;
}

static isize INIT_BLOCK_SIZE = 4 * 1024;

static void *
PushSize(Arena *arena, isize size, bool zero) {
    DEBUG_ASSERT(size >= 0);

    if (!arena->block || arena->block->at + size > arena->block->end) {
        while (true) {
            if (arena->block && arena->block->next) {
                MemoryBlock *next = arena->block->next;
                arena->block = next;
                next->at = next->begin;
                if (next->at + size <= next->end) {
                    break;
                }
            } else {
                isize new_size = INIT_BLOCK_SIZE;
                if (arena->block) {
                    new_size = (arena->block->end - arena->block->begin) << 1;
                }
                while (new_size < size) {
                    new_size <<= 1;
                }
                MemoryBlock *block = (MemoryBlock *)AllocateMemory(
                    sizeof(MemoryBlock) + new_size
                );
                block->prev = arena->block;
                block->next = 0;
                block->begin = (u8 *)(block + 1);
                block->at = block->begin;
                block->end = block->begin + new_size;
                if (arena->block) {
                    ASSERT(!arena->block->next);
                    arena->block->next = block;
                }
                arena->block = block;
                if (!arena->first) {
                    arena->first = block;
                }
                break;
            }
        }
    }

    DEBUG_ASSERT(arena->block && arena->block->at + size <= arena->block->end);
    void *result = arena->block->at;
    arena->block->at += size;

    if (zero) {
        memset(result, 0, size);
    }

    return result;
}

static void *
PushSize(Arena *arena, isize size) {
    return PushSize(arena, size, /* zero = */ true);
}

static void *
BootstrapPushSize(isize struct_size, isize offset) {
    Arena arena = {};
    void *result = PushSize(&arena, struct_size);
    *(Arena *)((u8 *)result + offset) = arena;
    return result;
}

#define BootstrapPushStruct(Type, field)                                       \
    (Type *)BootstrapPushSize(sizeof(Type), offsetof(Type, field))

#define PushArray(arena, Type, size)                                           \
    (Type *)PushSize(arena, sizeof(Type) * size)

#define PushStruct(arena, Type) (Type *)PushSize(arena, sizeof(Type))

static Buffer
PushBufferNoZero(Arena *arena, isize size) {
    Buffer buffer = {};
    buffer.data = (u8 *)PushSize(arena, size, /* zero = */ false);
    buffer.size = size;
    return buffer;
}

static Buffer
PushBuffer(Arena *arena, Buffer src) {
    Buffer dst = PushBufferNoZero(arena, src.size);
    CopyMemory(dst.data, src.data, src.size);
    return dst;
}

static isize
GetRemaining(Arena *arena) {
    isize result = 0;
    if (arena->block) {
        result = arena->block->end - arena->block->at;
    }
    return result;
}

static Buffer
PushFormat(Arena *arena, const char *fmt, ...) {
    Buffer result = {};

    va_list args;

    TempArena temp_arena = BeginTempArena(arena);
    isize size = GetRemaining(arena);
    char *buf = (char *)PushSize(arena, size, /* zero= */ false);
    va_start(args, fmt);
    result.size = vsnprintf(buf, size, fmt, args) + 1;
    va_end(args);
    EndTempArena(temp_arena);

    result.data = (u8 *)buf;
    if (result.size < size) {
        result.data = (u8 *)PushSize(arena, size, /* zero= */ false);
        ASSERT(result.data == (u8 *)buf);
    } else if (result.size > size) {
        result.data = (u8 *)PushSize(arena, result.size, /* zero= */ false);
        va_start(args, fmt);
        vsnprintf((char *)result.data, result.size, fmt, args);
        va_end(args);
    }

    return result;
}

#define PushFormatZ(arena, fmt, ...)                                           \
    ((char *)PushFormat(arena, fmt, ##__VA_ARGS__).data)

static void
FreeLastBlock(Arena *arena) {
    MemoryBlock *block = arena->block;
    arena->block = block->prev;
    if (arena->block) {
        arena->block->next = 0;
    }
    DeallocateMemory(block, sizeof(MemoryBlock) + block->end - block->begin);
}

static void
ClearArena(Arena *arena) {
    ASSERT(arena->temp_arena_count == 0);

    while (arena->block && arena->block->next) {
        arena->block = arena->block->next;
    }

    bool has_block = arena->block != 0;
    while (has_block) {
        // The arena itself might be stored in the last block, we must ensure
        // not looking at it after freeing
        has_block = arena->block->prev != 0;
        FreeLastBlock(arena);
    }
}

static inline u64
Hash(Buffer buffer) {
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

static void **
Upsert(Arena *arena, HashMap *m, Buffer key) {
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

static inline Buffer
GetKey(void **value_ptr) {
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

static inline HashMapIter
IterateHashMap(Arena *arena, HashMap *m) {
    HashMapIter iter = {};
    iter.arena = arena;
    if (m->root) {
        iter.next = PushStruct(arena, HashMapIterNode);
        iter.next->node = m->root;
    }
    return iter;
}

static HashNode *
GetNext(HashMapIter *iter) {
    HashNode *result = 0;
    if (iter->next) {
        result = iter->next->node;
        HashMapIterNode *free = iter->next;
        iter->next = iter->next->next;

        free->next = iter->first_free;
        iter->first_free = free;
    }

    if (result) {
        for (isize i = 0; i < 4; ++i) {
            HashNode *node = result->child[i];
            if (node) {
                HashMapIterNode *next = 0;
                if (iter->first_free) {
                    next = iter->first_free;
                    iter->first_free = next->next;
                } else {
                    next = PushStruct(iter->arena, HashMapIterNode);
                }
                next->node = result->child[i];
                next->next = iter->next;

                iter->next = next;
            }
        }
    }
    ASSERT(!result || result->value);
    return result;
}
