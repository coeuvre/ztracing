#include "core/arena.h"

#include <gtest/gtest.h>

#include "core/allocator.h"

TEST(arena_test, basic) {
  allocator_t backing = allocator_get_default();
  arena_t arena = {};
  arena_init(&arena, backing, 1024);  // 1KB blocks

  // Test basic allocation
  void* p1 = arena_alloc(&arena, 10);
  ASSERT_NE(p1, nullptr);

  // Test alignment (should be 8-byte aligned)
  uintptr_t addr1 = (uintptr_t)p1;
  EXPECT_EQ(addr1 % 8, 0u);

  void* p2 = arena_alloc(&arena, 20);
  ASSERT_NE(p2, nullptr);
  uintptr_t addr2 = (uintptr_t)p2;
  EXPECT_EQ(addr2 % 8, 0u);

  // They should be distinct and contiguous (modulo alignment)
  EXPECT_GE(addr2, addr1 + 16);  // 10 bytes aligned up to 16

  arena_deinit(&arena);
}

TEST(arena_test, multi_block) {
  allocator_t backing = allocator_get_default();
  arena_t arena = {};
  arena_init(&arena, backing, 100);  // Small blocks to force growth

  // Allocate something larger than block size
  void* p1 = arena_alloc(&arena, 200);
  ASSERT_NE(p1, nullptr);

  // Allocate many small things to trigger new blocks
  void* last_p = nullptr;
  for (int i = 0; i < 50; i++) {
    void* p = arena_alloc(&arena, 10);
    ASSERT_NE(p, nullptr);
    if (last_p != nullptr) {
      EXPECT_NE(p, last_p);
    }
    last_p = p;
  }

  arena_deinit(&arena);
}

TEST(arena_test, reset) {
  allocator_t backing = allocator_get_default();
  arena_t arena = {};
  arena_init(&arena, backing, 1024);

  void* p1 = arena_alloc(&arena, 100);
  void* p2 = arena_alloc(&arena, 200);

  arena_reset(&arena);

  // After reset, the first allocation should return the same address as p1
  void* p3 = arena_alloc(&arena, 100);
  EXPECT_EQ(p3, p1);

  void* p4 = arena_alloc(&arena, 200);
  EXPECT_EQ(p4, p2);

  arena_deinit(&arena);
}

TEST(arena_test, allocator_interface) {
  allocator_t backing = allocator_get_default();
  arena_t arena = {};
  arena_init(&arena, backing, 1024);

  allocator_t a = arena_get_allocator(&arena);

  // Test alloc
  void* p1 = allocator_alloc(a, 100);
  ASSERT_NE(p1, nullptr);

  // Test realloc (grow)
  void* p2 = allocator_realloc(a, p1, 100, 200);
  ASSERT_NE(p2, nullptr);
  // It might be the same or different, but data should be preserved if we had
  // any Let's write to it first to test copy
  char* s1 = (char*)p1;
  strcpy(s1, "hello");

  void* p3 = allocator_realloc(a, p1, 100, 200);
  char* s3 = (char*)p3;
  EXPECT_STREQ(s3, "hello");

  // Test realloc (shrink or same)
  void* p4 = allocator_realloc(a, p3, 200, 150);
  EXPECT_EQ(p4, p3);  // Should return same pointer

  // Test free (no-op, but shouldn't crash)
  allocator_free(a, p4, 150);

  arena_deinit(&arena);
}
