extern "C" {
#include "core/allocator.h"
}

#include <gtest/gtest.h>
#include <string.h>

TEST(allocator_test, alloc_zeros_memory_by_default) {
  size_t size = 128;
  void* ptr = allocator_alloc(c_allocator(), size);
  ASSERT_NE(ptr, nullptr);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(((char*)ptr)[i], 0);
  }
  allocator_free(c_allocator(), ptr, size);
}

TEST(allocator_test, alloc_uninitialized_provides_memory) {
  size_t size = 128;
  void* ptr = allocator_alloc_uninitialized(c_allocator(), size);
  ASSERT_NE(ptr, nullptr);
  memset(ptr, 0xAB, size);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(((unsigned char*)ptr)[i], 0xAB);
  }
  allocator_free(c_allocator(), ptr, size);
}

TEST(allocator_test, realloc_zeros_only_new_memory) {
  size_t old_size = 16;
  size_t new_size = 32;

  void* ptr = allocator_alloc(c_allocator(), old_size);
  ASSERT_NE(ptr, nullptr);
  memset(ptr, 0x55, old_size);

  void* new_ptr = allocator_realloc(c_allocator(), ptr, old_size, new_size);
  ASSERT_NE(new_ptr, nullptr);

  // First old_size bytes should retain their value
  for (size_t i = 0; i < old_size; ++i) {
    EXPECT_EQ(((unsigned char*)new_ptr)[i], 0x55);
  }

  // Bytes from old_size to new_size should be zeroed
  for (size_t i = old_size; i < new_size; ++i) {
    EXPECT_EQ(((unsigned char*)new_ptr)[i], 0);
  }

  allocator_free(c_allocator(), new_ptr, new_size);
}

TEST(allocator_test, realloc_uninitialized_does_not_zero) {
  size_t old_size = 16;
  size_t new_size = 32;

  void* ptr = allocator_alloc_uninitialized(c_allocator(), old_size);
  ASSERT_NE(ptr, nullptr);
  memset(ptr, 0x77, old_size);

  void* new_ptr =
      allocator_realloc_uninitialized(c_allocator(), ptr, old_size, new_size);
  ASSERT_NE(new_ptr, nullptr);

  // First old_size bytes should retain their value
  for (size_t i = 0; i < old_size; ++i) {
    EXPECT_EQ(((unsigned char*)new_ptr)[i], 0x77);
  }

  allocator_free(c_allocator(), new_ptr, new_size);
}

struct dummy_struct {
  int a;
  double b;
  char c[16];
};

TEST(allocator_test, struct_macros_zero_by_default) {
  dummy_struct* s = allocator_alloc_struct(c_allocator(), dummy_struct);
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(s->a, 0);
  EXPECT_EQ(s->b, 0.0);
  for (int i = 0; i < 16; ++i) {
    EXPECT_EQ(s->c[i], 0);
  }
  allocator_free_struct(c_allocator(), s, dummy_struct);
}

TEST(allocator_test, array_macros_zero_by_default) {
  size_t count = 10;
  dummy_struct* arr = allocator_alloc_array(c_allocator(), dummy_struct, count);
  ASSERT_NE(arr, nullptr);
  for (size_t i = 0; i < count; ++i) {
    EXPECT_EQ(arr[i].a, 0);
    EXPECT_EQ(arr[i].b, 0.0);
    for (int j = 0; j < 16; ++j) {
      EXPECT_EQ(arr[i].c[j], 0);
    }
  }

  size_t new_count = 20;
  dummy_struct* new_arr = allocator_realloc_array(c_allocator(), dummy_struct,
                                                  arr, count, new_count);
  ASSERT_NE(new_arr, nullptr);
  for (size_t i = count; i < new_count; ++i) {
    EXPECT_EQ(new_arr[i].a, 0);
    EXPECT_EQ(new_arr[i].b, 0.0);
    for (int j = 0; j < 16; ++j) {
      EXPECT_EQ(new_arr[i].c[j], 0);
    }
  }

  allocator_free_array(c_allocator(), new_arr, dummy_struct, new_count);
}

TEST(allocator_test, page_allocator_basic) {
  allocator_t* a = page_allocator();
  ASSERT_NE(a, nullptr);
  EXPECT_GT(a->page_size, 0u);

  // Allocate 1 page
  size_t size1 = a->page_size;
  void* ptr = allocator_alloc(a, size1);
  ASSERT_NE(ptr, nullptr);
  // Write some data
  memset(ptr, 0xAA, size1);

  // Grow to 2 pages
  size_t size2 = a->page_size * 2;
  void* ptr2 = allocator_realloc(a, ptr, size1, size2);
  ASSERT_NE(ptr2, nullptr);
  // Verify old data is intact
  for (size_t i = 0; i < size1; ++i) {
    EXPECT_EQ(((unsigned char*)ptr2)[i], 0xAA);
  }
  // Verify new data is zeroed (since allocator_realloc zeroes new memory)
  for (size_t i = size1; i < size2; ++i) {
    EXPECT_EQ(((unsigned char*)ptr2)[i], 0);
  }

  // Shrink back to 1 page
  void* ptr3 = allocator_realloc(a, ptr2, size2, size1);
  ASSERT_NE(ptr3, nullptr);
  // Verify data is still intact
  for (size_t i = 0; i < size1; ++i) {
    EXPECT_EQ(((unsigned char*)ptr3)[i], 0xAA);
  }

  allocator_free(a, ptr3, size1);
}
