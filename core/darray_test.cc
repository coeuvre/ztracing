#include "core/darray.h"

#include <gtest/gtest.h>

#include "core/allocator.h"
#include "core/arena.h"

TEST(darray_test, init_empty_zii) {
  darray_t(int) arr = {};
  EXPECT_EQ(arr.len, (size_t)0);
  EXPECT_EQ(arr.cap, (size_t)0);
  EXPECT_EQ(arr.ptr, nullptr);
}

TEST(darray_test, push_and_get) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  darray_t(int) arr = {};

  darray_push(&arr, 10, alloc);
  darray_push(&arr, 20, alloc);
  darray_push(&arr, 30, alloc);

  EXPECT_EQ(arr.len, (size_t)3);
  EXPECT_GE(arr.cap, (size_t)3);

  EXPECT_EQ(*darray_get(&arr, 0), 10);
  EXPECT_EQ(*darray_get(&arr, 1), 20);
  EXPECT_EQ(*darray_get(&arr, 2), 30);

  // Direct pointer access
  EXPECT_EQ(arr.ptr[0], 10);
  EXPECT_EQ(arr.ptr[1], 20);
  EXPECT_EQ(arr.ptr[2], 30);

  darray_deinit(&arr, alloc);
  EXPECT_EQ(arr.len, (size_t)0);
  EXPECT_EQ(arr.cap, (size_t)0);
  EXPECT_EQ(arr.ptr, nullptr);

  arena_destroy(a);
}

TEST(darray_test, pop) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  darray_t(int) arr = {};

  darray_push(&arr, 100, alloc);
  darray_push(&arr, 200, alloc);

  int* popped = darray_pop(&arr);
  ASSERT_NE(popped, nullptr);
  EXPECT_EQ(*popped, 200);
  EXPECT_EQ(arr.len, (size_t)1);

  popped = darray_pop(&arr);
  ASSERT_NE(popped, nullptr);
  EXPECT_EQ(*popped, 100);
  EXPECT_EQ(arr.len, (size_t)0);

  // Pop empty
  popped = darray_pop(&arr);
  EXPECT_EQ(popped, nullptr);

  darray_deinit(&arr, alloc);
  arena_destroy(a);
}

TEST(darray_test, clear) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  darray_t(int) arr = {};

  darray_push(&arr, 1, alloc);
  darray_push(&arr, 2, alloc);
  size_t cap = arr.cap;

  darray_clear(&arr);
  EXPECT_EQ(arr.len, (size_t)0);
  EXPECT_EQ(arr.cap, cap);  // Capacity is preserved

  darray_deinit(&arr, alloc);
  arena_destroy(a);
}

TEST(darray_test, reserve_and_resize) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  darray_t(int) arr = {};

  darray_reserve(&arr, 50, alloc);
  EXPECT_GE(arr.cap, (size_t)50);
  EXPECT_EQ(arr.len, (size_t)0);

  darray_resize(&arr, 20, alloc);
  EXPECT_EQ(arr.len, (size_t)20);
  EXPECT_GE(arr.cap, (size_t)20);

  // Initialize the resized elements to verify we can write to them
  for (size_t i = 0; i < arr.len; ++i) {
    arr.ptr[i] = (int)i;
  }

  EXPECT_EQ(*darray_get(&arr, 15), 15);

  darray_deinit(&arr, alloc);
  arena_destroy(a);
}

TEST(darray_test, push_n) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  darray_t(int) arr = {};

  int source[] = {1, 2, 3, 4, 5};
  darray_push_n(&arr, source, 5, alloc);

  EXPECT_EQ(arr.len, (size_t)5);
  for (size_t i = 0; i < 5; ++i) {
    EXPECT_EQ(arr.ptr[i], source[i]);
  }

  int source2[] = {6, 7};
  darray_push_n(&arr, source2, 2, alloc);

  EXPECT_EQ(arr.len, (size_t)7);
  EXPECT_EQ(arr.ptr[5], 6);
  EXPECT_EQ(arr.ptr[6], 7);

  darray_deinit(&arr, alloc);
  arena_destroy(a);
}

struct custom_struct {
  int x;
  double y;
};

TEST(darray_test, custom_struct_type_safety) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  darray_t(struct custom_struct) arr = {};

  struct custom_struct val1 = {10, 20.5};
  darray_push(&arr, val1, alloc);

  struct custom_struct val2 = {30, 40.5};
  darray_push(&arr, val2, alloc);

  EXPECT_EQ(arr.len, (size_t)2);
  EXPECT_EQ(arr.ptr[0].x, 10);
  EXPECT_EQ(arr.ptr[0].y, 20.5);
  EXPECT_EQ(arr.ptr[1].x, 30);
  EXPECT_EQ(arr.ptr[1].y, 40.5);

  darray_deinit(&arr, alloc);
  arena_destroy(a);
}

TEST(darray_test, into_array_nil_count) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  darray_t(int) arr = {};

  darray_push(&arr, 10, alloc);
  darray_push(&arr, 20, alloc);
  darray_push(&arr, 30, alloc);
  size_t original_len = arr.len;

  // Turn into plain C array (passing nullptr for count)
  int* raw_arr = darray_into_array(&arr, alloc, nullptr);

  // Verify container is reset (ZII)
  EXPECT_EQ(arr.ptr, nullptr);
  EXPECT_EQ(arr.len, (size_t)0);
  EXPECT_EQ(arr.cap, (size_t)0);

  // Verify raw array has the data
  ASSERT_NE(raw_arr, nullptr);
  EXPECT_EQ(raw_arr[0], 10);
  EXPECT_EQ(raw_arr[1], 20);
  EXPECT_EQ(raw_arr[2], 30);

  // We now own raw_arr, must free it
  allocator_free_array(alloc, raw_arr, int, original_len);

  arena_destroy(a);
}

TEST(darray_test, into_array_with_count) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  darray_t(int) arr = {};

  darray_push(&arr, 10, alloc);
  darray_push(&arr, 20, alloc);
  darray_push(&arr, 30, alloc);

  // Turn into plain C array (passing pointer to receive count)
  size_t count = 0;
  int* raw_arr = darray_into_array(&arr, alloc, &count);

  EXPECT_EQ(count, (size_t)3);
  ASSERT_NE(raw_arr, nullptr);
  EXPECT_EQ(raw_arr[0], 10);
  EXPECT_EQ(raw_arr[1], 20);
  EXPECT_EQ(raw_arr[2], 30);

  allocator_free_array(alloc, raw_arr, int, count);

  // Empty array case
  darray_t(int) empty_arr = {};
  size_t empty_count = 100;  // initialize to non-zero
  int* empty_raw = darray_into_array(&empty_arr, alloc, &empty_count);
  EXPECT_EQ(empty_raw, nullptr);
  EXPECT_EQ(empty_count, (size_t)0);

  arena_destroy(a);
}
