#include "core/arena.h"

#include <gtest/gtest.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <vector>

// ─── lifecycle ──────────────────────────────────────────────────────────────

TEST(arena_test, create_destroy) {
  arena_t* a = arena_create();
  ASSERT_NE(a, nullptr);
  arena_destroy(a);
}

TEST(arena_test, reset) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  allocator_alloc(alloc, 4096);
  arena_reset(a);

  // After reset, allocating works again.
  void* p = allocator_alloc(alloc, 4096);
  ASSERT_NE(p, nullptr);

  arena_destroy(a);
}

// ─── basic alloc / free (top of stack) ──────────────────────────────────────

TEST(arena_test, alloc_free_top_of_stack) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  char* p1 = (char*)allocator_alloc(alloc, 64);
  char* p2 = (char*)allocator_alloc(alloc, 128);
  ASSERT_NE(p1, nullptr);
  ASSERT_NE(p2, nullptr);
  EXPECT_NE(p1, p2);

  allocator_free(alloc, p2, 128);

  char* p3 = (char*)allocator_alloc(alloc, 128);
  EXPECT_EQ(p2, p3);

  allocator_free(alloc, p3, 128);
  allocator_free(alloc, p1, 64);

  char* p4 = (char*)allocator_alloc(alloc, 64);
  EXPECT_EQ(p1, p4);

  arena_destroy(a);
}

TEST(arena_test, free_non_top_is_no_op) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  char* p1 = (char*)allocator_alloc(alloc, 64);
  char* p2 = (char*)allocator_alloc(alloc, 128);

  allocator_free(alloc, p1, 64);

  char* p3 = (char*)allocator_alloc(alloc, 32);
  EXPECT_GT(p3, p2);

  arena_destroy(a);
}

// ─── realloc ────────────────────────────────────────────────────────────────

TEST(arena_test, realloc_shrink_top) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  char* p1 = (char*)allocator_alloc(alloc, 256);
  char* p2 = (char*)allocator_realloc(alloc, p1, 256, 128);
  EXPECT_EQ(p1, p2);

  char* p3 = (char*)allocator_alloc(alloc, 32);
  EXPECT_EQ(p1 + 128, p3);

  arena_destroy(a);
}

TEST(arena_test, realloc_grow_top) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  char* p1 = (char*)allocator_alloc(alloc, 64);
  char* p2 = (char*)allocator_realloc(alloc, p1, 64, 128);
  EXPECT_EQ(p1, p2);

  memset(p1, 0xAB, 128);
  char* p3 = (char*)allocator_alloc(alloc, 32);
  EXPECT_EQ(p1 + 128, p3);

  arena_destroy(a);
}

TEST(arena_test, realloc_non_top_fallback) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  char* p1 = (char*)allocator_alloc(alloc, 64);
  memset(p1, 0xAA, 64);
  char* p2 = (char*)allocator_alloc(alloc, 64);

  char* p3 = (char*)allocator_realloc(alloc, p1, 64, 128);
  ASSERT_NE(p3, nullptr);
  EXPECT_NE(p3, p1);
  EXPECT_EQ(memcmp(p3, p1, 64), 0);
  EXPECT_GE(p3, p2 + 64);
  EXPECT_EQ(p1[0], (char)0xAA);

  arena_destroy(a);
}

// ─── chunk growth ───────────────────────────────────────────────────────────

TEST(arena_test, grows_into_new_chunks) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  char* p1 = (char*)allocator_alloc(alloc, 3000);
  char* p2 = (char*)allocator_alloc(alloc, 3000);
  ASSERT_NE(p1, nullptr);
  ASSERT_NE(p2, nullptr);

  memset(p1, 0xCC, 3000);
  memset(p2, 0xDD, 3000);
  EXPECT_EQ(p1[0], (char)0xCC);
  EXPECT_EQ(p2[0], (char)0xDD);

  arena_destroy(a);
}

TEST(arena_test, many_allocs_unique) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  void* ptrs[100];
  for (int i = 0; i < 100; i++) {
    ptrs[i] = allocator_alloc(alloc, 32);
    ASSERT_NE(ptrs[i], nullptr);
  }
  for (int i = 0; i < 100; i++) {
    for (int j = i + 1; j < 100; j++) {
      EXPECT_NE(ptrs[i], ptrs[j]);
    }
  }

  arena_destroy(a);
}

