#ifndef ZTRACING_SRC_HASH_TRIE_H_
#define ZTRACING_SRC_HASH_TRIE_H_

#include "src/string.h"

typedef struct HashTrie HashTrie;
struct HashTrie {
  HashTrie *slots[4];
  Str8 key;
  void *value;
};

static inline void **hash_trie_upsert(Arena *arena, HashTrie **t, Str8 key) {
  for (u64 hash = str8_hash(key); *t; hash <<= 2) {
    if (str8_eq(key, t[0]->key)) {
      return &t[0]->value;
    }
    t = t[0]->slots + (hash >> 62);
  }

  if (arena) {
    HashTrie *slot = arena_push_struct(arena, HashTrie);
    slot->key = arena_dup_str8(arena, key);
    *t = slot;
    return &t[0]->value;
  }

  return 0;
}

#endif  // ZTRACING_SRC_HASH_TRIE_H_
