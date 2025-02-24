#ifndef ZTRACING_SRC_HASH_TRIE_H_
#define ZTRACING_SRC_HASH_TRIE_H_

#include <string.h>

#include "src/list.h"
#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"

typedef struct HashTrie HashTrie;
struct HashTrie {
  HashTrie *slots[4];
  Str8 key;
  void *value;
};

static inline HashTrie *hash_trie_upsert(Arena *arena, HashTrie **t, Str8 key) {
  for (u64 hash = str8_hash(key); *t; hash <<= 2) {
    if (str8_eq(key, t[0]->key)) {
      return t[0];
    }
    t = t[0]->slots + (hash >> 62);
  }

  if (arena) {
    HashTrie *slot = arena_push_struct(arena, HashTrie);
    slot->key = str8_dup(arena, key);
    *t = slot;
    return slot;
  }

  return 0;
}

typedef struct HashTrieIterItem HashTrieIterItem;
struct HashTrieIterItem {
  HashTrieIterItem *prev;
  HashTrieIterItem *next;
  HashTrie *slot;
};

typedef struct HashTrieIter {
  Arena *arena;
  HashTrieIterItem *first;
  HashTrieIterItem *last;
  HashTrieIterItem *free_first;
  HashTrieIterItem *free_last;
} HashTrieIter;

static inline void hash_trie_iter__append_item(HashTrieIter *self,
                                               HashTrie *slot) {
  HashTrieIterItem *item = self->free_last;
  if (item) {
    DLL_REMOVE(self->free_first, self->free_last, item, prev, next);
  } else {
    item = arena_push_struct(self->arena, HashTrieIterItem);
  }
  item->slot = slot;
  DLL_APPEND(self->first, self->last, item, prev, next);
}

static inline HashTrieIter hash_trie_iter(Arena *arena, HashTrie *t) {
  HashTrieIter iter = {
      .arena = arena,
  };
  if (t) {
    hash_trie_iter__append_item(&iter, t);
  }
  return iter;
}

static inline HashTrie *hash_trie_iter_next(HashTrieIter *self) {
  HashTrie *result = 0;

  HashTrieIterItem *item = self->first;
  if (item) {
    DLL_REMOVE(self->first, self->last, item, prev, next);
    result = item->slot;
    DLL_APPEND(self->free_first, self->free_last, item, prev, next);
  }

  if (result) {
    for (usize slot_index = 0; slot_index < ARRAY_COUNT(result->slots);
         ++slot_index) {
      HashTrie *next_slot = result->slots[slot_index];
      if (next_slot) {
        hash_trie_iter__append_item(self, next_slot);
      }
    }
  }

  return result;
}

#endif  // ZTRACING_SRC_HASH_TRIE_H_