// ─── alignment ──────────────────────────────────────────────────────────────

TEST(arena_test, alignment_compliance) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  void* p1 = allocator_alloc(alloc, 3);
  void* p2 = allocator_alloc(alloc, 7);
  void* p3 = allocator_alloc(alloc, 1);

  EXPECT_EQ((uintptr_t)p1 % 8, (uintptr_t)0);
  EXPECT_EQ((uintptr_t)p2 % 8, (uintptr_t)0);
  EXPECT_EQ((uintptr_t)p3 % 8, (uintptr_t)0);

  arena_destroy(a);
}

TEST(arena_test, alignment_large_values) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  void* p1 = allocator_alloc_align(alloc, 16, 16);
  void* p2 = allocator_alloc_align(alloc, 16, 32);
  void* p3 = allocator_alloc_align(alloc, 16, 64);

  EXPECT_EQ((uintptr_t)p1 % 16, (uintptr_t)0);
  EXPECT_EQ((uintptr_t)p2 % 32, (uintptr_t)0);
  EXPECT_EQ((uintptr_t)p3 % 64, (uintptr_t)0);

  arena_destroy(a);
}

// ─── alloc / zeroing ────────────────────────────────────────────────────────

TEST(arena_test, allocator_alloc_zeroes) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  char* p = (char*)allocator_alloc(alloc, 128);
  ASSERT_NE(p, nullptr);
  for (int i = 0; i < 128; i++) {
    EXPECT_EQ(p[i], 0);
  }

  arena_destroy(a);
}

TEST(arena_test, allocator_realloc_trailing_zero) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  char* p1 = (char*)allocator_alloc_uninitialized(alloc, 64);
  memset(p1, 0xFF, 64);

  char* p2 = (char*)allocator_realloc(alloc, p1, 64, 128);
  ASSERT_NE(p2, nullptr);
  for (int i = 0; i < 64; i++) {
    EXPECT_EQ((unsigned char)p2[i], 0xFF);
  }
  for (int i = 64; i < 128; i++) {
    EXPECT_EQ((unsigned char)p2[i], 0x00);
  }

  arena_destroy(a);
}

// ─── repeated alloc/free cycle ──────────────────────────────────────────────

TEST(arena_test, repeated_alloc_free_cycle) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  for (int round = 0; round < 100; round++) {
    char* p1 = (char*)allocator_alloc(alloc, 128);
    char* p2 = (char*)allocator_alloc(alloc, 256);
    memset(p1, 'A', 128);
    memset(p2, 'B', 256);

    allocator_free(alloc, p2, 256);
    allocator_free(alloc, p1, 128);

    char* p1b = (char*)allocator_alloc(alloc, 128);
    char* p2b = (char*)allocator_alloc(alloc, 256);
    EXPECT_EQ(p1, p1b);
    EXPECT_EQ(p2, p2b);

    allocator_free(alloc, p2b, 256);
    allocator_free(alloc, p1b, 128);
  }

  arena_destroy(a);
}

// ─── macros ─────────────────────────────────────────────────────────────────

TEST(arena_test, allocator_alloc_struct) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  typedef struct {
    int value;
    double weight;
  } test_obj;

  test_obj* obj = allocator_alloc_struct(alloc, test_obj);
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(obj->value, 0);
  EXPECT_EQ(obj->weight, 0.0);

  arena_destroy(a);
}

TEST(arena_test, allocator_alloc_array) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  int* arr = allocator_alloc_array(alloc, int, 10);
  ASSERT_NE(arr, nullptr);
  for (int i = 0; i < 10; i++) {
    EXPECT_EQ(arr[i], 0);
  }

  arena_destroy(a);
}

// ─── chunk boundary ─────────────────────────────────────────────────────────

TEST(arena_test, chunk_end_is_correct) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  size_t cap = arena_get_capacity(a);
  ASSERT_GT(cap, (size_t)0);

  char* p1 = (char*)allocator_alloc_uninitialized(alloc, cap);
  ASSERT_NE(p1, nullptr);
  p1[0] = (char)0xAA;
  p1[cap - 1] = (char)0xBB;
  EXPECT_EQ(p1[0], (char)0xAA);
  EXPECT_EQ(p1[cap - 1], (char)0xBB);

  char* p2 = (char*)allocator_alloc(alloc, 4096);
  ASSERT_NE(p2, nullptr);
  p2[0] = (char)0xCC;
  p2[4095] = (char)0xDD;
  EXPECT_EQ(p2[0], (char)0xCC);

  arena_destroy(a);
}

