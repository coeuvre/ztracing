#define _GNU_SOURCE

#include "core/allocator.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#ifdef __APPLE__
#define MREMAP_MAYMOVE 1
static void* mremap(void* old_address, size_t old_size, size_t new_size,
                    int flags) {
  (void)flags;
  if (new_size == old_size) {
    return old_address;
  }
  if (new_size < old_size) {
    munmap((char*)old_address + new_size, old_size - new_size);
    return old_address;
  }
  void* new_address = mmap(nullptr, new_size, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (new_address == MAP_FAILED) {
    return MAP_FAILED;
  }
  memcpy(new_address, old_address, old_size);
  munmap(old_address, old_size);
  return new_address;
}
#endif

static void* c_alloc(allocator_t* self, size_t size, size_t alignment) {
  (void)self;
  (void)alignment;
  void* result = malloc(size);
  if (!result) {
    fprintf(stderr, "OOM: failed to allocate %zu bytes\n", size);
    abort();
  }
  return result;
}

static void* c_realloc(allocator_t* self, void* ptr, size_t old_size,
                       size_t new_size, size_t alignment) {
  (void)self;
  (void)old_size;
  (void)alignment;
  if (ptr == nullptr) {
    return c_alloc(self, new_size, alignment);
  }
  if (new_size == 0) {
    free(ptr);
    return nullptr;
  }
  void* result = realloc(ptr, new_size);
  if (!result) {
    fprintf(stderr, "OOM: failed to realloc %zu bytes\n", new_size);
    abort();
  }
  return result;
}

static void c_free(allocator_t* self, void* ptr, size_t old_size,
                   size_t alignment) {
  (void)self;
  (void)old_size;
  (void)alignment;
  free(ptr);
}

static allocator_t g_c_allocator = {.alloc = c_alloc,
                                    .realloc = c_realloc,
                                    .dealloc = c_free,
                                    .beg = nullptr,
                                    .end = nullptr,
                                    .page_size = 0};

allocator_t* c_allocator(void) { return &g_c_allocator; }

// ─── page allocator ─────────────────────────────────────────────────────────

static void page_free(allocator_t* self, void* ptr, size_t size,
                      size_t alignment);

static void* page_alloc(allocator_t* self, size_t size, size_t alignment) {
  if (alignment != 0 && self->page_size % alignment != 0) {
    fprintf(stderr,
            "page allocator: alignment %zu not compatible with page size %zu\n",
            alignment, self->page_size);
    abort();
  }
  size_t ps = self->page_size;
  size_t total = (size + ps - 1) & ~(ps - 1);
  void* mem = mmap(nullptr, total, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mem == MAP_FAILED) {
    fprintf(stderr, "OOM: page allocator mmap %zu bytes\n", total);
    abort();
  }
  return mem;
}

static void* page_realloc(allocator_t* self, void* ptr, size_t old_size,
                          size_t new_size, size_t alignment) {
  if (alignment != 0 && self->page_size % alignment != 0) {
    fprintf(stderr,
            "page allocator: alignment %zu not compatible with page size %zu\n",
            alignment, self->page_size);
    abort();
  }
  size_t ps = self->page_size;
  if (ptr == nullptr) {
    return page_alloc(self, new_size, alignment);
  }
  if (new_size == 0) {
    page_free(self, ptr, old_size, alignment);
    return nullptr;
  }
  size_t old_total = (old_size + ps - 1) & ~(ps - 1);
  size_t new_total = (new_size + ps - 1) & ~(ps - 1);
  void* result = mremap(ptr, old_total, new_total, MREMAP_MAYMOVE);
  if (result == MAP_FAILED) {
    fprintf(stderr, "OOM: page allocator mremap %zu -> %zu bytes\n", old_total,
            new_total);
    abort();
  }
  return result;
}

static void page_free(allocator_t* self, void* ptr, size_t size,
                      size_t alignment) {
  (void)self;
  (void)alignment;
  size_t ps = self->page_size;
  size_t total = (size + ps - 1) & ~(ps - 1);
  munmap(ptr, total);
}

static allocator_t g_page_allocator;
static pthread_once_t g_page_allocator_once = PTHREAD_ONCE_INIT;

static void page_allocator_init(void) {
  size_t ps = (size_t)sysconf(_SC_PAGESIZE);
  if (ps == 0 || (ps & (ps - 1)) != 0) {
    fprintf(stderr, "page allocator: page_size %zu is not a power of two\n",
            ps);
    abort();
  }
  g_page_allocator.alloc = page_alloc;
  g_page_allocator.realloc = page_realloc;
  g_page_allocator.dealloc = page_free;
  g_page_allocator.beg = nullptr;
  g_page_allocator.end = nullptr;
  g_page_allocator.page_size = ps;
}

allocator_t* page_allocator(void) {
  pthread_once(&g_page_allocator_once, page_allocator_init);
  return &g_page_allocator;
}
