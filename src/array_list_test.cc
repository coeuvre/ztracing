#include "src/array_list.h"
#include <gtest/gtest.h>
#include "src/allocator.h"

TEST(ArrayListTest, ZII) {
  ArrayList<int> al = {};
  EXPECT_EQ(al.data, nullptr);
  EXPECT_EQ(al.size, 0u);
  EXPECT_EQ(al.capacity, 0u);
}

TEST(ArrayListTest, PushBack) {
  ArrayList<int> al = {};
  Allocator a = allocator_get_default();

  for (int i = 0; i < 100; ++i) {
    array_list_push_back(&al, a, i);
  }

  EXPECT_EQ(al.size, 100u);
  EXPECT_GE(al.capacity, 100u);

  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(al[i], i);
  }

  array_list_deinit(&al, a);
  EXPECT_EQ(al.data, nullptr);
  EXPECT_EQ(al.size, 0u);
  EXPECT_EQ(al.capacity, 0u);
}

TEST(ArrayListTest, PopBack) {
  ArrayList<int> al = {};
  Allocator a = allocator_get_default();

  array_list_push_back(&al, a, 10);
  array_list_push_back(&al, a, 20);
  EXPECT_EQ(al.size, 2u);

  array_list_pop_back(&al);
  EXPECT_EQ(al.size, 1u);
  EXPECT_EQ(al[0], 10);

  array_list_pop_back(&al);
  EXPECT_EQ(al.size, 0u);

  // Pop from empty list should be safe
  array_list_pop_back(&al);
  EXPECT_EQ(al.size, 0u);

  array_list_deinit(&al, a);
}

TEST(ArrayListTest, Clear) {
  ArrayList<int> al = {};
  Allocator a = allocator_get_default();

  array_list_push_back(&al, a, 1);
  array_list_push_back(&al, a, 2);
  EXPECT_EQ(al.size, 2u);

  array_list_clear(&al);
  EXPECT_EQ(al.size, 0u);
  EXPECT_GE(al.capacity, 2u);

  array_list_deinit(&al, a);
}

TEST(ArrayListTest, MemoryLeak) {
  CountingAllocator ca;
  counting_allocator_init(&ca, allocator_get_default());
  Allocator a = counting_allocator_get_allocator(&ca);

  {
    ArrayList<int> al = {};
    for (int i = 0; i < 100; ++i) {
      array_list_push_back(&al, a, i);
    }
    EXPECT_GT(counting_allocator_get_allocated_bytes(&ca), 0u);
    array_list_deinit(&al, a);
  }

  EXPECT_EQ(counting_allocator_get_allocated_bytes(&ca), 0u);
}

TEST(ArrayListTest, Reserve) {
  ArrayList<int> al = {};
  Allocator a = allocator_get_default();

  array_list_reserve(&al, a, 100);
  EXPECT_EQ(al.size, 0u);
  EXPECT_EQ(al.capacity, 100u);
  EXPECT_NE(al.data, nullptr);

  for (int i = 0; i < 100; ++i) {
    array_list_push_back(&al, a, i);
  }
  EXPECT_EQ(al.size, 100u);
  EXPECT_EQ(al.capacity, 100u);

  // Reserve less than current capacity should do nothing
  array_list_reserve(&al, a, 50);
  EXPECT_EQ(al.capacity, 100u);

  array_list_deinit(&al, a);
}