// ─── get_capacity ───────────────────────────────────────────────────────────

TEST(arena_test, arena_get_capacity) {
  arena_t* a = arena_create();

  size_t cap1 = arena_get_capacity(a);
  EXPECT_GT(cap1, (size_t)0);

  allocator_t* alloc = arena_get_allocator(a);
  allocator_alloc(alloc, 1024);

  size_t cap2 = arena_get_capacity(a);
  EXPECT_LT(cap2, cap1);

  arena_destroy(a);
}

TEST(arena_test, reset_frees_extra_chunks) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  // Allocate enough to trigger multiple new chunks
  void* p1 = allocator_alloc(alloc, 100000);
  void* p2 = allocator_alloc(alloc, 200000);
  ASSERT_NE(p1, nullptr);
  ASSERT_NE(p2, nullptr);

  // Inspect internal chunk count before reset
  int chunk_count_before = 0;
  for (arena_chunk_t* c = arena_get_first_chunk(a); c != nullptr; c = c->next) {
    chunk_count_before++;
  }
  EXPECT_GT(chunk_count_before, 1);

  // Reset unmaps old chunks and consolidates into one large chunk.
  arena_reset(a);

  // After reset: embedded chunk + one consolidated chunk.
  int chunk_count_after = 0;
  for (arena_chunk_t* c = arena_get_first_chunk(a); c != nullptr; c = c->next) {
    chunk_count_after++;
  }
  EXPECT_LE(chunk_count_after, 2);

  arena_destroy(a);
}

TEST(arena_test, invalid_alignment_aborts) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  // 0 alignment is invalid
  EXPECT_DEATH(allocator_alloc_align(alloc, 16, 0), ".*");
  // Non-power-of-two alignment is invalid
  EXPECT_DEATH(allocator_alloc_align(alloc, 16, 3), ".*");
  EXPECT_DEATH(allocator_alloc_align(alloc, 16, 7), ".*");

  arena_destroy(a);
}

TEST(arena_test, size_overflow_aborts) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  // Large size causing overflow during chunk creation should abort
  EXPECT_DEATH(allocator_alloc(alloc, SIZE_MAX - 7), ".*");

  arena_destroy(a);
}

TEST(arena_test, mmap_utilizes_full_page) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  // Fill up the first chunk completely
  size_t cap = arena_get_capacity(a);
  void* p1 = allocator_alloc_uninitialized(alloc, cap);
  ASSERT_NE(p1, nullptr);

  // Trigger allocation of a new chunk by requesting 1 byte
  void* p2 = allocator_alloc_uninitialized(alloc, 1);
  ASSERT_NE(p2, nullptr);

  // Inspect the current chunk (which is the new chunk)
  // The capacity of the new chunk should be page-aligned minus the header size.
  size_t chunk_size = a->current->size;

  size_t ps = (size_t)sysconf(_SC_PAGESIZE);
  size_t total_chunk_size = chunk_size + sizeof(arena_chunk_t);
  EXPECT_EQ(total_chunk_size % ps, (size_t)0);
  EXPECT_GT(chunk_size, (size_t)(64 * 1024));

  arena_destroy(a);
}

// ─── chunk integrity across growth / reset ─────────────────────────────────

