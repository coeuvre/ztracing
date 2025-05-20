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
  isize value_size;
  void *value;
};

typedef struct HashTrie {
  HashTrieSlot *root;
} HashTrie;

// Returns true if *out_value points to uninitialized memory.
static inline bool HashTrie_Upsert_(HashTrie *self, Str key, isize value_size,
                                    isize value_alignment, void **out_value,
                                    Arena *arena) {
  bool found = false;

  void *value = 0;
  HashTrieSlot **t = &self->root;
  for (u64 hash = Str_Hash(key); *t; hash <<= 2) {
    if (Str_IsEqual(key, t[0]->key)) {
      DEBUG_ASSERT(t[0]->value_size == value_size);
      value = t[0]->value;
      found = true;
      break;
    }
    t = t[0]->slots + (hash >> 62);
  }

  if (!value && arena) {
    value = Arena_Push(arena, value_size + sizeof(void *), value_alignment);

    HashTrieSlot *slot = Arena_PushStruct(arena, HashTrieSlot);
    *slot = (HashTrieSlot){
        .key = Str_Dup(arena, key),
        .value_size = value_size,
        .value = value,
    };
    *t = slot;

    *((void **)((u8 *)value + value_size)) = slot;
  }

  *out_value = value;
  return !found;
}

#define HashTrie_Upsert(self, key, out_value, arena)                     \
  HashTrie_Upsert_(self, key, sizeof(**out_value), alignof(**out_value), \
                   (void **)out_value, arena)

static inline Str HashTrie_GetKey_(isize value_size, void *value) {
  HashTrieSlot *slot = *(void **)((u8 *)value + value_size);
  DEBUG_ASSERT(slot->value_size == value_size);
  return slot->key;
}

#define HashTrie_GetKey(value) HashTrie_GetKey_(sizeof(*value), value)

typedef struct HashTrieIterItem HashTrieIterItem;
struct HashTrieIterItem {
  HashTrieIterItem *prev;
  HashTrieIterItem *next;
  HashTrieSlot *slot;
};

typedef struct HashTrieIter {
  Arena *arena;
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
  };
  if (t->root) {
    HashTrieIter_Append(&iter, t->root);
  }
  return iter;
}

static inline void *HashTrie_Next_(HashTrieIter *self, isize value_size) {
  HashTrieSlot *result = 0;

  HashTrieIterItem *item = self->first;
  if (item) {
    DLL_REMOVE(self->first, self->last, item, prev, next);
    result = item->slot;
    DLL_APPEND(self->free_first, self->free_last, item, prev, next);
  }

  if (result) {
    for (isize slot_index = 0; slot_index < COUNT_OF(result->slots);
         ++slot_index) {
      HashTrieSlot *next_slot = result->slots[slot_index];
      if (next_slot) {
        HashTrieIter_Append(self, next_slot);
      }
    }
    DEBUG_ASSERT(result->value_size == value_size);
    return result->value;
  }

  return 0;
}

#define HashTrie_Next(self, ValueType) \
  ((ValueType *)HashTrie_Next_(self, sizeof(ValueType)))

#endif  // ZTRACING_SRC_HASH_TRIE_H_
