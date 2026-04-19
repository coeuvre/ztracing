#include "src/allocator.h"

#include <gtest/gtest.h>

TEST(AllocatorTest, DefaultAllocator) {
  Allocator a = allocator_get_default();
  void* ptr = allocator_alloc(a, 100);
  EXPECT_NE(ptr, nullptr);
  allocator_free(a, ptr, 100);
}

TEST(AllocatorTest, CountingAllocator) {
  CountingAllocator ca;
  counting_allocator_init(&ca, allocator_get_default());
  Allocator a = counting_allocator_get_allocator(&ca);

  EXPECT_EQ(counting_allocator_get_allocated_bytes(&ca), 0u);

  void* p1 = allocator_alloc(a, 100);
  EXPECT_NE(p1, nullptr);
  EXPECT_EQ(counting_allocator_get_allocated_bytes(&ca), 100u);

  void* p2 = allocator_alloc(a, 200);
  EXPECT_NE(p2, nullptr);
  EXPECT_EQ(counting_allocator_get_allocated_bytes(&ca), 300u);

  p1 = allocator_realloc(a, p1, 100, 150);
  EXPECT_NE(p1, nullptr);
  EXPECT_EQ(counting_allocator_get_allocated_bytes(&ca), 350u);

  allocator_free(a, p1, 150);
  EXPECT_EQ(counting_allocator_get_allocated_bytes(&ca), 200u);

  allocator_free(a, p2, 200);
  EXPECT_EQ(counting_allocator_get_allocated_bytes(&ca), 0u);
}

static void* fail_alloc(void* ctx, void* ptr, size_t old_size,
                        size_t new_size) {
  (void)ctx;
  (void)ptr;
  (void)old_size;
  (void)new_size;
  return nullptr;
}

TEST(AllocatorTest, CountingAllocatorFailure) {
  CountingAllocator ca;
  Allocator parent = {fail_alloc, nullptr};
  counting_allocator_init(&ca, parent);
  Allocator a = counting_allocator_get_allocator(&ca);

  void* p = allocator_alloc(a, 100);
  EXPECT_EQ(p, nullptr);
  EXPECT_EQ(counting_allocator_get_allocated_bytes(&ca), 0u);
}