// Regression: with the old prepend design, after exhausting the second
// chunk arena_alloc would advance to the first (full) chunk, reset beg to
// its data start, and allocate there — overwriting live data.
TEST(arena_test, old_chunks_not_overwritten_on_growth) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  // Fill the first chunk completely.
  size_t cap1 = arena_get_capacity(a);
  char* p1 = (char*)allocator_alloc_uninitialized(alloc, cap1);
  ASSERT_NE(p1, nullptr);
  memset(p1, 0xAA, cap1);

  // Exhaust it with an aligned size so the next chunk stays aligned.
  allocator_alloc_uninitialized(alloc, 8);

  // Fill most of the second chunk, leaving exactly 8 bytes free.
  size_t cap2 = arena_get_capacity(a);
  ASSERT_GT(cap2, (size_t)16);
  allocator_alloc_uninitialized(alloc, cap2 - 8);

  // Exhaust the last 8 bytes — now the second chunk is full.
  allocator_alloc_uninitialized(alloc, 8);

  // Try to allocate 1 more byte.  The second chunk is full so the bump
  // allocator fails.  With the old prepend layout, arena_alloc would
  // advance to the first chunk (current->next != NULL), reset beg to
  // first_chunk->data, and return a pointer inside the first chunk.
  char* p_corrupt = (char*)allocator_alloc_uninitialized(alloc, 1);
  ASSERT_NE(p_corrupt, nullptr);
  *p_corrupt = (char)0x33;

  // p1 must still be intact — its 0xAA must not have been overwritten.
  EXPECT_EQ(p1[0], (char)0xAA);
  EXPECT_EQ(p1[cap1 - 1], (char)0xAA);

  // The new allocation must NOT lie inside the first chunk.
  EXPECT_FALSE(p_corrupt >= p1 && p_corrupt < p1 + cap1);

  arena_destroy(a);
}

// After reset, the pre-existing chunks are free and should be reused
// in order.
TEST(arena_test, reset_reuses_chunks_in_order) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  // Allocate enough to create a second chunk.
  size_t cap1 = arena_get_capacity(a);
  char* p1 = (char*)allocator_alloc_uninitialized(alloc, cap1);
  ASSERT_NE(p1, nullptr);
  memset(p1, 0xAA, cap1);

  char* p2 = (char*)allocator_alloc_uninitialized(alloc, 1);
  ASSERT_NE(p2, nullptr);
  *p2 = (char)0x11;

  // p2 must come from a second chunk (not the first).
  EXPECT_FALSE(p2 >= p1 && p2 < p1 + cap1);

  // Reset — all chunks become free, cursor back at the first chunk.
  arena_reset(a);

  // Fill the first chunk again.
  size_t cap_after = arena_get_capacity(a);
  char* p3 = (char*)allocator_alloc_uninitialized(alloc, cap_after);
  ASSERT_NE(p3, nullptr);
  memset(p3, 0xCC, cap_after);

  // The next allocation should advance into the free second chunk.
  char* p4 = (char*)allocator_alloc_uninitialized(alloc, 1);
  ASSERT_NE(p4, nullptr);

  // p4 must not be inside the first chunk (it should be in a reused chunk).
  EXPECT_FALSE(p4 >= p1 && p4 < p1 + cap1);

  arena_destroy(a);
}

TEST(arena_test, peak_tracks_total_allocated_memory) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);
  size_t ps = (size_t)sysconf(_SC_PAGESIZE);

  // Peak starts at the first-chunk allocation size (arena + chunk + data,
  // page-aligned).  First chunk data is at least ARENA_MIN_CHUNK (64 KB).
  size_t initial_peak = a->peak;
  EXPECT_GE(initial_peak, (size_t)(64 * 1024));
  EXPECT_EQ(initial_peak % ps, (size_t)0);

  // Trigger a new chunk allocation.
  size_t cap = arena_get_capacity(a);
  void* p1 = allocator_alloc_uninitialized(alloc, cap);
  ASSERT_NE(p1, nullptr);
  void* p2 = allocator_alloc_uninitialized(alloc, 1);
  ASSERT_NE(p2, nullptr);

  // Peak now includes the new chunk (header + data, page-aligned).
  EXPECT_GT(a->peak, initial_peak);
  EXPECT_EQ(a->peak % ps, (size_t)0);

  // Allocating more without triggering a new chunk should not change peak.
  size_t peak_before = a->peak;
  void* p3 = allocator_alloc_uninitialized(alloc, 64);
  ASSERT_NE(p3, nullptr);
  EXPECT_EQ(a->peak, peak_before);

  // Exhaust the current chunk and all pre-existing chunks to force a
  // truly new chunk.
  size_t cap2 = arena_get_capacity(a);
  void* p4 = allocator_alloc_uninitialized(alloc, cap2);
  ASSERT_NE(p4, nullptr);
  // This reuses the embedded chunk (was filled in step 1, cursor reset).
  void* p5 = allocator_alloc_uninitialized(alloc, 1);
  ASSERT_NE(p5, nullptr);
  // Fill the reused embedded chunk.
  size_t cap3 = arena_get_capacity(a);
  void* p6 = allocator_alloc_uninitialized(alloc, cap3);
  ASSERT_NE(p6, nullptr);
  // Now all pre-existing chunks are full — this creates a truly new chunk.
  void* p7 = allocator_alloc_uninitialized(alloc, 1);
  ASSERT_NE(p7, nullptr);
  EXPECT_GT(a->peak, peak_before);
  EXPECT_EQ(a->peak % ps, (size_t)0);

  // Reset unmaps old extra chunks and consolidates into one large chunk
  // sized to cover peak.
  size_t peak_before_reset = a->peak;
  arena_reset(a);
  EXPECT_GE(a->peak, peak_before_reset);
  EXPECT_EQ(a->peak % ps, (size_t)0);

  // After reset, the consolidated chunk has ample room.  Allocating should
  // not create new chunks until the consolidated chunk is exhausted.
  size_t peak_after_reset = a->peak;
  void* p8 = allocator_alloc_uninitialized(alloc, 64);
  ASSERT_NE(p8, nullptr);
  EXPECT_EQ(a->peak, peak_after_reset);

  arena_destroy(a);
}

