#ifndef ZTRACING_SRC_HASH_TRIE_H_
#define ZTRACING_SRC_HASH_TRIE_H_

#include <stdalign.h>
#include <string.h>

#include "src/assert.h"
#include "src/list.h"
#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"

typedef struct HashTrieSlot HashTrieSlot;
struct HashTrieSlot {
  HashTrieSlot *slots[4];
  Str key;
};

typedef struct HashTrie {
  HashTrieSlot *root;
  usize value_size;
} HashTrie;

static inline void *HashTrie_Upsert_(HashTrie *self, Str key, Arena *arena,
                                     usize value_size) {
  if (!self->root) {
    self->value_size = value_size;
  }
  DEBUG_ASSERT(self->value_size == value_size);

  HashTrieSlot **t = &self->root;
  for (u64 hash = Str_Hash(key); *t; hash <<= 2) {
    if (Str_IsEqual(key, t[0]->key)) {
      return t[0] + 1;
    }
    t = t[0]->slots + (hash >> 62);
  }

  if (arena) {
    HashTrieSlot *slot = (HashTrieSlot *)Arena_Push(
        arena, sizeof(HashTrieSlot) + value_size, alignof(HashTrieSlot));
    *slot = (HashTrieSlot){
        .key = Str_Dup(arena, key),
    };
    *t = slot;
    void *value = slot + 1;
    ZeroMemory(value, value_size);
    return value;
  }

  return 0;
}

#define HashTrie_Upsert(self, key, arena, ValueType) \
  ((ValueType *)HashTrie_Upsert_(self, key, arena, sizeof(ValueType)))

static inline Str HashTrie_GetKey(void *value) {
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

static inline void HashTrieIter_Append(HashTrieIter *self, HashTrieSlot *slot) {
  HashTrieIterItem *item = self->free_last;
  if (item) {
    DLL_REMOVE(self->free_first, self->free_last, item, prev, next);
  } else {
    item = Arena_PushStruct(self->arena, HashTrieIterItem);
  }
  *item = (HashTrieIterItem){
      .slot = slot,
  };
  DLL_APPEND(self->first, self->last, item, prev, next);
}

static inline HashTrieIter HashTrie_Iter(const HashTrie *t, Arena *arena) {
  HashTrieIter iter = {
      .arena = arena,
      .value_size = t->value_size,
  };
  if (t->root) {
    HashTrieIter_Append(&iter, t->root);
  }
  return iter;
}

static inline void *HashTrie_Next_(HashTrieIter *self, usize value_size) {
  HashTrieSlot *result = 0;

  HashTrieIterItem *item = self->first;
  if (item) {
    DLL_REMOVE(self->first, self->last, item, prev, next);
    result = item->slot;
    DLL_APPEND(self->free_first, self->free_last, item, prev, next);
  }

  if (result) {
    for (usize slot_index = 0; slot_index < COUNT_OF(result->slots);
         ++slot_index) {
      HashTrieSlot *next_slot = result->slots[slot_index];
      if (next_slot) {
        HashTrieIter_Append(self, next_slot);
      }
    }
    DEBUG_ASSERT(self->value_size == value_size);
    return result + 1;
  }

  return 0;
}

#define HashTrie_Next(self, ValueType) \
  ((ValueType *)HashTrie_Next_(self, sizeof(ValueType)))

#endif  // ZTRACING_SRC_HASH_TRIE_H_
