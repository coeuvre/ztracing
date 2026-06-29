#include "core/hash_table.h"

#include <gtest/gtest.h>

#include "core/allocator.h"

static uint32_t int32_hash(const int32_t* key, void* ctx) {
  (void)ctx;
  return *(const uint32_t*)key;
}

static bool int32_eq(const int32_t* a, const int32_t* b, void* ctx) {
  (void)ctx;
  return *a == *b;
}

typedef hash_table_t(int32_t, int) int_map_t;

TEST(hash_table_test, basic) {
  allocator_t* a = c_allocator();
  int_map_t ht = {};
  hash_table_init(&ht, int32_hash, int32_eq, nullptr);

  int32_t k1 = 1;
  hash_table_put(&ht, &k1, 10, a);
  int32_t k2 = 2;
  hash_table_put(&ht, &k2, 20, a);
  int32_t k3 = 3;
  hash_table_put(&ht, &k3, 30, a);

  EXPECT_EQ(ht.size, 3u);
  EXPECT_EQ(*hash_table_get(&ht, &k1), 10);
  EXPECT_EQ(*hash_table_get(&ht, &k2), 20);
  EXPECT_EQ(*hash_table_get(&ht, &k3), 30);

  int32_t k4 = 4;
  EXPECT_EQ(hash_table_get(&ht, &k4), nullptr);

  // Update
  hash_table_put(&ht, &k2, 200, a);
  EXPECT_EQ(ht.size, 3u);
  EXPECT_EQ(*hash_table_get(&ht, &k2), 200);

  hash_table_deinit(&ht, a);
}

static uint32_t colliding_hash(const int32_t* key, void* ctx) {
  (void)key;
  (void)ctx;
  return 42;  // Force all keys to collide!
}

TEST(hash_table_test, collision) {
  allocator_t* a = c_allocator();
  int_map_t ht = {};
  hash_table_init(&ht, colliding_hash, int32_eq, nullptr);

  // Put 5 keys, all of which will hash to 42 and collide
  for (int32_t i = 1; i <= 5; ++i) {
    int32_t key = i;
    hash_table_put(&ht, &key, i * 10, a);
  }

  EXPECT_EQ(ht.size, 5u);

  // Verify we can retrieve all of them successfully despite collisions
  for (int32_t i = 1; i <= 5; ++i) {
    int32_t key = i;
    int* val = hash_table_get(&ht, &key);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, i * 10);
  }

  // Update one of the colliding keys
  int32_t target_key = 3;
  hash_table_put(&ht, &target_key, 300, a);
  EXPECT_EQ(ht.size, 5u);
  EXPECT_EQ(*hash_table_get(&ht, &target_key), 300);

  hash_table_deinit(&ht, a);
}

struct HashCtx {
  uint32_t multiplier;
};

static uint32_t context_hash(const int32_t* key, void* ctx) {
  auto* hash_ctx = (HashCtx*)ctx;
  return *(const uint32_t*)key * hash_ctx->multiplier;
}

TEST(hash_table_test, context) {
  allocator_t* a = c_allocator();
  HashCtx ctx = {.multiplier = 137};
  int_map_t ht = {};
  hash_table_init(&ht, context_hash, int32_eq, &ctx);

  int32_t key = 10;
  hash_table_put(&ht, &key, 100, a);

  // Verify lookup works
  EXPECT_EQ(*hash_table_get(&ht, &key), 100);

  // Verify the hash was actually multiplied by 137!
  // The hash stored in the entry should be 10 * 137 = 1370
  size_t idx = 1370 & ht.capacity_mask;
  EXPECT_TRUE(ht.entries[idx].occupied);
  EXPECT_EQ(ht.entries[idx].hash, 1370u);

  hash_table_deinit(&ht, a);
}

TEST(hash_table_test, growth) {
  allocator_t* a = c_allocator();
  int_map_t ht = {};
  hash_table_init(&ht, int32_hash, int32_eq, nullptr);

  for (int32_t i = 0; i < 100; i++) {
    int32_t key = i;
    hash_table_put(&ht, &key, i * 10, a);
  }

  EXPECT_EQ(ht.size, 100u);
  EXPECT_GE(ht.capacity, 100u);

  for (int32_t i = 0; i < 100; i++) {
    int32_t key = i;
    int* val = hash_table_get(&ht, &key);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, i * 10);
  }

  hash_table_deinit(&ht, a);
}

TEST(hash_table_test, clear) {
  allocator_t* a = c_allocator();
  int_map_t ht = {};
  hash_table_init(&ht, int32_hash, int32_eq, nullptr);

  int32_t key = 1;
  hash_table_put(&ht, &key, 10, a);
  hash_table_clear(&ht);

  EXPECT_EQ(ht.size, 0u);
  EXPECT_EQ(hash_table_get(&ht, &key), nullptr);

  hash_table_deinit(&ht, a);
}

#ifndef NDEBUG
TEST(hash_table_test, uninitialized_assertion) {
  int_map_t ht = {};  // Completely zero-initialized, no init!
  allocator_t* a = c_allocator();
  int32_t key = 1;

  // Attempting to put or get should trigger safety assertions and abort
  EXPECT_DEATH(hash_table_put(&ht, &key, 10, a),
               "Hash table must be initialized before use!");
  EXPECT_DEATH(hash_table_get(&ht, &key),
               "Hash table must be initialized before use!");
}
#endif
