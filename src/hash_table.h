#ifndef ZTRACING_SRC_HASH_TABLE_H_
#define ZTRACING_SRC_HASH_TABLE_H_

#include <stdint.h>
#include <string.h>

#include "src/allocator.h"

template <typename T>
struct DefaultHash;

template <>
struct DefaultHash<int32_t> {
  uint32_t operator()(int32_t v) const { return (uint32_t)v; }
};

template <>
struct DefaultHash<uint32_t> {
  uint32_t operator()(uint32_t v) const { return v; }
};

template <>
struct DefaultHash<uint64_t> {
  uint32_t operator()(uint64_t v) const { return (uint32_t)(v ^ (v >> 32)); }
};

template <typename T>
struct DefaultEq {
  bool operator()(const T& a, const T& b) const { return a == b; }
};

template <typename K, typename V, typename Hash = DefaultHash<K>,
          typename Eq = DefaultEq<K>>
struct HashTable {
  struct Entry {
    K key;
    V value;
    uint32_t hash;
    bool occupied;
  };

  Entry* entries;
  size_t size;
  size_t capacity;
  size_t capacity_mask;
  Hash hash_fn;
  Eq eq_fn;
};

template <typename K, typename V, typename Hash, typename Eq>
void hash_table_init(HashTable<K, V, Hash, Eq>* ht, Allocator a,
                     size_t initial_capacity = 16) {
  if (initial_capacity < 4) initial_capacity = 4;
  size_t cap = 1;
  while (cap < initial_capacity) cap <<= 1;

  ht->entries = (typename HashTable<K, V, Hash, Eq>::Entry*)allocator_alloc(
      a, cap * sizeof(typename HashTable<K, V, Hash, Eq>::Entry));
  memset(ht->entries, 0,
         cap * sizeof(typename HashTable<K, V, Hash, Eq>::Entry));
  ht->size = 0;
  ht->capacity = cap;
  ht->capacity_mask = cap - 1;
}

template <typename K, typename V, typename Hash, typename Eq>
void hash_table_deinit(HashTable<K, V, Hash, Eq>* ht, Allocator a) {
  if (ht->entries) {
    allocator_free(
        a, ht->entries,
        ht->capacity * sizeof(typename HashTable<K, V, Hash, Eq>::Entry));
  }
  memset(ht, 0, sizeof(HashTable<K, V, Hash, Eq>));
}

template <typename K, typename V, typename Hash, typename Eq>
void hash_table_put_with_hash(HashTable<K, V, Hash, Eq>* ht, Allocator a,
                              const K& key, const V& value, uint32_t h);

template <typename K, typename V, typename Hash, typename Eq>
void hash_table_resize(HashTable<K, V, Hash, Eq>* ht, Allocator a,
                       size_t new_capacity) {
  HashTable<K, V, Hash, Eq> new_ht;
  hash_table_init(&new_ht, a, new_capacity);
  new_ht.hash_fn = ht->hash_fn;
  new_ht.eq_fn = ht->eq_fn;

  for (size_t i = 0; i < ht->capacity; i++) {
    if (ht->entries[i].occupied) {
      hash_table_put_with_hash(&new_ht, a, ht->entries[i].key,
                               ht->entries[i].value, ht->entries[i].hash);
    }
  }

  hash_table_deinit(ht, a);
  *ht = new_ht;
}

template <typename K, typename V, typename Hash, typename Eq>
void hash_table_put_with_hash(HashTable<K, V, Hash, Eq>* ht, Allocator a,
                              const K& key, const V& value, uint32_t h) {
  if (ht->size * 2 > ht->capacity) {
    hash_table_resize(ht, a, ht->capacity * 2);
  }

  size_t idx = h & ht->capacity_mask;

  while (ht->entries[idx].occupied) {
    if (ht->entries[idx].hash == h && ht->eq_fn(ht->entries[idx].key, key)) {
      ht->entries[idx].value = value;
      return;
    }
    idx = (idx + 1) & ht->capacity_mask;
  }

  ht->entries[idx].key = key;
  ht->entries[idx].value = value;
  ht->entries[idx].hash = h;
  ht->entries[idx].occupied = true;
  ht->size++;
}

template <typename K, typename V, typename Hash, typename Eq>
void hash_table_put(HashTable<K, V, Hash, Eq>* ht, Allocator a, const K& key,
                    const V& value) {
  uint32_t h = ht->hash_fn(key);
  hash_table_put_with_hash(ht, a, key, value, h);
}

template <typename K, typename V, typename Hash, typename Eq>
V* hash_table_get_with_hash(HashTable<K, V, Hash, Eq>* ht, const K& key,
                            uint32_t h) {
  if (ht->capacity == 0) return nullptr;

  size_t idx = h & ht->capacity_mask;

  while (ht->entries[idx].occupied) {
    if (ht->entries[idx].hash == h && ht->eq_fn(ht->entries[idx].key, key)) {
      return &ht->entries[idx].value;
    }
    idx = (idx + 1) & ht->capacity_mask;
  }

  return nullptr;
}

template <typename K, typename V, typename Hash, typename Eq>
V* hash_table_get(HashTable<K, V, Hash, Eq>* ht, const K& key) {
  uint32_t h = ht->hash_fn(key);
  return hash_table_get_with_hash(ht, key, h);
}


template <typename K, typename V, typename Hash, typename Eq>
void hash_table_clear(HashTable<K, V, Hash, Eq>* ht) {
  if (ht->entries) {
    memset(ht->entries, 0,
           ht->capacity * sizeof(typename HashTable<K, V, Hash, Eq>::Entry));
  }
  ht->size = 0;
}

#endif  // ZTRACING_SRC_HASH_TABLE_H_
