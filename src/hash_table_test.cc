#include "src/hash_table.h"

#include <gtest/gtest.h>

struct IntKey {
  int value;
};

struct IntKeyHash {
  uint32_t operator()(const IntKey& k) const { return (uint32_t)k.value; }
};

struct IntKeyEq {
  bool operator()(const IntKey& a, const IntKey& b) const { return a.value == b.value; }
};

TEST(HashTableTest, Basic) {
  Allocator a = allocator_get_default();
  HashTable<IntKey, int, IntKeyHash, IntKeyEq> ht;
  hash_table_init(&ht, a);

  hash_table_put(&ht, a, {1}, 10);
  hash_table_put(&ht, a, {2}, 20);
  hash_table_put(&ht, a, {3}, 30);

  EXPECT_EQ(ht.size, 3u);
  EXPECT_EQ(*hash_table_get(&ht, {1}), 10);
  EXPECT_EQ(*hash_table_get(&ht, {2}), 20);
  EXPECT_EQ(*hash_table_get(&ht, {3}), 30);
  EXPECT_EQ(hash_table_get(&ht, {4}), nullptr);

  // Update
  hash_table_put(&ht, a, {2}, 200);
  EXPECT_EQ(ht.size, 3u);
  EXPECT_EQ(*hash_table_get(&ht, {2}), 200);

  hash_table_deinit(&ht, a);
}

TEST(HashTableTest, DefaultTypes) {
  Allocator a = allocator_get_default();
  HashTable<int32_t, int> ht; // Uses DefaultHash<int32_t> and DefaultEq<int32_t>
  hash_table_init(&ht, a);

  hash_table_put(&ht, a, 1, 10);
  hash_table_put(&ht, a, 2, 20);

  EXPECT_EQ(ht.size, 2u);
  EXPECT_EQ(*hash_table_get(&ht, 1), 10);
  EXPECT_EQ(*hash_table_get(&ht, 2), 20);

  hash_table_deinit(&ht, a);
}

TEST(HashTableTest, Growth) {
  Allocator a = allocator_get_default();
  HashTable<int32_t, int> ht;
  hash_table_init(&ht, a, 4);

  for (int i = 0; i < 100; i++) {
    hash_table_put(&ht, a, i, i * 10);
  }

  EXPECT_EQ(ht.size, 100u);
  EXPECT_GE(ht.capacity, 100u);

  for (int i = 0; i < 100; i++) {
    int* val = hash_table_get(&ht, i);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, i * 10);
  }

  hash_table_deinit(&ht, a);
}

TEST(HashTableTest, Clear) {
  Allocator a = allocator_get_default();
  HashTable<int32_t, int> ht;
  hash_table_init(&ht, a);

  hash_table_put(&ht, a, 1, 10);
  hash_table_clear(&ht);

  EXPECT_EQ(ht.size, 0u);
  EXPECT_EQ(hash_table_get(&ht, 1), nullptr);

  hash_table_deinit(&ht, a);
}
