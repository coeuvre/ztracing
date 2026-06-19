#include "src/array_list.h"

#include <gtest/gtest.h>

#include "src/allocator.h"

TEST(array_list_test, zii) {
  // Completely zero-initialized, no init, no designated initializer!
  array_list_t al = {};
  allocator_t a = allocator_get_default();

  EXPECT_EQ(al.ptr, nullptr);
  EXPECT_EQ(al.len, 0u);
  EXPECT_EQ(al.cap, 0u);
  EXPECT_EQ(al.elem_size, 0u);

  // First push locks in the size using the new macro!
  int* slot = array_list_push(&al, int, a);
  EXPECT_NE(slot, nullptr);
  *slot = 42;

  EXPECT_EQ(al.len, 1u);
  EXPECT_GE(al.cap, 1u);
  EXPECT_EQ(al.elem_size, sizeof(int));

  EXPECT_EQ(*array_list_get(&al, int, 0), 42);

  array_list_deinit(&al, a);
}

TEST(array_list_test, push) {
  array_list_t al = {};
  allocator_t a = allocator_get_default();

  for (size_t i = 0; i < 100; ++i) {
    int* slot = array_list_push(&al, int, a);
    EXPECT_NE(slot, nullptr);
    *slot = (int)i;
  }

  EXPECT_EQ(al.len, 100u);
  EXPECT_GE(al.cap, 100u);

  int* data = (int*)al.ptr;
  for (size_t i = 0; i < 100; ++i) {
    EXPECT_EQ(data[i], (int)i);
  }

  array_list_deinit(&al, a);
  EXPECT_EQ(al.ptr, nullptr);
  EXPECT_EQ(al.len, 0u);
  EXPECT_EQ(al.cap, 0u);
}

TEST(array_list_test, pop) {
  array_list_t al = {};
  allocator_t a = allocator_get_default();

  *array_list_push(&al, int, a) = 10;
  *array_list_push(&al, int, a) = 20;
  EXPECT_EQ(al.len, 2u);

  // Pop using the new type-safe macro!
  int* popped = array_list_pop(&al, int);
  EXPECT_NE(popped, nullptr);
  EXPECT_EQ(*popped, 20);
  EXPECT_EQ(al.len, 1u);

  int* data = (int*)al.ptr;
  EXPECT_EQ(data[0], 10);

  popped = array_list_pop(&al, int);
  EXPECT_NE(popped, nullptr);
  EXPECT_EQ(*popped, 10);
  EXPECT_EQ(al.len, 0u);

  // Pop from empty list should return nullptr and be safe
  popped = array_list_pop(&al, int);
  EXPECT_EQ(popped, nullptr);
  EXPECT_EQ(al.len, 0u);

  array_list_deinit(&al, a);
}

TEST(array_list_test, clear) {
  array_list_t al = {};
  allocator_t a = allocator_get_default();

  *array_list_push(&al, int, a) = 1;
  *array_list_push(&al, int, a) = 2;
  EXPECT_EQ(al.len, 2u);

  array_list_clear(&al);
  EXPECT_EQ(al.len, 0u);
  EXPECT_GE(al.cap, 2u);

  array_list_deinit(&al, a);
}

TEST(array_list_test, memory_leak) {
  counting_allocator_t ca = counting_allocator_init(allocator_get_default());
  allocator_t a = counting_allocator_get_allocator(&ca);

  {
    array_list_t al = {};
    for (size_t i = 0; i < 100; ++i) {
      *array_list_push(&al, int, a) = (int)i;
    }
    EXPECT_GT(counting_allocator_get_allocated_bytes(&ca), 0u);
    array_list_deinit(&al, a);
  }

  EXPECT_EQ(counting_allocator_get_allocated_bytes(&ca), 0u);
}

TEST(array_list_test, reserve) {
  array_list_t al = {};
  allocator_t a = allocator_get_default();

  array_list_reserve(&al, 100, sizeof(int), a);
  EXPECT_EQ(al.len, 0u);
  EXPECT_EQ(al.cap, 100u);
  EXPECT_NE(al.ptr, nullptr);

  for (size_t i = 0; i < 100; ++i) {
    *array_list_push(&al, int, a) = (int)i;
  }
  EXPECT_EQ(al.len, 100u);
  EXPECT_EQ(al.cap, 100u);

  // Reserve less than current capacity should do nothing
  array_list_reserve(&al, 50, sizeof(int), a);
  EXPECT_EQ(al.cap, 100u);

  array_list_deinit(&al, a);
}

TEST(array_list_test, resize) {
  array_list_t al = {};
  allocator_t a = allocator_get_default();

  array_list_resize(&al, 100, sizeof(int), a);
  EXPECT_EQ(al.len, 100u);
  EXPECT_GE(al.cap, 100u);

  int* data = (int*)al.ptr;
  for (size_t i = 0; i < 100; i++) {
    data[i] = (int)i;
  }

  array_list_resize(&al, 50, sizeof(int), a);
  EXPECT_EQ(al.len, 50u);
  EXPECT_GE(al.cap, 100u);

  data = (int*)al.ptr;
  for (size_t i = 0; i < 50; i++) {
    EXPECT_EQ(data[i], (int)i);
  }

  array_list_deinit(&al, a);
}