// ─── checkpoint and restore tests ──────────────────────────────────────────

TEST(arena_test, checkpoint_basic) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  char* p1 = (char*)allocator_alloc(alloc, 64);
  ASSERT_NE(p1, nullptr);
  memset(p1, 0x11, 64);

  // Take checkpoint
  arena_checkpoint_t cp = arena_get_checkpoint(a);

  char* p2 = (char*)allocator_alloc(alloc, 128);
  ASSERT_NE(p2, nullptr);
  memset(p2, 0x22, 128);

  // Restore checkpoint
  arena_reset_to_checkpoint(a, cp);

  // Allocating again should reuse the same space
  char* p3 = (char*)allocator_alloc(alloc, 128);
  EXPECT_EQ(p2, p3);

  // The original allocation must be intact
  for (int i = 0; i < 64; i++) {
    EXPECT_EQ(p1[i], 0x11);
  }

  arena_destroy(a);
}

TEST(arena_test, checkpoint_multi_chunk) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  // Fill up most of the first chunk
  size_t cap1 = arena_get_capacity(a);
  char* p1 = (char*)allocator_alloc_uninitialized(alloc, cap1 - 8);
  ASSERT_NE(p1, nullptr);
  memset(p1, 0x33, cap1 - 8);

  // Take checkpoint
  arena_checkpoint_t cp = arena_get_checkpoint(a);

  // This allocation will exceed the first chunk and allocate a second chunk
  char* p2 = (char*)allocator_alloc_uninitialized(alloc, 100);
  ASSERT_NE(p2, nullptr);
  memset(p2, 0x44, 100);

  // Count chunks: should be 2
  int chunks_before = 0;
  for (arena_chunk_t* c = arena_get_first_chunk(a); c != nullptr; c = c->next) {
    chunks_before++;
  }
  EXPECT_EQ(chunks_before, 2);

  // Restore checkpoint
  arena_reset_to_checkpoint(a, cp);

  // Allocate 100 bytes again. It should advance to the already-allocated second
  // chunk and reuse the same memory pointer.
  char* p3 = (char*)allocator_alloc_uninitialized(alloc, 100);
  EXPECT_EQ(p2, p3);

  // Count chunks: should still be 2 (no new chunk was created, existing one was
  // reused)
  int chunks_after = 0;
  for (arena_chunk_t* c = arena_get_first_chunk(a); c != nullptr; c = c->next) {
    chunks_after++;
  }
  EXPECT_EQ(chunks_after, 2);

  arena_destroy(a);
}

TEST(arena_test, checkpoint_nested) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  char* p1 = (char*)allocator_alloc(alloc, 32);
  memset(p1, 0x55, 32);

  arena_checkpoint_t cp1 = arena_get_checkpoint(a);

  char* p2 = (char*)allocator_alloc(alloc, 32);
  memset(p2, 0x66, 32);

  arena_checkpoint_t cp2 = arena_get_checkpoint(a);

  char* p3 = (char*)allocator_alloc(alloc, 32);
  memset(p3, 0x77, 32);

  // Restore to nested checkpoint cp2
  arena_reset_to_checkpoint(a, cp2);
  char* p4 = (char*)allocator_alloc(alloc, 32);
  EXPECT_EQ(p3, p4);

  // Restore to outer checkpoint cp1
  arena_reset_to_checkpoint(a, cp1);
  char* p5 = (char*)allocator_alloc(alloc, 32);
  EXPECT_EQ(p2, p5);

  arena_destroy(a);
}

