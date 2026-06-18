#ifndef ZTRACING_SRC_HASH_TABLE_H_
#define ZTRACING_SRC_HASH_TABLE_H_

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "src/allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hash_table {
  void* entries;
  size_t size;
  size_t capacity;
  size_t capacity_mask;
  size_t key_size;
  size_t value_size;
  size_t key_align;
  size_t value_align;
  size_t entry_size;
  size_t value_offset;
  size_t hash_offset;
  size_t occupied_offset;
  uint32_t (*hash_fn)(const void* key, void* ctx);
  bool (*eq_fn)(const void* a, const void* b, void* ctx);
  void* ctx;
} hash_table_t;

inline void hash_table_calculate_layout(hash_table_t* ht) {
  size_t offset = ht->key_size;

  // Align value
  offset = (offset + ht->value_align - 1) & ~(ht->value_align - 1);
  ht->value_offset = offset;
  offset += ht->value_size;

  // Align hash (uint32_t -> 4 bytes)
  offset = (offset + 3) & ~3u;
  ht->hash_offset = offset;
  offset += sizeof(uint32_t);

  // Align occupied (bool -> 1 byte, no padding needed really, but we record
  // offset)
  ht->occupied_offset = offset;
  offset += sizeof(bool);

  // Align entry_size to max alignment of key, value, and hash (at least 4)
  size_t max_align =
      ht->key_align > ht->value_align ? ht->key_align : ht->value_align;
  if (max_align < 4) {
    max_align = 4;
  }
  ht->entry_size = (offset + max_align - 1) & ~(max_align - 1);
}

inline hash_table_t hash_table_init_(
    size_t key_size, size_t value_size, size_t key_align, size_t value_align,
    uint32_t (*hash_fn)(const void* key, void* ctx),
    bool (*eq_fn)(const void* a, const void* b, void* ctx), void* ctx) {
  hash_table_t ht = {
      .key_size = key_size,
      .value_size = value_size,
      .key_align = key_align,
      .value_align = value_align,
      .hash_fn = hash_fn,
      .eq_fn = eq_fn,
      .ctx = ctx,
  };
  hash_table_calculate_layout(&ht);
  return ht;
}

#define hash_table_init(key_t, value_t, hash_fn, eq_fn, ctx)       \
  hash_table_init_(sizeof(key_t), sizeof(value_t), alignof(key_t), \
                   alignof(value_t), (hash_fn), (eq_fn), (ctx))

inline void* hash_table_entry_key(const hash_table_t* ht, void* entry) {
  (void)ht;
  return entry;
}

inline void* hash_table_entry_value(const hash_table_t* ht, void* entry) {
  return (char*)entry + ht->value_offset;
}

inline uint32_t* hash_table_entry_hash(const hash_table_t* ht, void* entry) {
  return (uint32_t*)((char*)entry + ht->hash_offset);
}

inline bool* hash_table_entry_occupied(const hash_table_t* ht, void* entry) {
  return (bool*)((char*)entry + ht->occupied_offset);
}

inline void hash_table_deinit(hash_table_t* ht, allocator_t a) {
  if (ht->entries != nullptr) {
    allocator_free(a, ht->entries, ht->capacity * ht->entry_size);
  }
  hash_table_t empty = {};
  *ht = empty;
}

void hash_table_resize(hash_table_t* ht, size_t new_capacity, allocator_t a);

inline void* hash_table_put_with_hash(hash_table_t* ht, const void* key,
                                      uint32_t h, allocator_t a) {
  assert(ht->key_size > 0 && ht->entry_size > 0 && ht->hash_fn != nullptr &&
         ht->eq_fn != nullptr && "HashTable must be initialized before use!");
  if (ht->capacity == 0 || ht->size * 2 > ht->capacity) {
    size_t new_capacity = ht->capacity == 0 ? 16 : ht->capacity * 2;
    hash_table_resize(ht, new_capacity, a);
  }

  size_t idx = h & ht->capacity_mask;
  void* entry = (char*)ht->entries + idx * ht->entry_size;
  void* result_value = nullptr;

  while (*hash_table_entry_occupied(ht, entry) && result_value == nullptr) {
    if (*hash_table_entry_hash(ht, entry) == h &&
        ht->eq_fn(hash_table_entry_key(ht, entry), key, ht->ctx)) {
      result_value = hash_table_entry_value(ht, entry);
    } else {
      idx = (idx + 1) & ht->capacity_mask;
      entry = (char*)ht->entries + idx * ht->entry_size;
    }
  }

  if (result_value == nullptr) {
    memcpy(hash_table_entry_key(ht, entry), key, ht->key_size);
    *hash_table_entry_hash(ht, entry) = h;
    *hash_table_entry_occupied(ht, entry) = true;
    ht->size++;
    result_value = hash_table_entry_value(ht, entry);
  }

  return result_value;
}

