#ifndef ZTRACING_SRC_HASH_TRIE_H_
#define ZTRACING_SRC_HASH_TRIE_H_

#include <string.h>

#include "src/assert.h"
#include "src/list.h"
#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"

typedef struct HashTrieSlot HashTrieSlot;
struct HashTrieSlot {
  HashTrieSlot *slots[4];
  Str8 key;
};

typedef struct HashTrie {
  HashTrieSlot *root;
  usize value_size;
} HashTrie;

static inline void *hash_trie_upsert_(HashTrie *self, Str8 key, Arena *arena,
                                      usize value_size) {
  if (!self->root) {
    self->value_size = value_size;
  }
  DEBUG_ASSERT(self->value_size == value_size);

  HashTrieSlot **t = &self->root;
  for (u64 hash = str8_hash(key); *t; hash <<= 2) {
    if (str8_eq(key, t[0]->key)) {
      return t[0] + 1;
    }
    t = t[0]->slots + (hash >> 62);
  }

  if (arena) {
    HashTrieSlot *slot =
        (HashTrieSlot *)arena_push(arena, sizeof(HashTrieSlot) + value_size, 0);
    slot->key = str8_dup(arena, key);
    *t = slot;
    return slot + 1;
  }

  return 0;
}

#define hash_trie_upsert(self, key, arena, ValueType) \
  ((ValueType *)hash_trie_upsert_(self, key, arena, sizeof(ValueType)))

static inline Str8 hash_trie_get_key(void *value) {
  HashTrieSlot *slot = ((HashTrieSlot *)value) - 1;
  return slot->key;
}

typedef struct HashTrieIterItem HashTrieIterItem;
struct HashTrieIterItem {
  HashTrieIterItem *prev;
  HashTrieIterItem *next;
  HashTrieSlot *slot;
};

typedef struct HashTrieIter {
  Arena *arena;
  usize value_size;
  HashTrieIterItem *first;
  HashTrieIterItem *last;
  HashTrieIterItem *free_first;
  HashTrieIterItem *free_last;
} HashTrieIter;

static inline void hash_trie_iter_append_item(HashTrieIter *self,
                                              HashTrieSlot *slot) {
  HashTrieIterItem *item = self->free_last;
  if (item) {
    DLL_REMOVE(self->free_first, self->free_last, item, prev, next);
  } else {
    item = arena_push_struct(self->arena, HashTrieIterItem);
  }
  item->slot = slot;
  DLL_APPEND(self->first, self->last, item, prev, next);
}

static inline HashTrieIter hash_trie_iter(const HashTrie *t, Arena *arena) {
  HashTrieIter iter = {
      .arena = arena,
      .value_size = t->value_size,
  };
  if (t->root) {
    hash_trie_iter_append_item(&iter, t->root);
  }
  return iter;
}

static inline void *hash_trie_iter_next_(HashTrieIter *self, usize value_size) {
  HashTrieSlot *result = 0;

  HashTrieIterItem *item = self->first;
  if (item) {
    DLL_REMOVE(self->first, self->last, item, prev, next);
    result = item->slot;
    DLL_APPEND(self->free_first, self->free_last, item, prev, next);
  }

  if (result) {
    for (usize slot_index = 0; slot_index < ARRAY_COUNT(result->slots);
         ++slot_index) {
      HashTrieSlot *next_slot = result->slots[slot_index];
      if (next_slot) {
        hash_trie_iter_append_item(self, next_slot);
      }
    }
    DEBUG_ASSERT(self->value_size == value_size);
    return result + 1;
  }

  return 0;
}

#define hash_trie_iter_next(self, ValueType) \
  ((ValueType *)hash_trie_iter_next_(self, sizeof(ValueType)))

#endif  // ZTRACING_SRC_HASH_TRIE_H_
