#ifndef CORE_ALLOCATOR_H
#define CORE_ALLOCATOR_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// OOM Policy: Out-of-memory handling and recovery is not a goal in this
// project. Implementations of struct allocator must log to stderr and
// abort/crash on OOM. Callers do not need to check for nullptr pointers on
// allocation success.

// ─── Allocator interface ───────────────────────────────────────────────────
//
// An struct allocator is a pluggable memory allocation strategy.  It exposes
// three callbacks (alloc, realloc, dealloc) plus inline bump-allocation
// state for arena-style allocators.
//
// Fast-path (arena):
//   When beg is non-nullptr, the static inline helpers in this header will
//   attempt a bump allocation directly from [beg, end) without calling
//   the callback.  This is the hot path for arena allocators.
//
// Slow-path (always):
//   If the inline bump fails (or beg is nullptr), the appropriate callback
//   is invoked.  For c_allocator (malloc/free), beg is always nullptr so
//   every operation goes through the callbacks.
//
// Zero-initialization:
//   Functions named alloc_* (without _uninitialized) guarantee that the
//   returned memory is zero-filled.  The _uninitialized variants return
//   uninitialized memory for performance.
typedef struct allocator {
  // Allocate a new block of at least `size` bytes with the given
  // `alignment` (must be a power of two).
  // Returns a pointer to the block, or aborts on OOM.
  void* (*alloc)(struct allocator* self, size_t size, size_t alignment);

  // Resize an existing block.  If `ptr` is nullptr this is equivalent to
  // alloc().  If `new_size` is 0 this is equivalent to dealloc().
  // The allocator may grow/shrink in-place or allocate a new block
  // and copy.  Returns the new pointer (may equal `ptr`), or aborts
  // on OOM.
  void* (*realloc)(struct allocator* self, void* ptr, size_t old_size,
                   size_t new_size, size_t alignment);

  // Free a block previously returned by alloc() or realloc().
  // `size` must match the allocation size; `alignment` the original
  // alignment.  For arena allocators this is a no-op unless the block
  // is at the top of the stack (handled inline).
  void (*dealloc)(struct allocator* self, void* ptr, size_t size,
                  size_t alignment);

  // ── Allocator kind (mutually exclusive) ─────────────────────────────
  //
  // An allocator is exactly one of three kinds:
  //
  //   Bump allocator (arena):  beg != nullptr,  page_size == 0
  //     beg / end bracket free space.  The inline helpers bump beg
  //     forward on allocations.  When exhausted the callback fires.
  //
  //   Page allocator:           beg == nullptr,  page_size != 0
  //     Every allocation goes through the callback.  The allocator
  //     returns page-aligned memory in page_size multiples.
  //
  //   Heap allocator:           beg == nullptr,  page_size == 0
  //     Every allocation goes through the callback with no special
  //     guarantees.
  //
  // beg and page_size are never both non-zero at the same time.
  char* beg;         // Start of free region (arena fast-path)
  char* end;         // One past the last usable byte (arena fast-path)
  size_t page_size;  // Page size for page allocators; 0 otherwise
} allocator_t;

// ─── Leaf functions (no dependencies on other static helpers) ─────────────

// Allocates size bytes with alignment without zero-initializing the memory.
// Includes an inline bump-allocation fast-path: if a->beg is non-nullptr
// (arena), we try to allocate from the current chunk directly without calling
// the callback. If the chunk is full, falls through to a->alloc().
static inline void* allocator_alloc_align_uninitialized(allocator_t* a,
                                                        size_t size,
                                                        size_t alignment) {
  if (a->beg != nullptr) {
    // Alignment must be a power of two and non-zero.
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
      return a->alloc(a, size, alignment);
    }
    uintptr_t addr = (uintptr_t)a->beg;
    uintptr_t mask = (uintptr_t)(alignment - 1);
    size_t padding = (uintptr_t)(-addr) & mask;
    size_t total = padding + size;
    if (total >= size && (size_t)(a->end - a->beg) >= total) {
      void* p = a->beg + padding;
      a->beg += total;
      return p;
    }
  }
  return a->alloc(a, size, alignment);
}

// Frees ptr of given size with alignment. For arena allocators, only
// top-of-stack frees are effective; others are silently ignored.
static inline void allocator_free_align(allocator_t* a, void* ptr, size_t size,
                                        size_t alignment) {
  if (a->beg != nullptr) {
    if (ptr && (char*)ptr + size == a->beg) {
      a->beg = (char*)ptr;
    }
    return;
  }
  a->dealloc(a, ptr, size, alignment);
}

// ─── Realloc (depends on alloc and free above) ──────────────────────────────