// ─── mock page allocator for backing-allocator tests ────────────────────────

struct mock_page_allocator {
  allocator_t super;
  std::vector<size_t> alloc_sizes;  // sizes passed to alloc()
};

static void* mock_page_alloc(allocator_t* self, size_t size, size_t alignment) {
  (void)alignment;
  auto* m = (mock_page_allocator*)self;
  m->alloc_sizes.push_back(size);
  void* p = malloc(size);
  if (!p) {
    abort();
  }
  return p;
}

static void* mock_page_realloc(allocator_t* self, void* ptr, size_t old_size,
                               size_t new_size, size_t alignment) {
  (void)old_size;
  (void)alignment;
  auto* m = (mock_page_allocator*)self;
  if (new_size > 0) {
    m->alloc_sizes.push_back(new_size);
  }
  if (ptr == NULL) {
    return mock_page_alloc(self, new_size, alignment);
  }
  if (new_size == 0) {
    free(ptr);
    return NULL;
  }
  void* p = realloc(ptr, new_size);
  if (!p) {
    abort();
  }
  return p;
}

static void mock_page_free(allocator_t* self, void* ptr, size_t size,
                           size_t alignment) {
  (void)self;
  (void)size;
  (void)alignment;
  free(ptr);
}

static void mock_page_allocator_init(mock_page_allocator* m, size_t page_size) {
  m->super.alloc = mock_page_alloc;
  m->super.realloc = mock_page_realloc;
  m->super.dealloc = mock_page_free;
  m->super.beg = NULL;
  m->super.end = NULL;
  m->super.page_size = page_size;
}

// ─── page-alignment of backing allocator requests ───────────────────────────

TEST(arena_test, backing_allocator_receives_aligned_sizes) {
  const size_t ps = 4096;
  const size_t min_chunk = (size_t)(64 * 1024);
  mock_page_allocator mock;
  mock_page_allocator_init(&mock, ps);

  arena_t* a = arena_create_with_allocator(&mock.super);
  allocator_t* alloc = arena_get_allocator(a);

  // Every allocation request must be a multiple of ARENA_MIN_CHUNK (and
  // therefore also page-aligned).
  auto all_page_aligned = [&]() {
    for (size_t sz : mock.alloc_sizes) {
      if (sz % ps != 0 || sz % min_chunk != 0) {
        return false;
      }
    }
    return true;
  };

  // The first-chunk allocation must be aligned.
  ASSERT_GE(mock.alloc_sizes.size(), 1u);
  EXPECT_EQ(mock.alloc_sizes[0] % min_chunk, (size_t)0);
  EXPECT_EQ(mock.alloc_sizes[0] % ps, (size_t)0);

  // Fill the first chunk so the next allocation triggers a new chunk.
  size_t cap = arena_get_capacity(a);
  allocator_alloc_uninitialized(alloc, cap);
  allocator_alloc_uninitialized(alloc, 1);  // triggers chunk_create

  // The second-chunk allocation must also be aligned.
  ASSERT_GE(mock.alloc_sizes.size(), 2u);
  EXPECT_EQ(mock.alloc_sizes[1] % min_chunk, (size_t)0);
  EXPECT_EQ(mock.alloc_sizes[1] % ps, (size_t)0);

  // Trigger a reset which may create a consolidated chunk, then force a
  // new chunk and verify alignment.
  arena_reset(a);
  cap = arena_get_capacity(a);
  allocator_alloc_uninitialized(alloc, cap);
  size_t sizes_before = mock.alloc_sizes.size();
  allocator_alloc_uninitialized(alloc, 1);  // trigger new chunk post-reset
  if (mock.alloc_sizes.size() > sizes_before) {
    EXPECT_EQ(mock.alloc_sizes[sizes_before] % min_chunk, (size_t)0);
    EXPECT_EQ(mock.alloc_sizes[sizes_before] % ps, (size_t)0);
  }

  // Everything must be aligned.
  EXPECT_TRUE(all_page_aligned());

  arena_destroy(a);
}
