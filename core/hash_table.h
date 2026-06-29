#ifndef CORE_HASH_TABLE_H
#define CORE_HASH_TABLE_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Generic hash table structure.
 *
 * This serves as the untyped backend for all hash tables. Users should not
 * interact with this structure directly, but rather use the type-safe
 * `hash_table_t` macro and its associated wrapper APIs.
 *
 * It uses runtime offsets (calculated during initialization) to locate
 * keys and values within the contiguous `entries` array.
 */
typedef struct hash_table_generic {
  void* entries;
  size_t size;
  size_t capacity;
  size_t capacity_mask;
  uint32_t (*hash_fn)(const void* key, void* ctx);
  bool (*eq_fn)(const void* a, const void* b, void* ctx);
  void* ctx;
  size_t entry_size;
  size_t key_size;
  size_t key_offset;
  size_t value_offset;
} hash_table_generic_t;

/**
 * Defines a type-safe hash table structure.
 *
 * Example:
 *   typedef hash_table_t(string_t, int) string_int_map_t;
 *   string_int_map_t map = {}; // Zero-Is-Initialization (ZII) compatible
 *
 * @param key_t The type of the keys (must be copyable via memcpy).
 * @param value_t The type of the values (must be copyable via memcpy).
 */
#define hash_table_t(key_t, value_t)                          \
  struct {                                                    \
    struct {                                                  \
      uint32_t hash;                                          \
      bool occupied;                                          \
      key_t key;                                              \
      value_t value;                                          \
    }* entries;                                               \
    size_t size;                                              \
    size_t capacity;                                          \
    size_t capacity_mask;                                     \
    uint32_t (*hash_fn)(const key_t* key, void* ctx);         \
    bool (*eq_fn)(const key_t* a, const key_t* b, void* ctx); \
    void* ctx;                                                \
    size_t entry_size;                                        \
    size_t key_size;                                          \
    size_t key_offset;                                        \
    size_t value_offset;                                      \
  }

/**
 * Initializes a hash table.
 *
 * This must be called before any get/put operations. It calculates and caches
 * the necessary layout offsets of the typed entry structure.
 *
 * The hash table remains Zero-Is-Initialization (ZII) compatible; no memory
 * is allocated until the first `hash_table_put` call.
 *
 * @param ht Pointer to the hash table instance.
 * @param hash_f Hash function: `uint32_t (*)(const key_t* key, void* ctx)`
 * @param eq_f Equality function: `bool (*)(const key_t* a, const key_t* b,
 * void* ctx)`
 * @param context User-defined context pointer passed to hash_f and eq_f.
 */
#define hash_table_init(ht, hash_f, eq_f, context)                       \
  do {                                                                   \
    (ht)->hash_fn = (hash_f);                                            \
    (ht)->eq_fn = (eq_f);                                                \
    (ht)->ctx = (context);                                               \
    (ht)->entry_size = sizeof(*(ht)->entries);                           \
    (ht)->key_size = sizeof((ht)->entries[0].key);                       \
    (ht)->key_offset = (size_t)&((__typeof__((ht)->entries))0)->key;     \
    (ht)->value_offset = (size_t)&((__typeof__((ht)->entries))0)->value; \
  } while (0)

// Generic out-of-line functions (internal use only)
void hash_table_deinit_(hash_table_generic_t* ht, allocator_t* a);
void hash_table_clear_(hash_table_generic_t* ht);
void* hash_table_get_(const hash_table_generic_t* ht, const void* key);
void* hash_table_put_(hash_table_generic_t* ht, const void* key,
                      allocator_t* a);

/**
 * Deinitializes the hash table and frees all allocated memory.
 * The table is reset to a zeroed state.
 */
#define hash_table_deinit(ht, allocator) \
  hash_table_deinit_((hash_table_generic_t*)(ht), (allocator))

/**
 * Clears all entries in the hash table, setting its size to 0.
 * The allocated capacity is preserved.
 */
#define hash_table_clear(ht) hash_table_clear_((hash_table_generic_t*)(ht))

/**
 * Looks up a key in the hash table.
 *
 * Compiles-time checks that `p_key` is compatible with `const key_t*`.
 *
 * @param ht Pointer to the hash table.
 * @param p_key Pointer to the key to look up (`const key_t*`).
 * @return Pointer to the value slot (`value_t*`), or `nullptr` if not found.
 */
#define hash_table_get(ht, p_key)                        \
  ((void)(&(ht)->entries[0].key == (p_key)),             \
   (__typeof__(&(ht)->entries[0].value))hash_table_get_( \
       (const hash_table_generic_t*)(ht), (p_key)))

/**
 * Inserts or updates a key-value pair in the hash table.
 *
 * Compiles-time checks that `p_key` is compatible with `const key_t*`, and
 * `val` is compatible with `value_t`.
 *
 * If the key already exists, its value is updated. If the table exceeds its
 * load factor, it is automatically resized.
 *
 * @param ht Pointer to the hash table.
 * @param p_key Pointer to the key to insert/update (`const key_t*`).
 * @param val The value to associate with the key (`value_t`).
 * @param allocator Allocator used if the table needs to allocate/resize.
 * @return The assigned value (`value_t`).
 */
#define hash_table_put(ht, p_key, val, allocator)         \
  ((void)(&(ht)->entries[0].key == (p_key)),              \
   *(__typeof__(&(ht)->entries[0].value))hash_table_put_( \
       (hash_table_generic_t*)(ht), (p_key), (allocator)) = (val))

#ifdef __cplusplus
}
#endif

#endif  // CORE_HASH_TABLE_H