inline void* hash_table_put(hash_table_t* ht, const void* key, allocator_t a) {
  uint32_t h = ht->hash_fn(key, ht->ctx);
  void* result = hash_table_put_with_hash(ht, key, h, a);
  return result;
}

inline void* hash_table_get_with_hash(const hash_table_t* ht, const void* key,
                                      uint32_t h) {
  assert(ht->key_size > 0 && ht->entry_size > 0 && ht->hash_fn != nullptr &&
         ht->eq_fn != nullptr && "HashTable must be initialized before use!");
  void* result_value = nullptr;
  if (ht->capacity > 0) {
    size_t idx = h & ht->capacity_mask;
    void* entry = (char*)ht->entries + idx * ht->entry_size;

    while (*hash_table_entry_occupied(ht, entry) && result_value == nullptr) {
      if (*hash_table_entry_hash(ht, entry) == h &&
          ht->eq_fn(hash_table_entry_key(ht, entry), key, ht->ctx)) {
        result_value = hash_table_entry_value(ht, entry);
      } else {
        idx = (idx + 1) & ht->capacity_mask;
        entry = (char*)ht->entries + idx * ht->entry_size;
      }
    }
  }
  return result_value;
}

inline void* hash_table_get(const hash_table_t* ht, const void* key) {
  void* result = nullptr;
  if (ht->capacity > 0) {
    uint32_t h = ht->hash_fn(key, ht->ctx);
    result = hash_table_get_with_hash(ht, key, h);
  }
  return result;
}

inline void hash_table_clear(hash_table_t* ht) {
  if (ht->entries != nullptr) {
    memset(ht->entries, 0, ht->capacity * ht->entry_size);
  }
  ht->size = 0;
}

inline void hash_table_resize(hash_table_t* ht, size_t new_capacity,
                              allocator_t a) {
  assert(ht->key_size > 0 && ht->entry_size > 0 && ht->hash_fn != nullptr &&
         ht->eq_fn != nullptr && "HashTable must be initialized before use!");
  if (new_capacity < 4) {
    new_capacity = 4;
  }
  size_t cap = 1;
  while (cap < new_capacity) {
    cap <<= 1;
  }

  hash_table_t new_ht = *ht;
  new_ht.entries = allocator_alloc(a, cap * ht->entry_size);
  memset(new_ht.entries, 0, cap * ht->entry_size);
  new_ht.size = 0;
  new_ht.capacity = cap;
  new_ht.capacity_mask = cap - 1;

  for (size_t i = 0; i < ht->capacity; i++) {
    void* entry = (char*)ht->entries + i * ht->entry_size;
    if (*hash_table_entry_occupied(ht, entry)) {
      void* key = hash_table_entry_key(ht, entry);
      void* value = hash_table_entry_value(ht, entry);
      uint32_t h = *hash_table_entry_hash(ht, entry);

      void* new_val_slot = hash_table_put_with_hash(&new_ht, key, h, a);
      memcpy(new_val_slot, value, ht->value_size);
    }
  }

  hash_table_deinit(ht, a);
  *ht = new_ht;
}

#ifdef __cplusplus
}
#endif

