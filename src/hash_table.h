#ifndef SRC_HASH_TABLE_H
#define SRC_HASH_TABLE_H

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "core/allocator.h"
#include "core/logging.h"

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

#define HASH_TABLE_ASSERT_INITIALIZED(ht)                      \
  assert((ht)->key_size > 0 && (ht)->entry_size > 0 &&         \
         (ht)->hash_fn != nullptr && (ht)->eq_fn != nullptr && \
         "HashTable must be initialized before use!")

static inline void hash_table_calculate_layout(hash_table_t* ht) {
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

static inline hash_table_t hash_table_init_(
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

static inline void* hash_table_entry_key(const hash_table_t* ht, void* entry) {
  (void)ht;
  return entry;
}

static inline void* hash_table_entry_value(const hash_table_t* ht,
                                           void* entry) {
  return (char*)entry + ht->value_offset;
}

static inline uint32_t* hash_table_entry_hash(const hash_table_t* ht,
                                              void* entry) {
  return (uint32_t*)((char*)entry + ht->hash_offset);
}

static inline bool* hash_table_entry_occupied(const hash_table_t* ht,
                                              void* entry) {
  return (bool*)((char*)entry + ht->occupied_offset);
}

static inline void hash_table_deinit(hash_table_t* ht, allocator_t* a) {
  if (ht->entries != nullptr) {
    allocator_free(a, ht->entries, ht->capacity * ht->entry_size);
  }
  hash_table_t empty = {};
  *ht = empty;
}

static inline void hash_table_resize(hash_table_t* ht, size_t new_capacity,
                                     allocator_t* a);

static inline void* hash_table_put_with_hash(hash_table_t* ht, const void* key,
                                             uint32_t h, allocator_t* a) {
  HASH_TABLE_ASSERT_INITIALIZED(ht);
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

static inline void* hash_table_put(hash_table_t* ht, const void* key,
                                   allocator_t* a) {
  HASH_TABLE_ASSERT_INITIALIZED(ht);
  uint32_t h = ht->hash_fn(key, ht->ctx);
  void* result = hash_table_put_with_hash(ht, key, h, a);
  return result;
}

static inline void* hash_table_get_with_hash(const hash_table_t* ht,
                                             const void* key, uint32_t h) {
  HASH_TABLE_ASSERT_INITIALIZED(ht);
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

static inline void* hash_table_get(const hash_table_t* ht, const void* key) {
  HASH_TABLE_ASSERT_INITIALIZED(ht);
  void* result = nullptr;
  if (ht->capacity > 0) {
    uint32_t h = ht->hash_fn(key, ht->ctx);
    result = hash_table_get_with_hash(ht, key, h);
  }
  return result;
}

static inline void hash_table_clear(hash_table_t* ht) {
  if (ht->entries != nullptr) {
    memset(ht->entries, 0, ht->capacity * ht->entry_size);
  }
  ht->size = 0;
}

static inline void hash_table_resize(hash_table_t* ht, size_t new_capacity,
                                     allocator_t* a) {
  HASH_TABLE_ASSERT_INITIALIZED(ht);
  if (new_capacity < 4) {
    new_capacity = 4;
  }
  size_t cap = 1;
  while (cap < new_capacity) {
    cap <<= 1;
  }

  if (cap > (size_t)-1 / ht->entry_size) {
    LOG_ERROR("Fatal Error: Hash table capacity overflow.");
    abort();
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

#endif  // SRC_HASH_TABLE_H