// Reallocates ptr to new_size without zero-initializing the newly allocated
// memory. Includes inline fast-paths for top-of-stack grow and shrink on
// arena allocators.
static inline void* allocator_realloc_align_uninitialized(allocator_t* a,
                                                          void* ptr,
                                                          size_t old_size,
                                                          size_t new_size,
                                                          size_t alignment) {
  if (a->beg != nullptr) {
    if (ptr == nullptr) {
      return allocator_alloc_align_uninitialized(a, new_size, alignment);
    }
    if (new_size == 0) {
      allocator_free_align(a, ptr, old_size, alignment);
      return nullptr;
    }
    // Top-of-stack: shrink
    if (new_size <= old_size && (char*)ptr + old_size == a->beg) {
      a->beg = (char*)ptr + new_size;
      return ptr;
    }
    // Top-of-stack: grow (if it fits in current chunk)
    if (new_size > old_size && (char*)ptr + old_size == a->beg) {
      size_t grow = new_size - old_size;
      if (grow <= (size_t)(a->end - a->beg)) {
        a->beg += grow;
        return ptr;
      }
    }
  }
  // Fallback: delegate to the allocator callback (non-top realloc,
  // or top-of-stack grow beyond current chunk).
  return a->realloc(a, ptr, old_size, new_size, alignment);
}

// ─── Zero-initializing wrappers (depend on functions above) ─────────────────

// Allocates size bytes with alignment. The allocated memory is
// zero-initialized.
static inline void* allocator_alloc_align(allocator_t* a, size_t size,
                                          size_t alignment) {
  void* ptr = allocator_alloc_align_uninitialized(a, size, alignment);
  memset(ptr, 0, size);
  return ptr;
}

// Reallocates ptr to new_size. If new_size > old_size, the newly allocated
// trailing memory is zero-initialized.
static inline void* allocator_realloc_align(allocator_t* a, void* ptr,
                                            size_t old_size, size_t new_size,
                                            size_t alignment) {
  void* new_ptr = allocator_realloc_align_uninitialized(a, ptr, old_size,
                                                        new_size, alignment);
  if (new_ptr && new_size > old_size) {
    memset((char*)new_ptr + old_size, 0, new_size - old_size);
  }
  return new_ptr;
}

// ─── Convenience wrappers (default alignment) ───────────────────────────────

// Default alignment helper (using 8 bytes as default alignment)
#ifndef DEFAULT_ALIGNMENT
#define DEFAULT_ALIGNMENT 8
#endif

// Allocates size bytes. The allocated memory is zero-initialized by default.
static inline void* allocator_alloc(allocator_t* a, size_t size) {
  return allocator_alloc_align(a, size, DEFAULT_ALIGNMENT);
}

// Allocates size bytes without zero-initializing.
static inline void* allocator_alloc_uninitialized(allocator_t* a, size_t size) {
  return allocator_alloc_align_uninitialized(a, size, DEFAULT_ALIGNMENT);
}

// Reallocates ptr to new_size. The newly allocated trailing memory is
// zero-initialized.
static inline void* allocator_realloc(allocator_t* a, void* ptr,
                                      size_t old_size, size_t new_size) {
  return allocator_realloc_align(a, ptr, old_size, new_size, DEFAULT_ALIGNMENT);
}

// Reallocates ptr to new_size without zero-initializing.
static inline void* allocator_realloc_uninitialized(allocator_t* a, void* ptr,
                                                    size_t old_size,
                                                    size_t new_size) {
  return allocator_realloc_align_uninitialized(a, ptr, old_size, new_size,
                                               DEFAULT_ALIGNMENT);
}

static inline void allocator_free(allocator_t* a, void* ptr, size_t size) {
  allocator_free_align(a, ptr, size, DEFAULT_ALIGNMENT);
}

// ─── Type-safe allocation/deletion macros ───────────────────────────────────

#define allocator_alloc_struct(allocator, type) \
  ((type*)allocator_alloc_align((allocator), sizeof(type), alignof(type)))

#define allocator_free_struct(allocator, ptr, type) \
  allocator_free((allocator), (ptr), sizeof(type))

// Array helpers
#define allocator_alloc_array(allocator, type, count)                        \
  ((type*)allocator_alloc_align((allocator), sizeof(type) * (size_t)(count), \
                                alignof(type)))

#define allocator_alloc_array_uninitialized(allocator, type, count) \
  ((type*)allocator_alloc_align_uninitialized(                      \
      (allocator), sizeof(type) * (size_t)(count), alignof(type)))

#define allocator_realloc_array(allocator, type, ptr, old_count, new_count) \
  ((type*)allocator_realloc((allocator), (ptr),                             \
                            sizeof(type) * (size_t)(old_count),             \
                            sizeof(type) * (size_t)(new_count)))

#define allocator_realloc_array_uninitialized(allocator, type, ptr, old_count, \
                                              new_count)                       \
  ((type*)allocator_realloc_uninitialized((allocator), (ptr),                  \
                                          sizeof(type) * (size_t)(old_count),  \
                                          sizeof(type) * (size_t)(new_count)))

#define allocator_free_array(allocator, ptr, type, count) \
  allocator_free((allocator), (ptr), sizeof(type) * (size_t)(count))

// The standard malloc/free based allocator.
allocator_t* c_allocator(void);

// A page allocator.  Returns page-aligned memory in page-size
// multiples.  Obtain via page_allocator() — the function initialises
// page_size on first call (thread-safe).
allocator_t* page_allocator(void);

#ifdef __cplusplus
}
#endif

#endif  // CORE_ALLOCATOR_H
