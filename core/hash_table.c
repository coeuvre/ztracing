#include "core/hash_table.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "core/assert.h"

#define HASH_TABLE_ASSERT_INITIALIZED(ht)                      \
  assert((ht)->hash_fn != nullptr && (ht)->eq_fn != nullptr && \
         (ht)->entry_size > 0 && "Hash table must be initialized before use!")

static_assert(sizeof(hash_table_t(int, int)) == sizeof(hash_table_generic_t),
              "hash_table_t layout must match hash_table_generic_t");

static inline void* entry_key(const hash_table_generic_t* ht, void* entry) {
  return (char*)entry + ht->key_offset;
}

static inline void* entry_value(const hash_table_generic_t* ht, void* entry) {
  return (char*)entry + ht->value_offset;
}

static inline uint32_t* entry_hash(const hash_table_generic_t* ht,
                                   void* entry) {
  (void)ht;
  return (uint32_t*)entry;
}

static inline bool* entry_occupied(const hash_table_generic_t* ht,
                                   void* entry) {
  (void)ht;
  return (bool*)((char*)entry + 4);
}

void hash_table_deinit_(hash_table_generic_t* ht, allocator_t* a) {
  if (ht->entries != nullptr) {
    allocator_free(a, ht->entries, ht->capacity * ht->entry_size);
  }
  hash_table_generic_t empty = {};
  *ht = empty;
}

void hash_table_clear_(hash_table_generic_t* ht) {
  if (ht->entries != nullptr) {
    memset(ht->entries, 0, ht->capacity * ht->entry_size);
  }
  ht->size = 0;
}

static void hash_table_resize_(hash_table_generic_t* ht, size_t new_capacity,
                               allocator_t* a) {
  if (new_capacity < 4) {
    new_capacity = 4;
  }
  size_t cap = 1;
  while (cap < new_capacity) {
    cap <<= 1;
  }

  expect(cap <= (size_t)-1 / ht->entry_size && "Hash table capacity overflow");

  hash_table_generic_t new_ht = *ht;
  new_ht.entries = allocator_alloc(a, cap * ht->entry_size);
  memset(new_ht.entries, 0, cap * ht->entry_size);
  new_ht.size = 0;
  new_ht.capacity = cap;
  new_ht.capacity_mask = cap - 1;

  for (size_t i = 0; i < ht->capacity; i++) {
    void* entry = (char*)ht->entries + i * ht->entry_size;
    if (*entry_occupied(ht, entry)) {
      uint32_t h = *entry_hash(ht, entry);

      size_t idx = h & new_ht.capacity_mask;
      void* new_entry = (char*)new_ht.entries + idx * new_ht.entry_size;
      while (*entry_occupied(&new_ht, new_entry)) {
        idx = (idx + 1) & new_ht.capacity_mask;
        new_entry = (char*)new_ht.entries + idx * new_ht.entry_size;
      }
      memcpy(new_entry, entry, ht->entry_size);
      new_ht.size++;
    }
  }

  if (ht->entries != nullptr) {
    allocator_free(a, ht->entries, ht->capacity * ht->entry_size);
  }
  *ht = new_ht;
}

void* hash_table_get_(const hash_table_generic_t* ht, const void* key) {
  HASH_TABLE_ASSERT_INITIALIZED(ht);
  void* result_value = nullptr;
  if (ht->capacity > 0) {
    uint32_t h = ht->hash_fn(key, ht->ctx);
    size_t idx = h & ht->capacity_mask;
    void* entry = (char*)ht->entries + idx * ht->entry_size;

    while (*entry_occupied(ht, entry) && result_value == nullptr) {
      if (*entry_hash(ht, entry) == h &&
          ht->eq_fn(entry_key(ht, entry), key, ht->ctx)) {
        result_value = entry_value(ht, entry);
      } else {
        idx = (idx + 1) & ht->capacity_mask;
        entry = (char*)ht->entries + idx * ht->entry_size;
      }
    }
  }
  return result_value;
}

void* hash_table_put_(hash_table_generic_t* ht, const void* key,
                      allocator_t* a) {
  HASH_TABLE_ASSERT_INITIALIZED(ht);
  if (ht->capacity == 0 || ht->size * 2 > ht->capacity) {
    size_t new_capacity = ht->capacity == 0 ? 16 : ht->capacity * 2;
    hash_table_resize_(ht, new_capacity, a);
  }

  uint32_t h = ht->hash_fn(key, ht->ctx);
  size_t idx = h & ht->capacity_mask;
  void* entry = (char*)ht->entries + idx * ht->entry_size;
  void* result_value = nullptr;

  while (*entry_occupied(ht, entry) && result_value == nullptr) {
    if (*entry_hash(ht, entry) == h &&
        ht->eq_fn(entry_key(ht, entry), key, ht->ctx)) {
      result_value = entry_value(ht, entry);
    } else {
      idx = (idx + 1) & ht->capacity_mask;
      entry = (char*)ht->entries + idx * ht->entry_size;
    }
  }

  if (result_value == nullptr) {
    memcpy(entry_key(ht, entry), key, ht->key_size);
    *entry_hash(ht, entry) = h;
    *entry_occupied(ht, entry) = true;
    ht->size++;
    result_value = entry_value(ht, entry);
  }

  return result_value;
}