// ============================================================================
// C++ Source-Level Compatibility Wrapper
// ============================================================================
#ifdef __cplusplus

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

  // Must match hash_table_t layout exactly
  Entry* entries;
  size_t size;
  size_t capacity;
  size_t capacity_mask;
  size_t key_size;
  size_t value_size;
  size_t key_align;
  size_t value_align;
  size_t entry_size;
  size_t value_offset;
  size_t hash_offset;
  size_t occupied_offset;
  uint32_t (*hash_fn_ptr)(const void* key, void* ctx);
  bool (*eq_fn_ptr)(const void* a, const void* b, void* ctx);
  void* ctx;

  // Stateful functors
  Hash hash_fn;
  Eq eq_fn;

  static uint32_t cpp_hash_wrapper(const void* key, void* ctx) {
    auto* self = static_cast<HashTable<K, V, Hash, Eq>*>(ctx);
    return self->hash_fn(*static_cast<const K*>(key));
  }

  static bool cpp_eq_wrapper(const void* a, const void* b, void* ctx) {
    auto* self = static_cast<HashTable<K, V, Hash, Eq>*>(ctx);
    return self->eq_fn(*static_cast<const K*>(a), *static_cast<const K*>(b));
  }
};

template <typename K, typename V, typename Hash, typename Eq>
inline void hash_table_ensure_initialized(HashTable<K, V, Hash, Eq>* ht) {
  if (ht->key_size == 0) {
    ht->key_size = sizeof(K);
    ht->value_size = sizeof(V);
    ht->key_align = alignof(K);
    ht->value_align = alignof(V);
    ht->hash_fn_ptr = HashTable<K, V, Hash, Eq>::cpp_hash_wrapper;
    ht->eq_fn_ptr = HashTable<K, V, Hash, Eq>::cpp_eq_wrapper;
    ht->ctx = ht;
    hash_table_calculate_layout(reinterpret_cast<hash_table_t*>(ht));
  }
}

template <typename K, typename V, typename Hash, typename Eq>
inline void hash_table_deinit(HashTable<K, V, Hash, Eq>* ht, allocator_t a) {
  hash_table_deinit(reinterpret_cast<hash_table_t*>(ht), a);
}

template <typename K, typename V, typename Hash, typename Eq>
inline void hash_table_resize(HashTable<K, V, Hash, Eq>* ht, allocator_t a,
                              size_t new_capacity) {
  hash_table_ensure_initialized(ht);
  // Reorder allocator to the end
  hash_table_resize(reinterpret_cast<hash_table_t*>(ht), new_capacity, a);
}

template <typename K, typename V, typename Hash, typename Eq>
inline void hash_table_put_with_hash(HashTable<K, V, Hash, Eq>* ht,
                                     allocator_t a, const K& key,
                                     const V& value, uint32_t h) {
  hash_table_ensure_initialized(ht);
  // Reorder allocator to the end
  void* val_slot =
      hash_table_put_with_hash(reinterpret_cast<hash_table_t*>(ht), &key, h, a);
  *static_cast<V*>(val_slot) = value;
}

template <typename K, typename V, typename Hash, typename Eq>
inline void hash_table_put(HashTable<K, V, Hash, Eq>* ht, allocator_t a,
                           const K& key, const V& value) {
  hash_table_ensure_initialized(ht);
  // Reorder allocator to the end
  void* val_slot = hash_table_put(reinterpret_cast<hash_table_t*>(ht), &key, a);
  *static_cast<V*>(val_slot) = value;
}

template <typename K, typename V, typename Hash, typename Eq>
inline V* hash_table_get_with_hash(HashTable<K, V, Hash, Eq>* ht, const K& key,
                                   uint32_t h) {
  hash_table_ensure_initialized(ht);
  void* val_slot = hash_table_get_with_hash(
      reinterpret_cast<const hash_table_t*>(ht), &key, h);
  return static_cast<V*>(val_slot);
}

template <typename K, typename V, typename Hash, typename Eq>
inline V* hash_table_get(HashTable<K, V, Hash, Eq>* ht, const K& key) {
  hash_table_ensure_initialized(ht);
  void* val_slot =
      hash_table_get(reinterpret_cast<const hash_table_t*>(ht), &key);
  return static_cast<V*>(val_slot);
}

template <typename K, typename V, typename Hash, typename Eq>
inline void hash_table_clear(HashTable<K, V, Hash, Eq>* ht) {
  hash_table_clear(reinterpret_cast<hash_table_t*>(ht));
}

#endif  // __cplusplus

#endif  // ZTRACING_SRC_HASH_TABLE_H_
