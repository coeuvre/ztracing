#include "flick.h"

#define FL_LIST_ADD_FIRST(first, node, next) \
  do {                                       \
    (node)->next = first;                    \
    (first) = node;                          \
  } while (0)

#define FL_LIST_REMOVE_FIRST(first, next) \
  do {                                    \
    (first) = (first)->next;              \
  } while (0)

#define FL_DLIST_ADD_LAST(first, last, node, prev, next) \
  do {                                                   \
    if (last) {                                          \
      (node)->prev = (last);                             \
      (node)->next = 0;                                  \
      (last)->next = (node);                             \
      (last) = (node);                                   \
    } else {                                             \
      (first) = (last) = (node);                         \
      (node)->prev = (node)->next = 0;                   \
    }                                                    \
  } while (0)

#define FL_DLIST_REMOVE_FIRST(first, last, prev, next) \
  do {                                                 \
    (first) = (first)->next;                           \
    if (!(first)) {                                    \
      (last) = 0;                                      \
    }                                                  \
  } while (0)

#define FL_DLIST_REMOVE(first, last, node, prev, next) \
  do {                                                 \
    if ((first) == (node)) {                           \
      (first) = (node)->next;                          \
    }                                                  \
    if ((last) == (node)) {                            \
      (last) = (node)->prev;                           \
    }                                                  \
    if ((node)->next) {                                \
      (node)->next->prev = (node)->prev;               \
    }                                                  \
    if ((node)->prev) {                                \
      (node)->prev->next = (node)->next;               \
    }                                                  \
    (node)->prev = (node)->next = 0;                   \
  } while (0)

#define FL_DLIST_CONCAT(first1, last1, first2, last2, prev, next) \
  do {                                                            \
    if (last1) {                                                  \
      (last1)->next = first2;                                     \
      (last1) = last2;                                            \
    } else {                                                      \
      first1 = first2;                                            \
      last1 = last2;                                              \
    }                                                             \
  } while (0)


#define FL_HASH_TRIE(Name, Key, Key_Hash, Key_IsEqual, Key_Dup, Value)     \
  typedef struct Name Name;                                                \
  struct Name {                                                            \
    Name *slots[4];                                                        \
    Key key;                                                               \
    Value value;                                                           \
  };                                                                       \
                                                                           \
  static inline Value *Name##_Upsert(Name **t, Key key, FL_Arena *arena) { \
    for (uint64_t hash = Key_Hash(key); *t; hash <<= 2) {                  \
      if (Key_IsEqual(key, t[0]->key)) {                                   \
        return &t[0]->value;                                               \
      }                                                                    \
      t = t[0]->slots + (hash >> 62);                                      \
    }                                                                      \
                                                                           \
    if (arena) {                                                           \
      Name *slot = (Name *)FL_Arena_PushStruct(arena, Name);               \
      slot->key = Key_Dup(key, arena);                                     \
      *t = slot;                                                           \
      return &slot->value;                                                 \
    }                                                                      \
                                                                           \
    return 0;                                                              \
  }

#include <stdbool.h>
#include <stdint.h>


typedef struct FL_PointerEventResolver {
  FL_Widget *widget;
} FL_PointerEventResolver;

void FL_PointerEventResolver_Reset(FL_PointerEventResolver *resolver);

/** Represents an object participating in an arena. */
typedef struct FL_GestureArenaMember {
  void *ptr;
  FL_GestureArenaMemberOps *ops;
} FL_GestureArenaMember;

typedef struct FL_GestureArena FL_GestureArena;

typedef struct FL_GestureArenaEntry FL_GestureArenaEntry;
struct FL_GestureArenaEntry {
  FL_GestureArenaEntry *prev;
  FL_GestureArenaEntry *next;
  FL_GestureArena *arena;
  FL_GestureArenaMember member;
  bool active;
};

typedef struct FL_GestureArenaState FL_GestureArenaState;

struct FL_GestureArena {
  FL_GestureArena *prev;
  FL_GestureArena *next;

  FL_GestureArenaState *state;
  FL_GestureArenaEntry *first;
  FL_GestureArenaEntry *last;
  FL_GestureArenaEntry *eager_winner;

  FL_i32 pointer;
  bool open;
};

struct FL_GestureArenaState {
  FL_GestureArena *first_arena;
  FL_GestureArena *last_arena;
  FL_GestureArena *first_free_arena;
  FL_GestureArena *last_free_arena;

  FL_GestureArenaEntry *first_free_entry;
  FL_GestureArenaEntry *last_free_entry;
};

FL_GestureArena *FL_GestureArena_Open(FL_GestureArenaState *state,
                                      FL_i32 pointer, FL_Arena *arena);

/**
 * Prevents new members from entering the arena.
 */
void FL_GestureArena_Close(FL_GestureArena *arena);

FL_GestureArena *FL_GestureArena_Get(FL_GestureArenaState *state,
                                     FL_i32 pointer);

/**
 * Forces resolution of the arena, giving the win to the first member.
 *
 * Sweep is typically after all the other processing for a `FLPointerUpEvent`
 * have taken place. It ensures that multiple passive gestures do not cause a
 * stalemate that prevents the user from interacting with the app.
 *
 * Recognizers that wish to delay resolving an arena past `FLPointerUpEvent`
 * should call `FL_GestureArena_Hold` to delay sweep until
 * `FL_GestureArena_Release` is called.
 */
void FL_GestureArena_Sweep(FL_GestureArena *arena);

void FL_Widget_Unmount(FL_Widget *widget);

#include <stddef.h>
#include <stdint.h>


struct State;
typedef struct State State;

typedef struct Build {
  State *state;
  FL_Arena *arena;
  FL_isize index;
  FL_f32 delta_time;
  FL_f32 fast_animation_rate;
  FL_Widget *root;
} Build;

typedef struct FL_HitTestEntry FL_HitTestEntry;
struct FL_HitTestEntry {
  FL_HitTestEntry *prev;
  FL_HitTestEntry *next;

  FL_Widget *widget;

  /**
   * Describing how `FL_PointerEvent` delivered to this `FL_HitTestEntry` should
   * be transformed from the global coordinate space of the screen to the local
   * coordinate space of `widget`.
   */
  FL_Trans2 transform;

  FL_Vec2 local_position;
};

struct FL_HitTestContext {
  FL_Arena *arena;

  FL_HitTestEntry *first;
  FL_HitTestEntry *last;

  /** position in the screen coordinate space. */
  FL_Vec2 position;

  /** Describing how `position` was transformed to `local_position`. */
  FL_Trans2 transform;

  /**
   * Position relative to widget's local coordinate system. (0, 0) is the
   * top-left of the box.
   */
  FL_Vec2 local_position;
};

struct State {
  FL_Arena *arena;

  Build builds[2];
  FL_isize build_index;
  Build *curr_build;
  Build *last_build;
  FL_i32 next_context_id;
  FL_i32 next_notification_id;
  FL_Canvas canvas;

  // -- Input ------------------------------------------------------------------
  FL_Vec2 mouse_pos;
  FL_i32 next_pointer_event_id;
  FL_u32 down_button;
  FL_i32 down_pointer;
  FL_i32 next_down_pointer;
  FL_PointerEventResolver resolver;

  // hit test result for button down.
  FL_HitTestContext button_down_hit_test_context;
  // hit test result for mouse move
  FL_HitTestContext button_move_hit_test_contexts[2];
  FL_i32 button_move_hit_test_context_index;

  FL_GestureArenaState gesture_arena_state;
};

State *GetGlobalState(void);

#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>


#if FL_PLATFORM_WINDOWS

#ifndef _MEMORYAPI_H_
#define MEM_COMMIT 0x00001000
#define MEM_RESERVE 0x00002000
#define MEM_RELEASE 0x00008000
#define PAGE_READWRITE 0x04

__declspec(dllimport) void *__stdcall VirtualAlloc(void *addr, FL_usize size,
                                                   FL_u32 type, FL_u32 protect);
__declspec(dllimport) int __stdcall VirtualFree(void *lpAddress,
                                                FL_usize dwSize,
                                                FL_u32 dwFreeType);
#endif  // _MEMORYAPI_H_

static void *default_allocator_alloc(void *ctx, FL_isize size) {
  (void)ctx;
  return VirtualAlloc(0, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

static void default_alocator_free(void *ctx, void *ptr, FL_isize size) {
  (void)ctx;
  (void)size;

  VirtualFree(ptr, 0, MEM_RELEASE);
}

#else

static void *default_allocator_alloc(void *ctx, FL_isize size) {
  (void)ctx;

  return malloc(size);
}

static void default_alocator_free(void *ctx, void *ptr, FL_isize size) {
  (void)ctx;
  (void)size;

  free(ptr);
}
#endif  // FL_PLATFORM_WINDOWS

static FL_AllocatorOps default_allocator_ops = {
    .alloc = default_allocator_alloc,
    .free = default_alocator_free,
};

FL_Allocator FL_Allocator_GetDefault(void) {
  return (FL_Allocator){0, &default_allocator_ops};
}

typedef struct FL_ArenaState {
  FL_Allocator allocator;
  FL_isize page_size;
} FL_ArenaState;

static inline bool IsPowerOfTwo(FL_isize val) { return (val & (val - 1)) == 0; }

static FL_isize AlignBackward(FL_isize addr, FL_isize alignment) {
  FL_DEBUG_ASSERT(IsPowerOfTwo(alignment));
  return addr & ~(alignment - 1);
}

static FL_isize AlignForward(FL_isize addr, FL_isize alignment) {
  FL_DEBUG_ASSERT(IsPowerOfTwo(alignment));
  return AlignBackward(addr + (alignment - 1), alignment);
}

static bool IsAligned(FL_isize addr, FL_isize alignment) {
  return AlignBackward(addr, alignment) == addr;
}

static FL_MemoryBlock *FL_MemoryBlock_Create(FL_isize size,
                                             FL_ArenaState *state) {
  FL_isize block_size =
      AlignForward(size + sizeof(FL_MemoryBlock), state->page_size);
  char *ptr = FL_Allocator_Alloc(state->allocator, block_size);
  FL_MemoryBlock *block = (FL_MemoryBlock *)(ptr + block_size) - 1;
  FL_ASSERTF(block, "out of memory");
  FL_ASSERT(IsAligned((FL_isize)block, alignof(FL_MemoryBlock)));
  *block = (FL_MemoryBlock){
      .state = state,
      .begin = ptr,
  };
  return block;
}

static void FL_MemoryBlock_Destroy(FL_MemoryBlock *block) {
  FL_isize block_size = (char *)block - block->begin + sizeof(FL_MemoryBlock);
  FL_ArenaState *state = block->state;
  FL_Allocator_Free(state->allocator, block->begin, block_size);
}

FL_Arena *FL_Arena_Create(const FL_ArenaOptions *opts) {
  FL_ASSERTF(IsPowerOfTwo(opts->page_size), "page_size must be power of two");
  FL_ArenaState tmp_state = {
      .allocator = opts->allocator,
      .page_size = opts->page_size,
  };
  if (!tmp_state.allocator.ops) {
    tmp_state.allocator = FL_Allocator_GetDefault();
  }
  if (!tmp_state.page_size) {
    tmp_state.page_size = 4096;
  }

  FL_MemoryBlock *block = FL_MemoryBlock_Create(
      sizeof(FL_ArenaState) + sizeof(FL_Arena), &tmp_state);
  FL_Arena bootstrap = {
      .begin = block->begin,
      .end = (char *)block,
  };

  FL_ArenaState *state = FL_Arena_PushStruct(&bootstrap, FL_ArenaState);
  *state = tmp_state;
  block->state = state;

  FL_Arena *arena = FL_Arena_PushStruct(&bootstrap, FL_Arena);
  *arena = bootstrap;

  return arena;
}

void FL_Arena_Destroy(FL_Arena *arena) {
  FL_MemoryBlock *block = FL_Arena_GetMemoryBlock(arena);
  while (block->next) {
    block = block->next;
  }
  while (block) {
    FL_MemoryBlock *prev = block->prev;
    FL_MemoryBlock_Destroy(block);
    block = prev;
  }
}

void FL_Arena_Reset(FL_Arena *arena) {
  FL_MemoryBlock *block = FL_Arena_GetMemoryBlock(arena);
  while (block->prev) {
    block = block->prev;
  }
  arena->begin = block->begin;
  arena->end = (char *)block;

  // Keep the memory space for internal state. See FL_Arena_Create.
  FL_Arena_PushStruct(arena, FL_ArenaState);
  FL_Arena_PushStruct(arena, FL_Arena);
}

FL_MemoryBlock *FL_Arena_GetMemoryBlock(FL_Arena *arena) {
  return (FL_MemoryBlock *)arena->end;
}

FL_Allocator FL_Arena_GetAllocator(FL_Arena *arena) {
  FL_MemoryBlock *block = FL_Arena_GetMemoryBlock(arena);
  FL_ArenaState *state = block->state;
  return state->allocator;
}

void *FL_Arena_Push(FL_Arena *arena, FL_isize size, FL_isize alignment) {
  FL_ASSERT(size >= 0);
  char *addr = (char *)AlignForward((FL_isize)arena->begin, alignment);
  while ((addr + size) >= arena->end) {
    FL_MemoryBlock *block = FL_Arena_GetMemoryBlock(arena);
    FL_MemoryBlock *next = block->next;
    if (!next) {
      next = FL_MemoryBlock_Create(size, block->state);
      next->prev = block;
      block->next = next;
    }
    arena->begin = next->begin;
    arena->end = (char *)next;

    addr = (char *)AlignForward((FL_isize)arena->begin, alignment);
  }

  arena->begin = addr + size;

  return addr;
}

void *FL_Arena_Pop(FL_Arena *arena, FL_isize size) {
  FL_MemoryBlock *block = FL_Arena_GetMemoryBlock(arena);
  while (block) {
    char *new_begin = arena->begin - size;
    if (new_begin >= block->begin) {
      arena->begin = new_begin;
      return new_begin;
    }

    size -= arena->begin - block->begin;
    block = block->prev;
    FL_ASSERTF(block, "arena overflow");
    arena->end = (char *)block;
    arena->begin = arena->end;
  }
  FL_UNREACHABLE;
}

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>


FL_Str FL_Str_FormatV(FL_Arena *arena, const char *format, va_list ap) {
  FL_isize buf_len = 256;
  char *buf_ptr = FL_Arena_PushArray(arena, char, buf_len);

  va_list args;
  va_copy(args, ap);
  FL_isize str_len = vsnprintf(buf_ptr, buf_len, format, args);

  if (str_len <= buf_len) {
    // Free the unused part of the buffer.
    FL_Arena_Pop(arena, buf_len - str_len);
  } else {
    // The buffer was too small. We need to resize it and try again.
    FL_Arena_Pop(arena, buf_len);
    buf_len = str_len;
    buf_ptr = FL_Arena_PushArray(arena, char, buf_len);
    va_copy(args, ap);
    vsnprintf(buf_ptr, buf_len, format, args);
  }

  return (FL_Str){buf_ptr, str_len};
}

FL_Str FL_Str_Format(FL_Arena *arena, const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  FL_Str result = FL_Str_FormatV(arena, format, ap);
  va_end(ap);
  return result;
}

#include <stdint.h>


bool FL_PointerEventResolver_Register(struct FL_Widget *widget) {
  Build *build = widget->build;
  State *state = build->state;
  FL_PointerEventResolver *resolver = &state->resolver;
  if (!resolver->widget) {
    resolver->widget = widget;
    return true;
  }
  return false;
}

void FL_PointerEventResolver_Reset(FL_PointerEventResolver *resolver) {
  (*resolver) = (FL_PointerEventResolver){0};
}

static void fl_gesture_arena_free(FL_GestureArena *arena) {
  FL_GestureArenaState *state = arena->state;

  FL_DLIST_REMOVE(state->first_arena, state->last_arena, arena, prev, next);

  for (FL_GestureArenaEntry *entry = arena->first; entry; entry = entry->next) {
    entry->active = false;
  }
  FL_DLIST_CONCAT(state->first_free_entry, state->last_free_entry, arena->first,
                  arena->last, prev, next);

  FL_DLIST_ADD_LAST(state->first_free_arena, state->last_free_arena, arena,
                    prev, next);
}

static inline void fl_gesture_arena_member_accept(FL_GestureArenaMember member,
                                                  FL_i32 pointer) {
  member.ops->accept(member.ptr, pointer);
}

static inline void fl_gesture_arena_member_reject(FL_GestureArenaMember member,
                                                  FL_i32 pointer) {
  member.ops->reject(member.ptr, pointer);
}

static void FL_GestureArena_Resolve_by_default(FL_GestureArena *arena) {
  FL_ASSERT(!arena->open);
  FL_ASSERT(arena->first && arena->first == arena->last);

  fl_gesture_arena_member_accept(arena->first->member, arena->pointer);
  fl_gesture_arena_free(arena);
}

static void FL_GestureArena_Resolve_in_favor_of(FL_GestureArena *arena,
                                                FL_GestureArenaEntry *winner) {
  FL_ASSERT(!arena->open);
  FL_ASSERT(!arena->eager_winner || arena->eager_winner == winner);

  for (FL_GestureArenaEntry *entry = arena->first; entry; entry = entry->next) {
    if (entry != winner) {
      fl_gesture_arena_member_reject(entry->member, arena->pointer);
    }
  }
  fl_gesture_arena_member_accept(winner->member, arena->pointer);
  fl_gesture_arena_free(arena);
}

static void fl_gesture_arena_try_resolve(FL_GestureArena *arena) {
  if (!arena->first) {
    fl_gesture_arena_free(arena);
  } else if (arena->first == arena->last) {
    FL_GestureArena_Resolve_by_default(arena);
  } else if (arena->eager_winner) {
    FL_GestureArena_Resolve_in_favor_of(arena, arena->eager_winner);
  }
}

FL_GestureArena *FL_GestureArena_Open(FL_GestureArenaState *state,
                                      FL_i32 pointer, FL_Arena *arena) {
  FL_ASSERT(!FL_GestureArena_Get(state, pointer));

  FL_GestureArena *gesture_arena = state->first_free_arena;
  if (gesture_arena) {
    FL_DLIST_REMOVE_FIRST(state->first_free_arena, state->last_free_entry, prev,
                          next);
  } else {
    gesture_arena = FL_Arena_PushStruct(arena, FL_GestureArena);
  }

  *gesture_arena = (FL_GestureArena){
      .state = state,
      .pointer = pointer,
      .open = true,
  };

  FL_DLIST_ADD_LAST(state->first_arena, state->last_arena, gesture_arena, prev,
                    next);

  return gesture_arena;
}

void FL_GestureArena_Close(FL_GestureArena *arena) {
  arena->open = false;
  fl_gesture_arena_try_resolve(arena);
}

FL_GestureArena *FL_GestureArena_Get(FL_GestureArenaState *state,
                                     FL_i32 pointer) {
  for (FL_GestureArena *gesture_arena = state->first_arena; gesture_arena;
       gesture_arena = gesture_arena->next) {
    if (gesture_arena->pointer == pointer) {
      return gesture_arena;
    }
  }
  return 0;
}

void FL_GestureArena_Sweep(FL_GestureArena *arena) {
  FL_ASSERT(!arena->open);

  // TODO: check for hold

  FL_GestureArenaEntry *winner = arena->first;
  if (winner) {
    // First member wins.
    fl_gesture_arena_member_accept(winner->member, arena->pointer);

    // Reject all the other members.
    for (FL_GestureArenaEntry *entry = winner->next; entry;
         entry = entry->next) {
      fl_gesture_arena_member_reject(entry->member, arena->pointer);
    }
  }

  fl_gesture_arena_free(arena);
}

FL_GestureArenaEntry *FL_GestureArena_Add(FL_i32 pointer,
                                          FL_GestureArenaMemberOps *ops,
                                          void *ctx) {
  FL_GestureArenaMember member = {.ops = ops, .ptr = ctx};
  State *state = GetGlobalState();
  FL_GestureArenaState *gesture_arena_state = &state->gesture_arena_state;
  FL_GestureArena *arena = FL_GestureArena_Get(gesture_arena_state, pointer);
  FL_ASSERTF(arena && arena->open, "gesture arena is not open");

  FL_GestureArenaEntry *entry = gesture_arena_state->first_free_entry;
  if (entry) {
    FL_DLIST_REMOVE_FIRST(gesture_arena_state->first_free_entry,
                          gesture_arena_state->last_free_entry, prev, next);
  } else {
    entry = FL_Arena_PushStruct(state->arena, FL_GestureArenaEntry);
  }

  (*entry) = (FL_GestureArenaEntry){
      .arena = arena,
      .member = member,
      .active = true,
  };

  FL_DLIST_ADD_LAST(arena->first, arena->last, entry, prev, next);
  return entry;
}

void FL_GestureArena_Update(FL_GestureArenaEntry *entry, void *ctx) {
  FL_ASSERT(entry->active);
  entry->member.ptr = ctx;
}

static void fl_gesture_arena_free_entry(FL_GestureArena *arena,
                                        FL_GestureArenaEntry *entry) {
  FL_GestureArenaState *state = arena->state;

  entry->active = false;

  FL_DLIST_REMOVE(arena->first, arena->last, entry, prev, next);
  FL_DLIST_ADD_LAST(state->first_free_entry, state->last_free_entry, entry,
                    prev, next);
}

void FL_GestureArena_Resolve(FL_GestureArenaEntry *entry, bool accepted) {
  FL_GestureArena *arena = entry->arena;
  if (accepted) {
    FL_GestureArena_Resolve_in_favor_of(arena, entry);
  } else {
    fl_gesture_arena_member_reject(entry->member, arena->pointer);
    fl_gesture_arena_free_entry(arena, entry);
  }
}

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>


static bool fl_struct_size_is_equal(FL_StructSize a, FL_StructSize b) {
  return a.size == b.size && a.alignment == b.alignment;
}

FL_Widget *FL_Widget_Create_(const FL_WidgetClass *klass, FL_Key key,
                             const void *props, FL_StructSize props_size) {
  FL_ASSERTF(fl_struct_size_is_equal(klass->props_size, props_size),
             "%s: expected {size=%td, alignment=%td}, but got {size=%td, "
             "alignment=%td}",
             klass->name, klass->props_size.size, klass->props_size.alignment,
             props_size.size, props_size.alignment);
  State *state = GetGlobalState();
  Build *build = state->curr_build;
  FL_Widget *widget = FL_Arena_PushStruct(build->arena, FL_Widget);
  *widget = (FL_Widget){
      .klass = klass,
      .key = key,
      .build = build,
      .props = FL_Arena_Dup(build->arena, props, props_size.size,
                            props_size.alignment),
  };
  return widget;
}

static bool can_reuse_widget(FL_Widget *curr, FL_Widget *last) {
  return last && !last->link && last->klass == curr->klass &&
         FL_Key_IsEqual(last->key, curr->key);
}

void FL_Widget_Mount(FL_Widget *parent, FL_Widget *widget) {
  FL_ASSERT(!widget->first && !widget->last);
  FL_ASSERT(widget->child_count == 0);
  FL_ASSERT(!widget->parent);
  FL_ASSERT(!widget->state);

  Build *build = widget->build;

  FL_Widget *last = 0;
  if (parent) {
    if (parent->link) {
      // TODO: use key to find widget from last build
    }

    if (!last) {
      last = parent->last_child_of_link;
      if (!can_reuse_widget(widget, last)) {
        last = 0;
      }
    }

    if (parent->last_child_of_link) {
      parent->last_child_of_link = parent->last_child_of_link->next;
    }

    widget->parent = parent;
    FL_DLIST_ADD_LAST(parent->first, parent->last, widget, prev, next);
    parent->child_count++;
  } else {
    State *state = build->state;
    FL_Widget *last_root = state->last_build->root;
    if (can_reuse_widget(widget, last_root)) {
      last = last_root;
    }
    widget->parent = 0;
    widget->prev = widget->next = 0;
  }

  if (last) {
    last->link = widget;

    widget->link = last;
    widget->size = last->size;
    widget->offset = last->offset;
    widget->last_child_of_link = last->first;
  }

  FL_StructSize state_size = widget->klass->state_size;
  if (state_size.size > 0) {
    widget->state =
        FL_Arena_Push(build->arena, state_size.size, state_size.alignment);
    if (widget->link) {
      memcpy(widget->state, widget->link->state, state_size.size);
    } else {
      memset(widget->state, 0, state_size.size);
    }
  }

  if (widget->klass->mount) {
    widget->klass->mount(widget);
  }
}

void FL_Widget_Unmount(FL_Widget *widget) {
  for (FL_Widget *child = widget->first; child; child = child->next) {
    FL_Widget_Unmount(child);
  }

  if (widget->klass->unmount) {
    widget->klass->unmount(widget);
  }
}

static uint64_t FL_ContextID_Hash(FL_ContextID id) { return id; }

static bool FL_ContextID_IsEqual(FL_ContextID a, FL_ContextID b) {
  return a == b;
}

static FL_ContextID FL_ContextID_Dup(FL_ContextID id, FL_Arena *arena) {
  (void)arena;
  return id;
}

FL_HASH_TRIE(FL_Context, FL_ContextID, FL_ContextID_Hash, FL_ContextID_IsEqual,
             FL_ContextID_Dup, FL_ContextData)

void *FL_Widget_GetContext_(FL_Widget *widget, FL_ContextID id, FL_isize size) {
  FL_ContextData *data = FL_Context_Upsert(&widget->context, id, 0);
  if (data) {
    FL_ASSERTF(data->len == size,
               "%s: expected context value size %td, but got %td",
               widget->klass->name, data->len, size);
    return data->ptr;
  }
  return 0;
}

void *FL_Widget_SetContext_(FL_Widget *widget, FL_ContextID id,
                            FL_StructSize size) {
  FL_ASSERT(id != 0);
  Build *build = widget->build;
  FL_ContextData *data = FL_Context_Upsert(&widget->context, id, build->arena);
  data->len = size.size;
  data->ptr = FL_Arena_Push(build->arena, size.size, size.alignment);
  return data->ptr;
}

void FL_Widget_Layout_Default(FL_Widget *widget,
                              FL_BoxConstraints constraints) {
  FL_Vec2 size = FL_BoxConstraints_GetSmallest(constraints);
  for (FL_Widget *child = widget->first; child; child = child->next) {
    FL_Widget_Layout(child, constraints);
    size = FL_Vec2_Max(size, child->size);
  }
  widget->size = size;
}

static bool FL_Widget_HitTestChildren(FL_Widget *widget,
                                      FL_HitTestContext *context) {
  FL_Trans2 parent_transform = context->transform;
  FL_Vec2 parent_local_position = context->local_position;

  bool hit = false;
  for (FL_Widget *child = widget->last; child; child = child->prev) {
    context->transform = FL_Trans2_Dot(
        parent_transform, FL_Trans2_Offset(-child->offset.x, -child->offset.y));
    context->local_position = FL_Vec2_Sub(parent_local_position, child->offset);

    hit = FL_Widget_HitTest(child, context);
    if (hit) {
      break;
    }
  }

  context->transform = parent_transform;
  context->local_position = parent_local_position;
  return hit;
}

bool FL_Widget_HitTest_DeferToChild(FL_Widget *widget,
                                    FL_HitTestContext *context) {
  if (!FL_Vec2_Contains(context->local_position, FL_Vec2_Zero(),
                        widget->size)) {
    return false;
  }

  if (!FL_Widget_HitTestChildren(widget, context)) {
    return false;
  }

  FL_HitTest_AddWidget(context, widget);
  return true;
}

bool FL_Widget_HitTest_Transluscent(FL_Widget *widget,
                                    FL_HitTestContext *context) {
  if (!FL_Vec2_Contains(context->local_position, FL_Vec2_Zero(),
                        widget->size)) {
    return false;
  }

  bool hit_children = FL_Widget_HitTestChildren(widget, context);

  FL_HitTest_AddWidget(context, widget);

  return hit_children;
}

bool FL_Widget_HitTest_Opaque(FL_Widget *widget, FL_HitTestContext *context) {
  if (!FL_Vec2_Contains(context->local_position, FL_Vec2_Zero(),
                        widget->size)) {
    return false;
  }

  FL_Widget_HitTestChildren(widget, context);

  FL_HitTest_AddWidget(context, widget);

  return true;
}

bool FL_Widget_HitTest_ByBehaviour(FL_Widget *widget,
                                   FL_HitTestContext *context,
                                   FL_HitTestBehaviour behaviour) {
  switch (behaviour) {
    case FL_HitTestBehaviour_DeferToChild: {
      return FL_Widget_HitTest_DeferToChild(widget, context);
    } break;

    case FL_HitTestBehaviour_Translucent: {
      return FL_Widget_HitTest_Transluscent(widget, context);
    } break;

    case FL_HitTestBehaviour_Opaque: {
      return FL_Widget_HitTest_Opaque(widget, context);
    } break;

    default:
      FL_UNREACHABLE;
  }
}

bool FL_Widget_HitTest(FL_Widget *widget, FL_HitTestContext *context) {
  if (widget->klass->hit_test) {
    return widget->klass->hit_test(widget, context);
  } else {
    return FL_Widget_HitTest_Opaque(widget, context);
  }
}

void FL_Widget_Paint_Default(FL_Widget *widget, FL_PaintingContext *context,
                             FL_Vec2 offset) {
  for (FL_Widget *child = widget->first; child; child = child->next) {
    FL_Widget_Paint(child, context, FL_Vec2_Add(offset, child->offset));
  }
}

void FL_Widget_SendNotification(FL_Widget *widget, FL_NotificationID id,
                                void *data) {
  for (FL_Widget *parent = widget->parent; parent; parent = parent->parent) {
    if (parent->klass->on_notification &&
        parent->klass->on_notification(parent, id, data)) {
      break;
    }
  }
}

void FL_WidgetList_Append(FL_WidgetList *list, FL_Widget *widget) {
  Build *build = widget->build;
  FL_WidgetListEntry *entry =
      FL_Arena_PushStruct(build->arena, FL_WidgetListEntry);
  *entry = (FL_WidgetListEntry){
      .widget = widget,
  };
  FL_DLIST_ADD_LAST(list->first, list->last, entry, prev, next);
}

FL_WidgetList FL_WidgetList_Make(FL_Widget *widgets[]) {
  FL_WidgetList list = {0};
  for (int i = 0;; ++i) {
    FL_Widget *widget = widgets[i];
    if (!widget) {
      break;
    }
    FL_WidgetList_Append(&list, widget);
  }
  return list;
}

FL_f32 FL_Widget_GetDeltaTime(FL_Widget *widget) {
  Build *build = widget->build;
  return build->delta_time;
}

FL_f32 FL_Widget_AnimateFast(FL_Widget *widget, FL_f32 value, FL_f32 target) {
  Build *build = widget->build;
  FL_f32 result;
  FL_f32 diff = (target - value);
  if (FL_Abs(diff) < FL_PRECISION_ERROR_TOLERANCE) {
    result = target;
  } else {
    result = value + diff * build->fast_animation_rate;
  }
  return result;
}

FL_Arena *FL_Widget_GetArena(FL_Widget *widget) {
  Build *build = widget->build;
  return build->arena;
}

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>


static void Build_Init(Build *build, State *state, FL_isize index,
                       FL_Allocator allocator) {
  (*build) = (Build){
      .state = state,
      .arena = FL_Arena_Create(&(FL_ArenaOptions){
          .allocator = allocator,
      }),
      .index = index,
  };
}

static void Build_Deinit(Build *build) { FL_Arena_Destroy(build->arena); }

static void FL_Canvas_Save_Stub(void *ctx) {}

static void FL_Canvas_Restore_Stub(void *ctx) {}

static void FL_Canvas_ClipRect_Stub(void *ctx, FL_Rect rect) {}

static void FL_Canvas_FillRect_Stub(void *ctx, FL_Rect rect, FL_Color color) {}

static void FL_Canvas_StrokeRect_Stub(void *ctx, FL_Rect rect, FL_Color color,
                                      FL_f32 line_width) {}

static FL_TextMetrics FL_Canvas_MeasureText_Stub(void *ctx, FL_Str text,
                                                 FL_f32 font_size) {
  return (FL_TextMetrics){0};
}

static void FL_Canvas_FillText_Stub(void *ctx, FL_Str text, FL_f32 x, FL_f32 y,
                                    FL_f32 font_size, FL_Color color) {}

static void MaybeSetCanvasStub(FL_Canvas *canvas) {
  if (!canvas->save) {
    canvas->save = FL_Canvas_Save_Stub;
  }
  if (!canvas->restore) {
    canvas->restore = FL_Canvas_Restore_Stub;
  }
  if (!canvas->clip_rect) {
    canvas->clip_rect = FL_Canvas_ClipRect_Stub;
  }
  if (!canvas->fill_rect) {
    canvas->fill_rect = FL_Canvas_FillRect_Stub;
  }
  if (!canvas->stroke_rect) {
    canvas->stroke_rect = FL_Canvas_StrokeRect_Stub;
  }
  if (!canvas->measure_text) {
    canvas->measure_text = FL_Canvas_MeasureText_Stub;
  }
  if (!canvas->fill_text) {
    canvas->fill_text = FL_Canvas_FillText_Stub;
  }
}

void FL_HitTest_Init(FL_HitTestContext *context, FL_Arena *arena) {
  *context = (FL_HitTestContext){
      .arena = arena,
  };
}

void FL_HitTest_AddWidget(FL_HitTestContext *context, FL_Widget *widget) {
  FL_HitTestEntry *entry = FL_Arena_PushStruct(context->arena, FL_HitTestEntry);
  entry->widget = widget;
  entry->transform = context->transform;
  entry->local_position = context->local_position;
  FL_DLIST_ADD_LAST(context->first, context->last, entry, prev, next);
}

static bool FL_HitTest_HasWidget(FL_HitTestContext *context,
                                 FL_Widget *widget) {
  // TODO: Use hash map to speed up lookup.
  for (FL_HitTestEntry *entry = context->first; entry; entry = entry->next) {
    if (entry->widget == widget) {
      return true;
    }
  }
  return false;
}

/**
 * Update widget references to their links.
 */
static void FL_HitTest_Sync(FL_HitTestContext *context) {
  FL_HitTestEntry *entry = context->last;
  for (; entry && entry->widget; entry = entry->prev) {
    entry->widget = entry->widget->link;
    if (!entry->widget) {
      break;
    }
  }

  if (entry) {
    FL_ASSERT(!entry->widget);
    if (entry->next) {
      context->first = entry->next;
      context->first->prev = 0;
    } else {
      context->first = context->last = 0;
    }
  }
}

static void FL_HitTest_Reset(FL_HitTestContext *context) {
  FL_Arena_Reset(context->arena);
  context->first = context->last = 0;
}

static State *global_state;

static void SetGlobalState(State *state) { global_state = state; }

State *GetGlobalState(void) { return global_state; }

static void InitWidgets(void) {
  FL_Basic_Init();
  FL_Flex_Init();
  FL_Stack_Init();
  FL_Viewport_Init();
}

void FL_Init(const FL_InitOptions *opts) {
  FL_Allocator allocator = opts->allocator;
  if (!allocator.ops) {
    allocator = FL_Allocator_GetDefault();
  }

  FL_Arena *arena = FL_Arena_Create(&(FL_ArenaOptions){
      .allocator = allocator,
  });

  State *state = FL_Arena_PushStruct(arena, State);
  *state = (State){
      .arena = arena,
      .canvas = opts->canvas,
      .next_context_id = 1,
      .next_notification_id = 1,
      .next_down_pointer = 1,
  };
  MaybeSetCanvasStub(&state->canvas);

  for (int i = 0; i < FL_COUNT_OF(state->builds); i++) {
    Build_Init(state->builds + i, state, i, allocator);
  }
  state->curr_build = state->builds;
  state->last_build = state->builds + 1;

  FL_HitTest_Init(&state->button_down_hit_test_context,
                  FL_Arena_Create(&(FL_ArenaOptions){.allocator = allocator}));
  for (int i = 0; i < FL_COUNT_OF(state->button_move_hit_test_contexts); i++) {
    FL_HitTest_Init(
        state->button_move_hit_test_contexts + i,
        FL_Arena_Create(&(FL_ArenaOptions){.allocator = allocator}));
  }

  SetGlobalState(state);

  InitWidgets();
}

void FL_Deinit(void) {
  // Unmount and cleanup widgets from last build.
  FL_Run(&(FL_RunOptions){0});

  State *state = GetGlobalState();
  SetGlobalState(0);

  for (int i = 0; i < FL_COUNT_OF(global_state->builds); i++) {
    Build_Deinit(state->builds + i);
  }

  FL_Arena_Destroy(state->button_down_hit_test_context.arena);
  for (int i = 0; i < FL_COUNT_OF(state->button_move_hit_test_contexts); i++) {
    FL_Arena_Destroy(state->button_move_hit_test_contexts[i].arena);
  }

  FL_Arena_Destroy(state->arena);
}

FL_ContextID FL_Context_Register(void) {
  State *state = GetGlobalState();
  return state->next_context_id++;
}

FL_NotificationID FL_Notification_Register(void) {
  State *state = GetGlobalState();
  return state->next_notification_id++;
}

static void HitTest(FL_Widget *widget, FL_HitTestContext *context,
                    FL_Vec2 pos) {
  if (!widget) {
    return;
  }

  context->position = pos;
  context->transform = FL_Trans2_Identity();
  context->local_position = pos;
  FL_Widget_HitTest(widget, context);
}

static void FL_CheckEnterExit(State *state, Build *build, FL_Vec2 pos) {
  // Handle ENTER/EXIT event
  FL_HitTestContext *last_context = state->button_move_hit_test_contexts +
                                    state->button_move_hit_test_context_index;
  state->button_move_hit_test_context_index =
      (state->button_move_hit_test_context_index + 1) %
      FL_COUNT_OF(state->button_move_hit_test_contexts);
  FL_HitTestContext *context = state->button_move_hit_test_contexts +
                               state->button_move_hit_test_context_index;
  HitTest(build->root, context, pos);

  for (FL_HitTestEntry *entry = last_context->first; entry;
       entry = entry->next) {
    if (!FL_HitTest_HasWidget(context, entry->widget)) {
      FL_Widget_OnPointerEvent(
          entry->widget,
          (FL_PointerEvent){
              .type = FL_PointerEventType_Exit,
              .pointer = state->down_pointer,
              .button = state->down_button,
              .position = pos,
              .transform = entry->transform,
              .local_position = FL_Trans2_DotVec2(entry->transform, pos),
          });
    }
  }
  FL_PointerEventResolver_Reset(&state->resolver);

  for (FL_HitTestEntry *entry = context->first; entry; entry = entry->next) {
    if (!FL_HitTest_HasWidget(last_context, entry->widget)) {
      FL_Widget_OnPointerEvent(entry->widget,
                               (FL_PointerEvent){
                                   .type = FL_PointerEventType_Enter,
                                   .pointer = state->down_pointer,
                                   .button = state->down_button,
                                   .position = pos,
                                   .transform = entry->transform,
                                   .local_position = entry->local_position,
                               });
    }
  }
  FL_PointerEventResolver_Reset(&state->resolver);

  FL_HitTest_Reset(last_context);
}

void FL_OnMouseMove(FL_Vec2 pos) {
  State *state = GetGlobalState();
  Build *build = state->last_build;

  FL_CheckEnterExit(state, build, pos);

  if (state->down_pointer) {
    for (FL_HitTestEntry *entry = state->button_down_hit_test_context.first;
         entry; entry = entry->next) {
      FL_Widget_OnPointerEvent(
          entry->widget,
          (FL_PointerEvent){
              .type = FL_PointerEventType_Move,
              .pointer = state->down_pointer,
              .button = state->down_button,
              .position = pos,
              .transform = entry->transform,
              .local_position = FL_Trans2_DotVec2(entry->transform, pos),
              .delta = FL_Vec2_Sub(pos, state->mouse_pos),
          });
    }
    FL_PointerEventResolver_Reset(&state->resolver);
  } else {
    // Only send HOVER events if there isn't button down.

    // TODO: Reuse hit test result from above.
    HitTest(build->root, &state->button_down_hit_test_context, pos);
    for (FL_HitTestEntry *entry = state->button_down_hit_test_context.first;
         entry; entry = entry->next) {
      FL_Widget_OnPointerEvent(entry->widget,
                               (FL_PointerEvent){
                                   .type = FL_PointerEventType_Hover,
                                   .button = state->down_button,
                                   .position = pos,
                                   .transform = entry->transform,
                                   .local_position = entry->local_position,
                               });
    }
    FL_PointerEventResolver_Reset(&state->resolver);
    FL_HitTest_Reset(&state->button_down_hit_test_context);
  }

  state->mouse_pos = pos;
}

void FL_OnMouseButtonDown(FL_Vec2 pos, FL_u32 button) {
  State *state = GetGlobalState();
  Build *build = state->last_build;

  state->down_button |= button;

  // TODO: Multiple touch?
  if (state->down_pointer) {
    // If there is already pointer down, treat this as MOVE event.
    FL_OnMouseMove(pos);
  } else {
    state->down_pointer = state->next_down_pointer++;

    FL_ASSERT(state->down_button == button);

    FL_GestureArena *gesture_arena = FL_GestureArena_Open(
        &state->gesture_arena_state, state->down_pointer, state->arena);

    HitTest(build->root, &state->button_down_hit_test_context, pos);
    for (FL_HitTestEntry *entry = state->button_down_hit_test_context.first;
         entry; entry = entry->next) {
      FL_Widget_OnPointerEvent(entry->widget,
                               (FL_PointerEvent){
                                   .type = FL_PointerEventType_Down,
                                   .pointer = state->down_pointer,
                                   .button = state->down_button,
                                   .position = pos,
                                   .transform = entry->transform,
                                   .local_position = entry->local_position,
                               });
    }
    FL_PointerEventResolver_Reset(&state->resolver);

    FL_GestureArena_Close(gesture_arena);

    state->mouse_pos = pos;
  }
}

void FL_OnMouseButtonUp(FL_Vec2 pos, FL_u32 button) {
  State *state = GetGlobalState();

  state->down_button &= (~button);

  // Only send UP event when no button is down.
  if (!state->down_button) {
    FL_ASSERT(state->down_pointer);

    for (FL_HitTestEntry *entry = state->button_down_hit_test_context.first;
         entry; entry = entry->next) {
      FL_Widget_OnPointerEvent(
          entry->widget,
          (FL_PointerEvent){
              .type = FL_PointerEventType_Up,
              .pointer = state->down_pointer,
              .button = button,
              .position = pos,
              .transform = entry->transform,
              .local_position = FL_Trans2_DotVec2(entry->transform, pos),
          });
    }
    FL_PointerEventResolver_Reset(&state->resolver);

    FL_HitTest_Reset(&state->button_down_hit_test_context);

    FL_GestureArena *gesture_arena =
        FL_GestureArena_Get(&state->gesture_arena_state, state->down_pointer);
    if (gesture_arena) {
      FL_GestureArena_Sweep(gesture_arena);
    }

    state->down_pointer = 0;
    state->mouse_pos = pos;
  } else {
    FL_OnMouseMove(pos);
  }
}

void FL_OnMouseScroll(FL_Vec2 pos, FL_Vec2 delta) {
  State *state = GetGlobalState();
  Build *build = state->last_build;

  FL_Arena scratch = *build->arena;
  FL_HitTestContext context;
  FL_HitTest_Init(&context, &scratch);
  HitTest(build->root, &context, pos);
  for (FL_HitTestEntry *entry = context.first; entry; entry = entry->next) {
    FL_Widget_OnPointerEvent(entry->widget,
                             (FL_PointerEvent){
                                 .type = FL_PointerEventType_Scroll,
                                 .position = pos,
                                 .transform = entry->transform,
                                 .local_position = entry->local_position,
                                 .delta = delta,
                             });
  }
  FL_PointerEventResolver_Reset(&state->resolver);

  state->mouse_pos = pos;
}

static void ResetLink(FL_Widget *widget) {
  for (FL_Widget *child = widget->first; child; child = child->next) {
    ResetLink(child);
  }
  widget->link = 0;
}

static void PrepareNextBuild(State *state) {
  state->build_index++;
  state->curr_build =
      state->builds + (state->build_index % FL_COUNT_OF(state->builds));
  state->last_build =
      state->builds + ((state->build_index - 1) % FL_COUNT_OF(state->builds));

  Build *build = state->curr_build;
  FL_Arena_Reset(build->arena);
  build->index = state->build_index;
  build->root = 0;
}

void FL_Run(const FL_RunOptions *opts) {
  State *state = GetGlobalState();
  Build *build = state->curr_build;
  build->delta_time = opts->delta_time;
  build->fast_animation_rate = 1.0f - FL_Exp(-50.f * opts->delta_time);

  if (state->last_build->root) {
    ResetLink(state->last_build->root);
  }

  build->root = opts->widget;
  if (build->root) {
    FL_Widget_Mount(0, build->root);

    FL_Widget_Layout(
        build->root,
        FL_BoxConstraints_Tight(opts->viewport.right - opts->viewport.left,
                                opts->viewport.bottom - opts->viewport.top));
  }

  FL_HitTest_Sync(&state->button_down_hit_test_context);
  for (int i = 0; i < FL_COUNT_OF(state->button_move_hit_test_contexts); i++) {
    FL_HitTest_Sync(state->button_move_hit_test_contexts + i);
  }

  FL_CheckEnterExit(state, build, state->mouse_pos);

  if (state->last_build->root) {
    FL_Widget_Unmount(state->last_build->root);
  }

  if (build->root) {
    FL_PaintingContext context = {.canvas = &state->canvas};
    FL_Widget_Paint(build->root, &context,
                    (FL_Vec2){opts->viewport.left, opts->viewport.top});
  }

  PrepareNextBuild(state);
}

FL_TextMetrics FL_MeasureText(FL_Str text, FL_f32 font_size) {
  State *state = GetGlobalState();
  return FL_Canvas_MeasureText(&state->canvas, text, font_size);
}

FL_Str FL_Format(const char *format, ...) {
  State *state = GetGlobalState();
  Build *build = state->curr_build;
  FL_Arena *arena = build->arena;

  va_list ap;
  va_start(ap, format);
  FL_Str result = FL_Str_FormatV(arena, format, ap);
  va_end(ap);

  return result;
}

#include <stdint.h>


static void FL_ColoredBox_Mount(FL_Widget *widget) {
  FL_ColoredBoxProps *props = FL_Widget_GetProps(widget, FL_ColoredBoxProps);
  if (props->child) {
    FL_Widget_Mount(widget, props->child);
  }
}

static void FL_ColoredBox_Paint(FL_Widget *widget, FL_PaintingContext *context,
                                FL_Vec2 offset) {
  FL_ColoredBoxProps *props = FL_Widget_GetProps(widget, FL_ColoredBoxProps);
  FL_Vec2 size = widget->size;
  if (size.x > 0 && size.y > 0) {
    FL_Canvas_FillRect(context->canvas, FL_Rect_FromMinSize(offset, size),
                       props->color);
  }

  FL_Widget_Paint_Default(widget, context, offset);
}

static FL_WidgetClass FL_ColoredBoxClass = {
    .name = "ColoredBox",
    .props_size = FL_SIZE_OF(FL_ColoredBoxProps),
    .mount = FL_ColoredBox_Mount,
    .paint = FL_ColoredBox_Paint,
    .hit_test = FL_Widget_HitTest_Opaque,
};

FL_Widget *FL_ColoredBox(const FL_ColoredBoxProps *props) {
  return FL_Widget_Create(&FL_ColoredBoxClass, props->key, props);
}

static void FL_ConstrainedBox_Layout(FL_Widget *widget,
                                     FL_BoxConstraints constraints) {
  FL_ConstrainedBoxProps *props =
      FL_Widget_GetProps(widget, FL_ConstrainedBoxProps);
  FL_BoxConstraints enforced_constraints =
      FL_BoxConstraints_Enforce(props->constraints, constraints);
  FL_Widget *child = props->child;
  if (child) {
    FL_Widget_Mount(widget, child);
    FL_Widget_Layout(child, enforced_constraints);
    widget->size = child->size;
  } else {
    widget->size =
        FL_BoxConstraints_Constrain(enforced_constraints, FL_Vec2_Zero());
  }
}

static FL_WidgetClass FL_ConstrainedBoxClass = {
    .name = "ConstrainedBox",
    .props_size = FL_SIZE_OF(FL_ConstrainedBoxProps),
    .layout = FL_ConstrainedBox_Layout,
};

FL_Widget *FL_ConstrainedBox(const FL_ConstrainedBoxProps *props) {
  return FL_Widget_Create(&FL_ConstrainedBoxClass, props->key, props);
}

static FL_BoxConstraints limit_constraints(FL_BoxConstraints constraints,
                                           FL_f32 max_width,
                                           FL_f32 max_height) {
  return (FL_BoxConstraints){
      constraints.min_width,
      FL_BoxConstraints_HasBoundedWidth(constraints)
          ? constraints.max_width
          : FL_BoxConstraints_ConstrainWidth(constraints, max_width),
      constraints.min_height,
      FL_BoxConstraints_HasBoundedHeight(constraints)
          ? constraints.max_height
          : FL_BoxConstraints_ConstrainHeight(constraints, max_height),
  };
}

static void FL_LimitedBox_Layout(FL_Widget *widget,
                                 FL_BoxConstraints constraints) {
  FL_LimitedBoxProps *props = FL_Widget_GetProps(widget, FL_LimitedBoxProps);
  FL_BoxConstraints limited_constraints =
      limit_constraints(constraints, props->max_width, props->max_height);

  FL_Widget *child = props->child;
  if (child) {
    FL_Widget_Mount(widget, child);
    FL_Widget_Layout(child, limited_constraints);
    FL_Vec2 child_size = child->size;
    widget->size = FL_BoxConstraints_Constrain(constraints, child_size);
  } else {
    widget->size =
        FL_BoxConstraints_Constrain(limited_constraints, FL_Vec2_Zero());
  }
}

static FL_WidgetClass FL_LimitedBoxClass = {
    .name = "LimitedBox",
    .props_size = FL_SIZE_OF(FL_LimitedBoxProps),
    .layout = FL_LimitedBox_Layout,
};

FL_Widget *FL_LimitedBox(const FL_LimitedBoxProps *props) {
  return FL_Widget_Create(&FL_LimitedBoxClass, props->key, props);
}

static void AlignChildren(FL_Widget *widget, FL_Alignment alignment) {
  // TODO: text direction
  for (FL_Widget *child = widget->first; child; child = child->next) {
    child->offset = FL_Alignment_AlignOffset(
        alignment, FL_Vec2_Sub(widget->size, child->size));
  }
}

static void FL_Align_Layout(FL_Widget *widget, FL_BoxConstraints constraints) {
  FL_AlignProps *props = FL_Widget_GetProps(widget, FL_AlignProps);
  FL_f32o width = props->width;
  FL_f32o height = props->height;

  if (props->width.present) {
    FL_ASSERTF(width.value >= 0, "width must be positive, got %f",
               (FL_f64)width.value);
  }
  if (height.present) {
    FL_ASSERTF(height.value >= 0, "height must be positive, got %f",
               (FL_f64)height.value);
  }
  bool should_shrink_wrap_width =
      width.present || FL_IsInfinite(constraints.max_width);
  bool should_shrink_wrap_height =
      height.present || FL_IsInfinite(constraints.max_height);

  FL_Widget *child = props->child;
  if (child) {
    FL_BoxConstraints child_constraints = FL_BoxConstraints_Loosen(constraints);

    FL_Widget_Mount(widget, child);
    FL_Widget_Layout(child, child_constraints);
    FL_Vec2 child_size = child->size;

    FL_Vec2 wrap_size = (FL_Vec2){
        should_shrink_wrap_width
            ? (child_size.x * (width.present ? width.value : 1.0f))
            : FL_INFINITY,
        should_shrink_wrap_height
            ? (child_size.y * (height.present ? height.value : 1.0f))
            : FL_INFINITY,
    };

    widget->size = FL_BoxConstraints_Constrain(constraints, wrap_size);

    AlignChildren(widget, props->alignment);
  } else {
    FL_Vec2 size = (FL_Vec2){
        should_shrink_wrap_width ? 0 : FL_INFINITY,
        should_shrink_wrap_height ? 0 : FL_INFINITY,
    };
    widget->size = FL_BoxConstraints_Constrain(constraints, size);
  }
}

static FL_WidgetClass FL_AlignClass = {
    .name = "Align",
    .props_size = FL_SIZE_OF(FL_AlignProps),
    .layout = FL_Align_Layout,
};

FL_Widget *FL_Align(const FL_AlignProps *props) {
  return FL_Widget_Create(&FL_AlignClass, props->key, props);
}

FL_WidgetClass FL_CenterClass = {
    .name = "Center",
    .props_size = FL_SIZE_OF(FL_AlignProps),
    .layout = FL_Align_Layout,
};

FL_Widget *FL_Center(const FL_CenterProps *props) {
  FL_AlignProps align_props = {
      .key = props->key,
      .alignment = FL_Alignment_Center(),
      .width = props->width,
      .height = props->height,
      .child = props->child,
  };
  return FL_Widget_Create(&FL_CenterClass, align_props.key, &align_props);
}

typedef struct FLResolvedEdgeInsets {
  FL_f32 left;
  FL_f32 right;
  FL_f32 top;
  FL_f32 bottom;
} FLResolvedEdgeInsets;

static void FL_Padding_Layout(FL_Widget *widget,
                              FL_BoxConstraints constraints) {
  FL_PaddingProps *props = FL_Widget_GetProps(widget, FL_PaddingProps);
  // TODO: text direction
  FLResolvedEdgeInsets resolved_padding = {
      .left = props->padding.start,
      .right = props->padding.end,
      .top = props->padding.top,
      .bottom = props->padding.bottom,
  };
  FL_f32 horizontal = resolved_padding.left + resolved_padding.right;
  FL_f32 vertical = resolved_padding.top + resolved_padding.bottom;

  FL_Widget *child = props->child;
  if (child) {
    FL_BoxConstraints inner_constraints =
        FL_BoxConstraints_Deflate(constraints, horizontal, vertical);

    FL_Widget_Mount(widget, child);
    FL_Widget_Layout(child, inner_constraints);
    FL_Vec2 child_size = child->size;
    child->offset = (FL_Vec2){resolved_padding.left, resolved_padding.top};

    widget->size = FL_BoxConstraints_Constrain(
        constraints,
        (FL_Vec2){horizontal + child_size.x, vertical + child_size.y});
  } else {
    widget->size = FL_BoxConstraints_Constrain(constraints,
                                               (FL_Vec2){horizontal, vertical});
  }
}

FL_WidgetClass FL_PaddingClass = {
    .name = "Padding",
    .props_size = FL_SIZE_OF(FL_PaddingProps),
    .layout = FL_Padding_Layout,
};

FL_Widget *FL_Padding(const FL_PaddingProps *props) {
  return FL_Widget_Create(&FL_PaddingClass, props->key, props);
}

FL_Widget *FL_Container(const FL_ContainerProps *props) {
  FL_BoxConstraintsO constraints = props->constraints;

  if (props->width.present || props->height.present) {
    if (constraints.present) {
      constraints = FL_BoxConstraints_Some(FL_BoxConstraints_Tighten(
          constraints.value, props->width, props->height));
    } else {
      constraints = FL_BoxConstraints_Some(
          FL_BoxConstraints_TightFor(props->width, props->height));
    }
  }

  FL_Widget *widget = props->child;
  if (!widget &&
      (!constraints.present || !FL_BoxConstraints_IsTight(constraints.value))) {
    widget = FL_LimitedBox(&(FL_LimitedBoxProps){
        .child = FL_ConstrainedBox(&(FL_ConstrainedBoxProps){
            .constraints =
                (FL_BoxConstraints){
                    FL_INFINITY,
                    FL_INFINITY,
                    FL_INFINITY,
                    FL_INFINITY,
                },
        }),
    });
  } else if (props->alignment.present) {
    widget = FL_Align(&(FL_AlignProps){
        .alignment = props->alignment.value,
        .child = widget,
    });
  }

  if (props->padding.present) {
    widget = FL_Padding(&(FL_PaddingProps){
        .padding = props->padding.value,
        .child = widget,
    });
  }

  if (props->color.present) {
    widget = FL_ColoredBox(&(FL_ColoredBoxProps){
        .color = props->color.value,
        .child = widget,
    });
  }

  if (constraints.present) {
    widget = FL_ConstrainedBox(&(FL_ConstrainedBoxProps){
        .constraints = constraints.value,
        .child = widget,
    });
  }

  if (props->margin.present) {
    widget = FL_Padding(&(FL_PaddingProps){
        .padding = props->margin.value,
        .child = widget,
    });
  }

  FL_ASSERT(widget);

  return widget;
}

static void FL_UnconstrainedBox_Layout(FL_Widget *widget,
                                       FL_BoxConstraints constraints) {
  FL_UnconstrainedBoxProps *props =
      FL_Widget_GetProps(widget, FL_UnconstrainedBoxProps);

  FL_Widget *child = props->child;
  if (child) {
    FL_BoxConstraints child_constraints = (FL_BoxConstraints){
        0,
        FL_INFINITY,
        0,
        FL_INFINITY,
    };

    FL_Widget_Mount(widget, child);
    FL_Widget_Layout(widget->first, child_constraints);
    FL_Vec2 child_size = child->size;
    widget->size = FL_BoxConstraints_Constrain(constraints, child_size);
  } else {
    widget->size = FL_BoxConstraints_Constrain(constraints, FL_Vec2_Zero());
  }

  AlignChildren(widget, props->alignment);
}

static FL_WidgetClass FL_UnconstrainedBoxClass = {
    .name = "UnconstrainedBox",
    .props_size = FL_SIZE_OF(FL_UnconstrainedBoxProps),
    .layout = FL_UnconstrainedBox_Layout,
};

FL_Widget *FL_UnconstrainedBox(const FL_UnconstrainedBoxProps *props) {
  return FL_Widget_Create(&FL_UnconstrainedBoxClass, props->key, props);
}

void FL_Basic_Init(void) {}

#include <stdbool.h>
#include <stdint.h>


static FL_ContextID FL_FlexContext_ID;

typedef struct FL_FlexContext {
  FL_i32 flex;
  FL_FlexFit fit;
} FL_FlexContext;

typedef struct AxisSize {
  FL_f32 main;
  FL_f32 cross;
} AxisSize;

static inline AxisSize AxisSize_FromVec2(FL_Vec2 size, FL_Axis direction) {
  switch (direction) {
    case FL_Axis_Horizontal: {
      return (AxisSize){size.x, size.y};
    } break;

    case FL_Axis_Vertical: {
      return (AxisSize){size.y, size.x};
    } break;

    default:
      FL_UNREACHABLE;
  }
}

static inline FL_Vec2 AxisSize_ToVec2(AxisSize size, FL_Axis direction) {
  switch (direction) {
    case FL_Axis_Horizontal: {
      return (FL_Vec2){size.main, size.cross};
    } break;

    case FL_Axis_Vertical: {
      return (FL_Vec2){size.cross, size.main};
    } break;

    default:
      FL_UNREACHABLE;
  }
}

static inline FL_BoxConstraints GetConstrainsForNonFlexChild(
    FL_Axis direction, FL_CrossAxisAlignment cross_axis_alignment,
    FL_BoxConstraints constraints) {
  bool should_fill_cross_axis = false;
  if (cross_axis_alignment == FL_CrossAxisAlignment_Stretch) {
    should_fill_cross_axis = true;
  }

  FL_BoxConstraints result;
  switch (direction) {
    case FL_Axis_Horizontal: {
      if (should_fill_cross_axis) {
        result = FL_BoxConstraints_TightHeight(constraints.max_height);
      } else {
        result = (FL_BoxConstraints){
            .min_width = 0,
            .max_width = FL_INFINITY,
            .min_height = 0,
            .max_height = constraints.max_height,
        };
      }
    } break;
    case FL_Axis_Vertical: {
      if (should_fill_cross_axis) {
        result = FL_BoxConstraints_TightWidth(constraints.max_width);
      } else {
        result = (FL_BoxConstraints){
            .min_width = 0,
            .max_width = constraints.max_width,
            .min_height = 0,
            .max_height = FL_INFINITY,
        };
      }
    } break;
    default:
      FL_UNREACHABLE;
  }
  return result;
}

static inline FL_BoxConstraints GetConstrainsForFlexChild(
    FL_Axis direction, FL_CrossAxisAlignment cross_axis_alignment,
    FL_BoxConstraints constraints, FL_f32 max_child_extent,
    FL_FlexContext *flex) {
  FL_DEBUG_ASSERT(flex->flex > 0);
  FL_DEBUG_ASSERT(max_child_extent >= 0.0f);
  FL_f32 min_child_extent = 0.0;
  if (flex->fit == FL_FlexFit_Tight) {
    min_child_extent = max_child_extent;
  }
  bool should_fill_cross_axis = false;
  if (cross_axis_alignment == FL_CrossAxisAlignment_Stretch) {
    should_fill_cross_axis = true;
  }
  FL_BoxConstraints result;
  if (direction == FL_Axis_Horizontal) {
    result = (FL_BoxConstraints){
        .min_width = min_child_extent,
        .max_width = max_child_extent,
        .min_height = should_fill_cross_axis ? constraints.max_height : 0,
        .max_height = constraints.max_height,
    };
  } else {
    result = (FL_BoxConstraints){
        .min_width = should_fill_cross_axis ? constraints.max_width : 0,
        .max_width = constraints.max_width,
        .min_height = min_child_extent,
        .max_height = max_child_extent,
    };
  }
  return result;
}

static AxisSize AxisSize_Constrains(AxisSize size,
                                    FL_BoxConstraints constraints,
                                    FL_Axis direction) {
  FL_BoxConstraints effective_constraints = constraints;
  if (direction != FL_Axis_Horizontal) {
    effective_constraints = FL_BoxConstraints_Flip(constraints);
  }
  FL_Vec2 constrained_size = FL_BoxConstraints_Constrain(
      effective_constraints, (FL_Vec2){size.main, size.cross});
  return (AxisSize){constrained_size.x, constrained_size.y};
}

typedef struct LayoutSize {
  AxisSize size;
  FL_f32 main_axis_free_space;
  bool can_flex;
  FL_f32 space_per_flex;
} LayoutSize;

static LayoutSize FL_Flex_ComputeSize(
    FL_Widget *widget, FL_Axis direction, FL_MainAxisSize main_axis_size,
    FL_CrossAxisAlignment cross_axis_alignment, FL_f32 spacing,
    FL_BoxConstraints constraints) {
  // Determine used flex factor, size inflexible items, calculate free space.
  FL_f32 max_main_size;
  switch (direction) {
    case FL_Axis_Horizontal: {
      max_main_size =
          FL_BoxConstraints_ConstrainWidth(constraints, FL_INFINITY);
    } break;

    case FL_Axis_Vertical: {
      max_main_size =
          FL_BoxConstraints_ConstrainHeight(constraints, FL_INFINITY);
    } break;

    default:
      FL_UNREACHABLE;
  }

  bool can_flex = FL_IsFinite(max_main_size);
  FL_BoxConstraints non_flex_child_constraints = GetConstrainsForNonFlexChild(
      direction, cross_axis_alignment, constraints);
  // TODO: Baseline aligned

  // The first pass lays out non-flex children and computes total flex.
  FL_i32 total_flex = 0;
  FL_Widget *first_flex_child = 0;
  // Initially, accumulated_size is the sum of the spaces between children in
  // the main axis.
  AxisSize accumulated_size = {spacing * (FL_f32)(widget->child_count - 1),
                               0.0f};
  for (FL_Widget *child = widget->first; child; child = child->next) {
    FL_i32 child_flex = 0;
    if (can_flex) {
      FL_FlexContext *data =
          FL_Widget_GetContext(child, FL_FlexContext_ID, FL_FlexContext);
      if (data) {
        child_flex = data->flex;
      }
    }

    if (child_flex > 0) {
      total_flex += child_flex;
      if (!first_flex_child) {
        first_flex_child = child;
      }
    } else {
      FL_Widget_Layout(child, non_flex_child_constraints);
      AxisSize child_size = AxisSize_FromVec2(child->size, direction);

      accumulated_size.main += child_size.main;
      accumulated_size.cross = FL_Max(accumulated_size.cross, child_size.cross);
    }
  }

  FL_DEBUG_ASSERT((total_flex == 0) == (first_flex_child == 0));
  FL_DEBUG_ASSERT(first_flex_child == 0 || can_flex);

  // The second pass distributes free space to flexible children.
  FL_f32 flex_space = FL_Max(0.0f, max_main_size - accumulated_size.main);
  FL_f32 space_per_flex = flex_space / (FL_f32)total_flex;
  for (FL_Widget *child = widget->first; child && total_flex > 0;
       child = child->next) {
    FL_FlexContext *data =
        FL_Widget_GetContext(child, FL_FlexContext_ID, FL_FlexContext);
    if (!data || data->flex <= 0) {
      continue;
    }
    total_flex -= data->flex;
    FL_DEBUG_ASSERT(FL_IsFinite(space_per_flex));
    FL_f32 max_child_extent = space_per_flex * (FL_f32)data->flex;
    FL_DEBUG_ASSERT(data->fit == FL_FlexFit_Loose ||
                    max_child_extent < FL_INFINITY);
    FL_BoxConstraints child_constraints = GetConstrainsForFlexChild(
        direction, cross_axis_alignment, constraints, max_child_extent, data);
    FL_Widget_Layout(child, child_constraints);
    AxisSize child_size = AxisSize_FromVec2(child->size, direction);

    accumulated_size.main += child_size.main;
    accumulated_size.cross = FL_Max(accumulated_size.cross, child_size.cross);
  }
  FL_DEBUG_ASSERT(total_flex == 0);

  FL_f32 ideal_main_size;
  if (main_axis_size == FL_MainAxisSize_Max && FL_IsFinite(max_main_size)) {
    ideal_main_size = max_main_size;
  } else {
    ideal_main_size = accumulated_size.main;
  }

  AxisSize size = {ideal_main_size, accumulated_size.cross};
  AxisSize constrained_size = AxisSize_Constrains(size, constraints, direction);

  return (LayoutSize){
      .size = constrained_size,
      .main_axis_free_space = constrained_size.main - accumulated_size.main,
      .can_flex = can_flex,
      .space_per_flex = can_flex ? space_per_flex : 0,
  };
}

static void FL_Flex_DistributeSpace(FL_MainAxisAlignment main_axis_alignment,
                                    FL_f32 free_space, FL_isize item_count,
                                    bool flipped, FL_f32 spacing,
                                    FL_f32 *leading_space,
                                    FL_f32 *between_space) {
  switch (main_axis_alignment) {
    case FL_MainAxisAlignment_Start: {
      if (flipped) {
        *leading_space = free_space;
      } else {
        *leading_space = 0;
      }
      *between_space = spacing;
    } break;

    case FL_MainAxisAlignment_End: {
      FL_Flex_DistributeSpace(FL_MainAxisAlignment_Start, free_space,
                              item_count, !flipped, spacing, leading_space,
                              between_space);
    } break;

    case FL_MainAxisAlignment_SpaceBetween: {
      if (item_count < 2) {
        FL_Flex_DistributeSpace(FL_MainAxisAlignment_Start, free_space,
                                item_count, flipped, spacing, leading_space,
                                between_space);
      } else {
        *leading_space = 0;
        *between_space = free_space / (FL_f32)(item_count - 1) + spacing;
      }
    } break;

    case FL_MainAxisAlignment_SpaceAround: {
      if (item_count == 0) {
        FL_Flex_DistributeSpace(FL_MainAxisAlignment_Start, free_space,
                                item_count, flipped, spacing, leading_space,
                                between_space);
      } else {
        *leading_space = free_space / (FL_f32)item_count / 2;
        *between_space = free_space / (FL_f32)item_count + spacing;
      }
    } break;

    case FL_MainAxisAlignment_Center: {
      *leading_space = free_space / 2.0f;
      *between_space = spacing;
    } break;

    case FL_MainAxisAlignment_SpaceEvenly: {
      *leading_space = free_space / (FL_f32)(item_count + 1);
      *between_space = free_space / (FL_f32)(item_count + 1) + spacing;
    } break;

    default:
      FL_UNREACHABLE;
  }
}

static FL_f32 GetChildCrossAxisOffset(
    FL_CrossAxisAlignment cross_axis_alignment, FL_f32 free_space,
    bool flipped) {
  switch (cross_axis_alignment) {
    case FL_CrossAxisAlignment_Stretch:
    case FL_CrossAxisAlignment_Baseline: {
      return 0.0f;
    } break;

    case FL_CrossAxisAlignment_Start: {
      return flipped ? free_space : 0.0f;
    } break;

    case FL_CrossAxisAlignment_Center: {
      return free_space / 2.0f;
    } break;

    case FL_CrossAxisAlignment_End: {
      return GetChildCrossAxisOffset(FL_CrossAxisAlignment_Start, free_space,
                                     !flipped);
    } break;

    default:
      FL_UNREACHABLE;
  }
}

static FL_f32 GetCrossSize(FL_Vec2 size, FL_Axis direction) {
  if (direction == FL_Axis_Horizontal) {
    return size.y;
  } else {
    return size.x;
  }
}

static FL_f32 GetMainSize(FL_Vec2 size, FL_Axis direction) {
  if (direction == FL_Axis_Horizontal) {
    return size.x;
  } else {
    return size.y;
  }
}

static void FL_Flex_Layout(FL_Widget *widget, FL_BoxConstraints constraints) {
  FL_FlexProps *props = FL_Widget_GetProps(widget, FL_FlexProps);

  for (FL_WidgetListEntry *entry = props->children.first; entry;
       entry = entry->next) {
    FL_Widget_Mount(widget, entry->widget);
  }

  LayoutSize sizes = FL_Flex_ComputeSize(
      widget, props->direction, props->main_axis_size,
      props->cross_axis_alignment, props->spacing, constraints);
  FL_f32 cross_axis_extent = sizes.size.cross;
  widget->size = AxisSize_ToVec2(sizes.size, props->direction);
  // TODO: Handle overflow.

  FL_f32 remaining_space = FL_Max(0.0f, sizes.main_axis_free_space);
  // TODO: Handle text direction and vertical direction.
  FL_f32 leading_space;
  FL_f32 between_space;
  FL_Flex_DistributeSpace(props->main_axis_alignment, remaining_space,
                          widget->child_count, /* flipped= */ false,
                          props->spacing, &leading_space, &between_space);

  // Position all children in visual order: starting from the top-left child and
  // work towards the child that's farthest away from the origin.
  FL_f32 child_main_position = leading_space;
  for (FL_Widget *child = widget->first; child; child = child->next) {
    FL_f32 child_cross_position = GetChildCrossAxisOffset(
        props->cross_axis_alignment,
        cross_axis_extent - GetCrossSize(child->size, props->direction),
        /* flipped= */ false);
    if (props->direction == FL_Axis_Horizontal) {
      child->offset = (FL_Vec2){child_main_position, child_cross_position};
    } else {
      child->offset = (FL_Vec2){child_cross_position, child_main_position};
    }
    child_main_position +=
        GetMainSize(child->size, props->direction) + between_space;
  }
}

static FL_WidgetClass FL_FlexClass = {
    .name = "Flex",
    .props_size = FL_SIZE_OF(FL_FlexProps),
    .layout = FL_Flex_Layout,
};

FL_Widget *FL_Flex(const FL_FlexProps *props) {
  return FL_Widget_Create(&FL_FlexClass, props->key, props);
}

static FL_WidgetClass FL_ColumnClass = {
    .name = "Column",
    .props_size = FL_SIZE_OF(FL_FlexProps),
    .layout = FL_Flex_Layout,
};

FL_Widget *FL_Column(const FL_ColumnProps *props) {
  FL_FlexProps flex_props = {
      .key = props->key,
      .direction = FL_Axis_Vertical,
      .main_axis_alignment = props->main_axis_alignment,
      .main_axis_size = props->main_axis_size,
      .cross_axis_alignment = props->cross_axis_alignment,
      .spacing = props->spacing,
      .children = props->children,
  };
  return FL_Widget_Create(&FL_ColumnClass, flex_props.key, &flex_props);
}

static FL_WidgetClass FL_RowClass = {
    .name = "Row",
    .props_size = FL_SIZE_OF(FL_FlexProps),
    .layout = FL_Flex_Layout,
};

FL_Widget *FL_Row(const FL_RowProps *props) {
  FL_FlexProps flex_props = {
      .key = props->key,
      .direction = FL_Axis_Horizontal,
      .main_axis_alignment = props->main_axis_alignment,
      .main_axis_size = props->main_axis_size,
      .cross_axis_alignment = props->cross_axis_alignment,
      .spacing = props->spacing,
      .children = props->children,
  };
  return FL_Widget_Create(&FL_RowClass, flex_props.key, &flex_props);
}

static void FL_Flexible_Mount(FL_Widget *widget) {
  FL_FlexibleProps *props = FL_Widget_GetProps(widget, FL_FlexibleProps);
  FL_FlexContext *flex =
      FL_Widget_SetContext(widget, FL_FlexContext_ID, FL_FlexContext);
  *flex = (FL_FlexContext){
      .flex = props->flex,
      .fit = props->fit,
  };
  if (props->child) {
    FL_Widget_Mount(widget, props->child);
  }
}

static FL_WidgetClass FL_FlexibleClass = {
    .name = "Flexible",
    .props_size = FL_SIZE_OF(FL_FlexibleProps),
    .mount = FL_Flexible_Mount,
};

FL_Widget *FL_Flexible(const FL_FlexibleProps *props) {
  return FL_Widget_Create(&FL_FlexibleClass, props->key, props);
}

static void FL_Expanded_Mount(FL_Widget *widget) {
  FL_ExpandedProps *props = FL_Widget_GetProps(widget, FL_ExpandedProps);
  FL_FlexContext *flex =
      FL_Widget_SetContext(widget, FL_FlexContext_ID, FL_FlexContext);
  *flex = (FL_FlexContext){
      .flex = props->flex,
      .fit = FL_FlexFit_Tight,
  };
  if (props->child) {
    FL_Widget_Mount(widget, props->child);
  }
}

static FL_WidgetClass FL_ExpandedClass = {
    .name = "Expanded",
    .props_size = FL_SIZE_OF(FL_ExpandedProps),
    .mount = FL_Expanded_Mount,
};

FL_Widget *FL_Expanded(const FL_ExpandedProps *props) {
  return FL_Widget_Create(&FL_ExpandedClass, props->key, props);
}

void FL_Flex_Init(void) { FL_FlexContext_ID = FL_Context_Register(); }

#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>


static void FL_PointerListener_Mount(FL_Widget *widget) {
  FL_PointerListenerProps *props =
      FL_Widget_GetProps(widget, FL_PointerListenerProps);
  if (props->child) {
    FL_Widget_Mount(widget, props->child);
  }
}

static inline void MaybeCallCallback(FL_PointerListenerCallback *callback,
                                     void *context, FL_PointerEvent event) {
  if (callback) {
    callback(context, event);
  }
}

static bool FL_PointerListener_HitTest(FL_Widget *widget,
                                       FL_HitTestContext *context) {
  FL_PointerListenerProps *props =
      FL_Widget_GetProps(widget, FL_PointerListenerProps);
  return FL_Widget_HitTest_ByBehaviour(widget, context, props->behaviour);
}

static void FL_PointerListener_OnPointerEvent(FL_Widget *widget,
                                              FL_PointerEvent event) {
  FL_PointerListenerProps *props =
      FL_Widget_GetProps(widget, FL_PointerListenerProps);
  switch (event.type) {
    case FL_PointerEventType_Down: {
      MaybeCallCallback(props->on_down, props->context, event);
    } break;

    case FL_PointerEventType_Move: {
      MaybeCallCallback(props->on_move, props->context, event);
    } break;

    case FL_PointerEventType_Up: {
      MaybeCallCallback(props->on_up, props->context, event);
    } break;

    case FL_PointerEventType_Enter: {
      MaybeCallCallback(props->on_enter, props->context, event);
    } break;

    case FL_PointerEventType_Hover: {
      MaybeCallCallback(props->on_hover, props->context, event);
    } break;

    case FL_PointerEventType_Exit: {
      MaybeCallCallback(props->on_exit, props->context, event);
    } break;

    case FL_PointerEventType_Cancel: {
      MaybeCallCallback(props->on_cancel, props->context, event);
    } break;

    case FL_PointerEventType_Scroll: {
      MaybeCallCallback(props->on_scroll, props->context, event);
    } break;

    default: {
    } break;
  }
}

static FL_WidgetClass FL_PointerListenerClass = {
    .name = "PointerListener",
    .props_size = FL_SIZE_OF(FL_PointerListenerProps),
    .mount = FL_PointerListener_Mount,
    .hit_test = FL_PointerListener_HitTest,
    .on_pointer_event = FL_PointerListener_OnPointerEvent,
};

FL_Widget *FL_PointerListener(const FL_PointerListenerProps *props) {
  return FL_Widget_Create(&FL_PointerListenerClass, props->key, props);
}

typedef struct FL_TapGestureRecognizer {
  FL_Widget *widget;
  void *context;
  FL_GestureCallback *tap_down;
  FL_GestureCallback *tap_up;
  FL_GestureCallback *tap;
  FL_GestureCallback *tap_cancel;

  FL_GestureArenaEntry *entry;
  FL_PointerEvent down;
  FL_PointerEvent up;
  FL_f32 time;
  bool won_arena;
  bool sent_down;
} FL_TapGestureRecognizer;

static void FL_TapGestureRecognizer_Reset(FL_TapGestureRecognizer *state) {
  if (state->entry) {
    FL_GestureArena_Resolve(state->entry, /* accepted= */ false);
  }

  state->down = (FL_PointerEvent){0};
  state->up = (FL_PointerEvent){0};
  state->time = 0;
  state->won_arena = false;
  state->sent_down = false;
}

static void FL_TapGestureRecognizer_CheckDown(FL_TapGestureRecognizer *state) {
  if (state->sent_down) {
    return;
  }

  FL_ASSERT(state->down.type == FL_PointerEventType_Down);

  if (state->tap_down) {
    state->tap_down(state->context,
                    (FL_GestureDetails){
                        .local_position = state->down.local_position,
                    });
  }

  state->sent_down = true;
}

static void FL_TapGestureRecognizer_CheckUp(FL_TapGestureRecognizer *state) {
  if (!state->won_arena || state->up.type != FL_PointerEventType_Up) {
    return;
  }

  if (state->tap_up) {
    state->tap_up(state->context,
                  (FL_GestureDetails){
                      .local_position = state->up.local_position,
                  });
  }

  if (state->tap) {
    state->tap(state->context, (FL_GestureDetails){
                                   .local_position = state->up.local_position,
                               });
  }

  FL_TapGestureRecognizer_Reset(state);
}

static void FL_TapGestureRecognizer_Update(FL_TapGestureRecognizer *state) {
  if (state->entry) {
    FL_GestureArena_Update(state->entry, state);
  }

  if (state->down.type == FL_PointerEventType_Down && !state->sent_down) {
    state->time += FL_Widget_GetDeltaTime(state->widget);
    if (state->time >= 0.1f) {
      FL_TapGestureRecognizer_CheckDown(state);
    }
  }
}

static void FL_TapGestureRecognizer_Cancel(FL_TapGestureRecognizer *state) {
  if (state->entry) {
    FL_GestureArena_Resolve(state->entry, /* accepted= */ false);
  } else {
    if (state->sent_down) {
      if (state->tap_cancel) {
        state->tap_cancel(state->context, (FL_GestureDetails){0});
      }
    }
    FL_TapGestureRecognizer_Reset(state);
  }
}

static void FL_TapGestureRecognizer_Accept(void *ctx, FL_i32 pointer) {
  FL_TapGestureRecognizer *state = ctx;
  FL_ASSERT(state->down.type == FL_PointerEventType_Down);
  FL_ASSERT(state->down.pointer == pointer);

  state->entry = 0;
  state->won_arena = true;

  FL_TapGestureRecognizer_CheckDown(state);
  FL_TapGestureRecognizer_CheckUp(state);
}

static void FL_TapGestureRecognizer_Reject(void *ctx, FL_i32 pointer) {
  FL_TapGestureRecognizer *state = ctx;
  FL_ASSERT(state->down.type == FL_PointerEventType_Down);
  FL_ASSERT(state->down.pointer == pointer);

  state->entry = 0;
  FL_TapGestureRecognizer_Cancel(state);
}

static FL_GestureArenaMemberOps FL_TapGestureRecognizer_ops = {
    .accept = FL_TapGestureRecognizer_Accept,
    .reject = FL_TapGestureRecognizer_Reject,
};

static void FL_TapGestureRecognizer_OnPointerEvent(
    FL_TapGestureRecognizer *state, FL_PointerEvent event) {
  switch (event.type) {
    case FL_PointerEventType_Down: {
      if (state->down.type != FL_PointerEventType_Down) {
        FL_u32 allowed_button = 0;
        if (state->tap_down || state->tap_up || state->tap ||
            state->tap_cancel) {
          allowed_button |= FL_BUTTON_PRIMARY;
        }
        if (event.button & allowed_button) {
          state->down = event;
          state->entry = FL_GestureArena_Add(
              event.pointer, &FL_TapGestureRecognizer_ops, state);
        }
      }
    } break;

    case FL_PointerEventType_Move: {
      if (state->down.type == FL_PointerEventType_Down) {
        FL_f32 dist_squared = FL_Vec2_GetLenSquared(
            FL_Vec2_Sub(event.position, state->down.position));
        if (dist_squared > 18 * 18) {
          FL_TapGestureRecognizer_Cancel(state);
        }
      }
    } break;

    case FL_PointerEventType_Up: {
      if (state->down.type == FL_PointerEventType_Down &&
          state->down.pointer == event.pointer &&
          FL_Vec2_Contains(event.local_position, FL_Vec2_Zero(),
                           state->widget->size)) {
        state->up = event;
        FL_TapGestureRecognizer_CheckUp(state);
      } else {
        FL_TapGestureRecognizer_Cancel(state);
      }
    } break;

    case FL_PointerEventType_Cancel: {
      FL_TapGestureRecognizer_Cancel(state);
    } break;

    default: {
    } break;
  }
}

typedef struct FL_DragGestureRecognizer {
  FL_Widget *widget;
  void *context;
  FL_GestureCallback *drag_down;
  FL_GestureCallback *drag_start;
  FL_GestureCallback *drag_update;
  FL_GestureCallback *drag_end;
  FL_GestureCallback *drag_cancel;

  FL_GestureArenaEntry *entry;
  FL_PointerEvent down;
  FL_Vec2 last_position;
  FL_Vec2 position;
  FL_Vec2 local_position;
  bool won_arena;
} FL_DragGestureRecognizer;

static void FL_DragGestureRecognizer_Update(FL_DragGestureRecognizer *state) {
  if (state->entry) {
    FL_GestureArena_Update(state->entry, state);
  }
}

static void FL_DragGestureRecognizer_Cancel(FL_DragGestureRecognizer *state) {
  if (state->entry) {
    FL_GestureArena_Resolve(state->entry, /* accepted= */ false);
  } else {
    if (state->down.pointer) {
      if (state->won_arena) {
        if (state->drag_end) {
          state->drag_end(state->context,
                          (FL_GestureDetails){
                              .local_position = state->local_position,
                          });
        }
      } else {
        if (state->drag_cancel) {
          state->drag_cancel(state->context,
                             (FL_GestureDetails){
                                 .local_position = state->local_position,
                             });
        }
      }
    }

    FL_ASSERT(!state->entry);
    state->down = (FL_PointerEvent){0};
    state->last_position = (FL_Vec2){0};
    state->position = (FL_Vec2){0};
    state->local_position = (FL_Vec2){0};
    state->won_arena = false;
  }
}

static void FL_DragGestureRecognizer_Accept(void *ctx, FL_i32 pointer) {
  FL_DragGestureRecognizer *state = ctx;
  FL_ASSERT(state->down.type == FL_PointerEventType_Down);
  FL_ASSERT(state->down.pointer == pointer);

  state->won_arena = true;
  state->entry = 0;

  if (state->drag_start) {
    state->drag_start(state->context,
                      (FL_GestureDetails){
                          .local_position = state->down.local_position,
                      });
  }
}

static void FL_DragGestureRecognizer_Reject(void *ctx, FL_i32 pointer) {
  FL_DragGestureRecognizer *state = ctx;
  FL_ASSERT(state->down.type == FL_PointerEventType_Down);
  FL_ASSERT(state->down.pointer == pointer);

  state->entry = 0;

  FL_DragGestureRecognizer_Cancel(state);
}

static FL_GestureArenaMemberOps FL_DragGestureRecognizerOps = {
    .accept = FL_DragGestureRecognizer_Accept,
    .reject = FL_DragGestureRecognizer_Reject,
};

static void FL_DragGestureRecognizer_OnPointerEvent(
    FL_DragGestureRecognizer *state, FL_PointerEvent event) {
  switch (event.type) {
    case FL_PointerEventType_Down: {
      if (!state->down.pointer) {
        state->entry = FL_GestureArena_Add(event.pointer,
                                           &FL_DragGestureRecognizerOps, state);
        state->down = event;
        state->position = event.position;
        state->local_position = event.local_position;
        if (state->drag_down) {
          state->drag_down(state->context,
                           (FL_GestureDetails){
                               .local_position = event.local_position,
                           });
        }
      }
    } break;

    case FL_PointerEventType_Move: {
      if (state->down.pointer == event.pointer) {
        state->last_position = state->position;
        state->position = event.position;
        state->local_position = event.local_position;
        if (state->won_arena) {
          FL_Vec2 delta = FL_Vec2_Sub(state->position, state->last_position);
          if (state->drag_update) {
            state->drag_update(state->context,
                               (FL_GestureDetails){
                                   .local_position = state->local_position,
                                   .delta = delta,
                               });
          }
        } else if (FL_Vec2_GetLenSquared(FL_Vec2_Sub(
                       state->position, state->down.position)) > 4.0f) {
          FL_GestureArena_Resolve(state->entry, /* accepted= */ true);
        }
      } else {
        FL_DragGestureRecognizer_Cancel(state);
      }
    } break;

    case FL_PointerEventType_Up:
    case FL_PointerEventType_Cancel: {
      if (state->down.pointer == event.pointer) {
        state->last_position = state->position;
        state->position = event.position;
        state->local_position = event.local_position;
        FL_DragGestureRecognizer_Cancel(state);
      }
    } break;

    default: {
    } break;
  }
}

typedef struct FL_GestureDetectorState {
  FL_TapGestureRecognizer *tap;
  FL_DragGestureRecognizer *drag;
} FL_GestureDetectorState;

static void *DupOrPushZero_(FL_Arena *arena, void *src, FL_isize size,
                            FL_isize alignment) {
  if (src) {
    return FL_Arena_Dup(arena, src, size, alignment);
  }
  void *dst = FL_Arena_Push(arena, size, alignment);
  memset(dst, 0, size);
  return dst;
}

#define DupOrPushZero(arena, src) \
  (FL_TYPE_OF(src))(DupOrPushZero_(arena, src, sizeof(*src), alignof(*src)))

static void FL_GestureDetector_Mount(FL_Widget *widget) {
  FL_GestureDetectorProps *props =
      FL_Widget_GetProps(widget, FL_GestureDetectorProps);
  FL_GestureDetectorState *state =
      FL_Widget_GetState(widget, FL_GestureDetectorState);

  FL_Arena *arena = FL_Widget_GetArena(widget);
  if (props->tap_down || props->tap_up || props->tap || props->tap_cancel) {
    state->tap = DupOrPushZero(arena, state->tap);
    state->tap->widget = widget;
    state->tap->context = props->context;
    state->tap->tap_down = props->tap_down;
    state->tap->tap_up = props->tap_up;
    state->tap->tap = props->tap;
    state->tap->tap_cancel = props->tap_cancel;
    FL_TapGestureRecognizer_Update(state->tap);
  } else if (state->tap) {
    state->tap->widget = widget;
    state->tap->context = 0;
    state->tap->tap_down = 0;
    state->tap->tap_up = 0;
    state->tap->tap = 0;
    state->tap->tap_cancel = 0;
    FL_TapGestureRecognizer_Cancel(state->tap);
  }

  if (props->drag_down || props->drag_start || props->drag_update ||
      props->drag_end || props->drag_cancel) {
    state->drag = DupOrPushZero(arena, state->drag);
    state->drag->widget = widget;
    state->drag->context = props->context;
    state->drag->drag_down = props->drag_down;
    state->drag->drag_start = props->drag_start;
    state->drag->drag_update = props->drag_update;
    state->drag->drag_end = props->drag_end;
    state->drag->drag_cancel = props->drag_cancel;
    FL_DragGestureRecognizer_Update(state->drag);
  } else if (state->drag) {
    state->drag->widget = widget;
    state->drag->context = 0;
    state->drag->drag_down = 0;
    state->drag->drag_start = 0;
    state->drag->drag_update = 0;
    state->drag->drag_end = 0;
    state->drag->drag_cancel = 0;
    FL_DragGestureRecognizer_Cancel(state->drag);
  }

  if (props->child) {
    FL_Widget_Mount(widget, props->child);
  }
}

static void FL_GestureDetector_Unmount(FL_Widget *widget) {
  if (widget->link) {
    return;
  }

  FL_GestureDetectorState *state =
      FL_Widget_GetState(widget, FL_GestureDetectorState);

  if (state->tap) {
    FL_TapGestureRecognizer_Cancel(state->tap);
  }

  if (state->drag) {
    FL_DragGestureRecognizer_Cancel(state->drag);
  }
}

static bool FL_GestureDetector_HitTest(FL_Widget *widget,
                                       FL_HitTestContext *context) {
  FL_GestureDetectorProps *props =
      FL_Widget_GetProps(widget, FL_GestureDetectorProps);
  return FL_Widget_HitTest_ByBehaviour(widget, context, props->behaviour);
}

static void FL_GestureDetector_OnPointerEvent(FL_Widget *widget,
                                              FL_PointerEvent event) {
  FL_GestureDetectorState *state =
      FL_Widget_GetState(widget, FL_GestureDetectorState);
  if (state->tap) {
    FL_TapGestureRecognizer_OnPointerEvent(state->tap, event);
  }
  if (state->drag) {
    FL_DragGestureRecognizer_OnPointerEvent(state->drag, event);
  }
}

static FL_WidgetClass FL_GestureDetectorClass = {
    .name = "GestureDetector",
    .props_size = FL_SIZE_OF(FL_GestureDetectorProps),
    .state_size = FL_SIZE_OF(FL_GestureDetectorState),
    .mount = FL_GestureDetector_Mount,
    .unmount = FL_GestureDetector_Unmount,
    .hit_test = FL_GestureDetector_HitTest,
    .on_pointer_event = FL_GestureDetector_OnPointerEvent,
};

FL_Widget *FL_GestureDetector(const FL_GestureDetectorProps *props) {
  return FL_Widget_Create(&FL_GestureDetectorClass, props->key, props);
}

#include <stdint.h>


static FL_ContextID FL_PositionedContext_ID;

typedef struct FLStackState {
  bool has_visual_overflow;
} FLStackState;

typedef struct FLPositionedContext {
  FL_f32o left;
  FL_f32o right;
  FL_f32o top;
  FL_f32o bottom;
  FL_f32o width;
  FL_f32o height;
} FLPositionedContext;

static inline bool is_positioned(FLPositionedContext *self) {
  if (!self) {
    return false;
  }

  return self->left.present || self->right.present || self->top.present ||
         self->bottom.present || self->width.present || self->height.present;
}

static FL_Vec2 FL_Stack_compute_size(FL_Widget *widget, FL_StackFit fit,
                                     FL_BoxConstraints constraints) {
  if (!widget->first) {
    FL_Vec2 biggest = FL_BoxConstraints_GetBiggest(constraints);
    if (FL_Vec2_IsFinite(biggest)) {
      return biggest;
    }
    return FL_BoxConstraints_GetSmallest(constraints);
  }

  FL_f32 width = constraints.min_width;
  FL_f32 height = constraints.min_height;

  FL_BoxConstraints non_positioned_constraints;
  switch (fit) {
    case FL_StackFit_Loose: {
      non_positioned_constraints = FL_BoxConstraints_Loosen(constraints);
    } break;
    case FL_StackFit_Expand: {
      FL_Vec2 biggest = FL_BoxConstraints_GetBiggest(constraints);
      non_positioned_constraints =
          FL_BoxConstraints_Tight(biggest.x, biggest.y);
    } break;
    default: {
      non_positioned_constraints = constraints;
    } break;
  }

  bool has_non_positioned_child = false;
  for (FL_Widget *child = widget->first; child; child = child->next) {
    FLPositionedContext *positioned = FL_Widget_GetContext(
        child, FL_PositionedContext_ID, FLPositionedContext);

    if (!is_positioned(positioned)) {
      has_non_positioned_child = true;
      FL_Widget_Layout(child, non_positioned_constraints);
      FL_Vec2 child_size = child->size;

      width = FL_Max(width, child_size.x);
      height = FL_Max(height, child_size.y);
    }
  }

  FL_Vec2 size;
  if (has_non_positioned_child) {
    size = (FL_Vec2){width, height};
    FL_ASSERT(size.x == FL_BoxConstraints_ConstrainWidth(constraints, width));
    FL_ASSERT(size.y == FL_BoxConstraints_ConstrainHeight(constraints, height));
  } else {
    size = FL_BoxConstraints_GetBiggest(constraints);
  }

  FL_ASSERT(FL_Vec2_IsFinite(size));
  return size;
}

static FL_BoxConstraints get_positioned_child_constraints(
    FLPositionedContext *self, FL_Vec2 stack_size) {
  FL_f32o width = FL_f32_None();
  if (self->left.present && self->right.present) {
    width = FL_f32_Some(stack_size.x - self->right.value - self->left.value);
  } else {
    width = self->width;
  }

  FL_f32o height = FL_f32_None();
  if (self->top.present && self->bottom.present) {
    height = FL_f32_Some(stack_size.y - self->bottom.value - self->top.value);
  } else {
    height = self->height;
  }

  FL_ASSERT(!width.present || !FL_IsNaN(width.value));
  FL_ASSERT(!height.present || !FL_IsNaN(height.value));

  if (width.present) {
    width.value = FL_Max(0, width.value);
  }
  if (height.present) {
    height.value = FL_Max(0, height.value);
  }

  return FL_BoxConstraints_TightFor(width, height);
}

/** Returns true when the child has visual overflow. */
static bool layout_positioned_child(FL_Widget *child,
                                    FLPositionedContext *positioned,
                                    FL_Vec2 size) {
  FL_BoxConstraints child_constraints =
      get_positioned_child_constraints(positioned, size);
  FL_Widget_Layout(child, child_constraints);
  FL_Vec2 child_size = child->size;

  FL_f32 x;
  if (positioned->left.present) {
    x = positioned->left.value;
  } else if (positioned->right.present) {
    x = size.x - positioned->right.value - child_size.x;
  } else {
    // TODO: alignment
    x = 0;
  }

  FL_f32 y;
  if (positioned->top.present) {
    y = positioned->top.value;
  } else if (positioned->bottom.present) {
    y = size.y - positioned->bottom.value - child_size.y;
  } else {
    // TODO: alignment
    y = 0;
  }

  child->offset = (FL_Vec2){x, y};

  return x < 0 || x + child_size.x > size.x || y < 0 ||
         y + child_size.y > size.y;
}

static void FL_Stack_Layout(FL_Widget *widget, FL_BoxConstraints constraints) {
  FL_StackProps *props = FL_Widget_GetProps(widget, FL_StackProps);
  FLStackState *state = FL_Widget_GetState(widget, FLStackState);

  for (FL_WidgetListEntry *entry = props->children.first; entry;
       entry = entry->next) {
    FL_Widget_Mount(widget, entry->widget);
  }

  FL_Vec2 size = FL_Stack_compute_size(widget, props->fit, constraints);
  widget->size = size;

  state->has_visual_overflow = false;
  for (FL_Widget *child = widget->first; child; child = child->next) {
    FLPositionedContext *positioned = FL_Widget_GetContext(
        child, FL_PositionedContext_ID, FLPositionedContext);
    if (!is_positioned(positioned)) {
      // TODO: alignment
    } else {
      state->has_visual_overflow =
          layout_positioned_child(child, positioned, size) ||
          state->has_visual_overflow;
    }
  }
}

static void FL_Stack_Paint(FL_Widget *widget, FL_PaintingContext *context,
                           FL_Vec2 offset) {
  FLStackState *state = FL_Widget_GetState(widget, FLStackState);

  bool should_clip = state->has_visual_overflow;
  if (should_clip) {
    FL_Canvas_Save(context->canvas);
    FL_Canvas_ClipRect(context->canvas,
                       FL_Rect_FromMinSize(offset, widget->size));
  }

  FL_Widget_Paint_Default(widget, context, offset);

  if (should_clip) {
    FL_Canvas_Restore(context->canvas);
  }
}

static FL_WidgetClass FL_StackClass = {
    .name = "Stack",
    .props_size = FL_SIZE_OF(FL_StackProps),
    .state_size = FL_SIZE_OF(FLStackState),
    .layout = FL_Stack_Layout,
    .paint = FL_Stack_Paint,
};

FL_Widget *FL_Stack(const FL_StackProps *props) {
  return FL_Widget_Create(&FL_StackClass, props->key, props);
}

static void FL_Positioned_Mount(FL_Widget *widget) {
  FL_PositionedProps *props = FL_Widget_GetProps(widget, FL_PositionedProps);
  FLPositionedContext *positioned = FL_Widget_SetContext(
      widget, FL_PositionedContext_ID, FLPositionedContext);
  *positioned = (FLPositionedContext){
      .left = props->left,
      .right = props->right,
      .top = props->top,
      .bottom = props->bottom,
      .width = props->width,
      .height = props->height,
  };
  if (props->child) {
    FL_Widget_Mount(widget, props->child);
  }
}

FL_WidgetClass FL_PositionedClass = {
    .name = "Positioned",
    .props_size = FL_SIZE_OF(FL_PositionedProps),
    .mount = FL_Positioned_Mount,
};

FL_Widget *FL_Positioned(const FL_PositionedProps *props) {
  return FL_Widget_Create(&FL_PositionedClass, props->key, props);
}

void FL_Stack_Init(void) { FL_PositionedContext_ID = FL_Context_Register(); }


typedef struct FL_TextState {
  FL_f32 font_size;
  FL_f32 ascent;
  FL_Color color;
} FL_TextState;

static void FL_Text_Layout(FL_Widget *widget, FL_BoxConstraints constraints) {
  FL_TextProps *props = FL_Widget_GetProps(widget, FL_TextProps);
  FL_TextState *state = FL_Widget_GetState(widget, FL_TextState);

  // TODO: Get default text style from widget tree.
  FL_f32 font_size = 13;
  if (props->style.present) {
    if (props->style.value.font_size.present) {
      font_size = props->style.value.font_size.value;
    }
  }
  state->font_size = font_size;

  // TODO: Get default text style from widget tree.
  FL_Color color = {1, 1, 1, 1};
  if (props->style.present) {
    if (props->style.value.color.present) {
      color = props->style.value.color.value;
    }
  }
  state->color = color;

  FL_TextMetrics metrics = FL_MeasureText(props->text, state->font_size);

  state->ascent = metrics.font_bounding_box_ascent;
  // TODO: Handle overflow.
  widget->size = FL_BoxConstraints_Constrain(
      constraints,
      (FL_Vec2){
          metrics.width,
          metrics.font_bounding_box_ascent + metrics.font_bounding_box_descent,
      });
}

static void FL_Text_Paint(FL_Widget *widget, FL_PaintingContext *context,
                          FL_Vec2 offset) {
  FL_TextProps *props = FL_Widget_GetProps(widget, FL_TextProps);
  FL_TextState *state = FL_Widget_GetState(widget, FL_TextState);
  FL_Canvas_FillText(context->canvas, props->text, offset.x,
                     offset.y + state->ascent, state->font_size, state->color);
}

static FL_WidgetClass FL_TextClass = {
    .name = "Text",
    .props_size = FL_SIZE_OF(FL_TextProps),
    .state_size = FL_SIZE_OF(FL_TextState),
    .layout = FL_Text_Layout,
    .paint = FL_Text_Paint,
    .hit_test = FL_Widget_HitTest_Opaque,
};

FL_Widget *FL_Text(const FL_TextProps *props) {
  return FL_Widget_Create(&FL_TextClass, props->key, props);
}

#include <stdint.h>


FL_ContextID FL_SliverContext_ID;

static FL_NotificationID FL_ScrollNotification_ID;

typedef struct FL_ScrollNotification {
  /** The number of viewports that this notification has bubbled through. */
  FL_i32 depth;
  FL_f32 scroll_offset;
  FL_f32 max_scroll_extent;

  FL_Widget *scrollable;
  void (*scroll_to)(FL_Widget *widget, FL_f32 to);
} FL_ScrollNotification;

typedef struct FL_ViewportState {
  bool has_visual_overflow;
  FL_f32 max_scroll_extent;
  FL_f32 max_scroll_offset;
} FL_ViewportState;

static FL_ScrollDirection FL_ScrollDirection_Flip(
    FL_ScrollDirection scroll_direction) {
  switch (scroll_direction) {
    case FL_ScrollDirection_Forward: {
      return FL_ScrollDirection_Reverse;
    } break;

    case FL_ScrollDirection_Reverse: {
      return FL_ScrollDirection_Forward;
    } break;

    default: {
      return scroll_direction;
    } break;
  }
}

static FL_ScrollDirection FL_ScrollDirection_ApplyGrowthDirection(
    FL_ScrollDirection scroll_direction, FL_GrowthDirection growth) {
  if (growth == FL_GrowthDirection_Reverse) {
    return FL_ScrollDirection_Flip(scroll_direction);
  }

  return scroll_direction;
}

static FL_AxisDirection FL_AxisDirection_Flip(FL_AxisDirection self) {
  switch (self) {
    case FL_AxisDirection_Up: {
      return FL_AxisDirection_Down;
    } break;

    case FL_AxisDirection_Down: {
      return FL_AxisDirection_Up;
    } break;

    case FL_AxisDirection_Left: {
      return FL_AxisDirection_Right;
    } break;

    case FL_AxisDirection_Right: {
      return FL_AxisDirection_Left;
    } break;

    default: {
      FL_UNREACHABLE;
    } break;
  }
}

static FL_AxisDirection FL_AxisDirection_ApplyGrowthDirection(
    FL_AxisDirection self, FL_GrowthDirection growth) {
  if (growth == FL_GrowthDirection_Reverse) {
    return FL_AxisDirection_Flip(self);
  }
  return self;
}

static FL_f32 FL_Viewport_LayoutChildren(
    FL_Widget *widget, FL_ViewportProps *props, FL_ViewportState *state,
    FL_Widget *child, FL_f32 scroll_offset, FL_f32 overlap,
    FL_f32 layout_offset, FL_f32 remaining_painting_extent,
    FL_f32 main_axis_extent, FL_f32 cross_axis_extent,
    FL_GrowthDirection growth_direction, FL_f32 remaining_cache_extent,
    FL_f32 cache_origin) {
  FL_ASSERT(FL_IsFinite(scroll_offset));
  FL_ASSERT(scroll_offset >= 0);
  FL_f32 initial_layout_offset = layout_offset;
  FL_ScrollDirection scroll_direction = FL_ScrollDirection_ApplyGrowthDirection(
      props->offset.scroll_direction, growth_direction);
  FL_f32 max_paint_offset = layout_offset + overlap;
  FL_f32 preceeding_scroll_extent = 0;

  while (child) {
    FL_f32 sliver_scroll_offset = scroll_offset < 0 ? 0 : scroll_offset;
    FL_f32 corrected_cache_origin = FL_Max(cache_origin, -sliver_scroll_offset);
    FL_f32 cache_extent_correction = cache_origin - corrected_cache_origin;
    FL_ASSERT(sliver_scroll_offset >= FL_Abs(corrected_cache_origin));
    FL_ASSERT(corrected_cache_origin <= 0);
    FL_ASSERT(sliver_scroll_offset >= 0);
    FL_ASSERT(cache_extent_correction <= 0);

    FL_SliverContext *sliver =
        FL_Widget_SetContext(child, FL_SliverContext_ID, FL_SliverContext);
    *sliver = (FL_SliverContext){
        .constraints =
            {
                .axis_direction = props->axis_direction,
                .growth_direction = growth_direction,
                .scroll_direction = scroll_direction,
                .scroll_offset = sliver_scroll_offset,
                .preceeding_scroll_extent = preceeding_scroll_extent,
                .overlap = max_paint_offset - layout_offset,
                .remaining_paint_extent =
                    FL_Max(0, remaining_painting_extent - layout_offset +
                                  initial_layout_offset),
                .cross_axis_extent = cross_axis_extent,
                .cross_axis_direction = props->cross_axis_direction,
                .main_axis_extent = main_axis_extent,
                .remaining_cache_extent =
                    FL_Max(0, remaining_cache_extent + cache_extent_correction),
                .cache_origin = corrected_cache_origin,
            },
        .layout_offset = layout_offset,
    };
    FL_Widget_Layout(child, FL_BoxConstraints_FromSliverConstraints(
                                sliver->constraints, 0, FL_INFINITY));

    if (sliver->geometry.scroll_offset_correction != 0) {
      return sliver->geometry.scroll_offset_correction;
    }

    state->has_visual_overflow |= sliver->geometry.has_visual_overflow;

    FL_f32 effective_layout_offset =
        layout_offset + sliver->geometry.paint_origin;
    switch (FL_AxisDirection_ApplyGrowthDirection(props->axis_direction,
                                                  growth_direction)) {
      case FL_AxisDirection_Up: {
        child->offset = (FL_Vec2){
            0, widget->size.y - layout_offset - sliver->geometry.paint_extent};
      } break;

      case FL_AxisDirection_Down: {
        child->offset = (FL_Vec2){0, layout_offset};
      } break;

      case FL_AxisDirection_Left: {
        child->offset = (FL_Vec2){
            widget->size.x - layout_offset - sliver->geometry.paint_extent, 0};
      } break;

      case FL_AxisDirection_Right: {
        child->offset = (FL_Vec2){layout_offset, 0};
      } break;

      default: {
        FL_UNREACHABLE;
      } break;
    }

    max_paint_offset =
        FL_Max(effective_layout_offset + sliver->geometry.paint_extent,
               max_paint_offset);
    scroll_offset -= sliver->geometry.scroll_extent;
    preceeding_scroll_extent += sliver->geometry.scroll_extent;
    layout_offset += sliver->geometry.layout_extent;
    if (sliver->geometry.cache_extent != 0) {
      remaining_cache_extent -=
          sliver->geometry.cache_extent - cache_extent_correction;
      cache_origin =
          FL_Min(corrected_cache_origin + sliver->geometry.cache_extent, 0);
    }

    child = child->next;
  }

  state->max_scroll_extent += preceeding_scroll_extent;

  return 0;
}

static FL_f32 FL_Viewport_AttemptLayout(
    FL_Widget *widget, FL_ViewportProps *props, FL_ViewportState *state,
    FL_f32 main_axis_extent, FL_f32 cross_axis_extent, FL_f32 offset) {
  FL_f32 center_offset = main_axis_extent * props->anchor - offset;
  FL_f32 reverse_direction_remaining_paint_extent =
      FL_Clamp(center_offset, 0, main_axis_extent);
  FL_f32 forward_direction_remaining_paint_extent =
      FL_Clamp(main_axis_extent - center_offset, 0, main_axis_extent);

  FL_f32 cache_extent = props->cache_extent;
  FL_f32 full_cache_extent = main_axis_extent + 2 * cache_extent;
  FL_f32 center_cache_offset = center_offset + cache_extent;
  FL_f32 reverse_direction_remaining_cache_extent =
      FL_Clamp(center_cache_offset, 0, full_cache_extent);
  // TODO: reverse scroll direction
  (void)reverse_direction_remaining_cache_extent;
  FL_f32 forward_direction_remaining_cache_extent =
      FL_Clamp(full_cache_extent - center_offset, 0, full_cache_extent);

  state->has_visual_overflow = false;
  state->max_scroll_extent = cache_extent;
  return FL_Viewport_LayoutChildren(
      widget, props, state, /* child= */ widget->first,
      /* scroll_offset= */ FL_Max(0, -center_offset),
      /* overlap= */ FL_Min(0, -center_offset),
      /* layout_offset= */ center_offset >= main_axis_extent
          ? center_offset
          : reverse_direction_remaining_paint_extent,
      forward_direction_remaining_paint_extent, main_axis_extent,
      cross_axis_extent, FL_GrowthDirection_Forward,
      forward_direction_remaining_cache_extent,
      FL_Clamp(center_offset, -cache_extent, 0));
}

static void FL_Viewport_Layout(FL_Widget *widget,
                               FL_BoxConstraints constraints) {
  FL_ViewportProps *props = FL_Widget_GetProps(widget, FL_ViewportProps);
  for (FL_WidgetListEntry *entry = props->slivers.first; entry;
       entry = entry->next) {
    FL_Widget_Mount(widget, entry->widget);
  }

  FL_Vec2 size = FL_BoxConstraints_GetBiggest(constraints);
  widget->size = size;

  if (!widget->first) {
    return;
  }

  if (FL_IsInfinite(size.x) || FL_IsInfinite(size.y)) {
    FL_DEBUG_ASSERT_F(false, "Cannot layout Viewport with infinity space.");
    return;
  }

  FL_ViewportState *state = FL_Widget_GetState(widget, FL_ViewportState);
  FL_f32 main_axis_extent = size.x;
  FL_f32 cross_axis_extent = size.y;
  if (FL_Axis_FromAxisDirection(props->axis_direction) == FL_Axis_Vertical) {
    main_axis_extent = size.y;
    cross_axis_extent = size.x;
  }

  FL_isize max_layout_counts = 10 * widget->child_count;
  FL_isize layout_index = 0;
  for (; layout_index < max_layout_counts; ++layout_index) {
    FL_f32 correction =
        FL_Viewport_AttemptLayout(widget, props, state, main_axis_extent,
                                  cross_axis_extent, props->offset.points);
    if (correction != 0.0f) {
      // TODO
      FL_UNREACHABLE;
    } else {
      break;
    }
  }
  FL_ASSERT(layout_index < max_layout_counts);

  state->max_scroll_offset =
      FL_Max(0, state->max_scroll_extent - main_axis_extent);
}

static void FL_Viewport_Paint(FL_Widget *widget, FL_PaintingContext *context,
                              FL_Vec2 offset) {
  FL_ViewportState *state = FL_Widget_GetState(widget, FL_ViewportState);

  bool should_clip = state->has_visual_overflow;
  if (should_clip) {
    FL_Canvas_Save(context->canvas);
    FL_Canvas_ClipRect(context->canvas,
                       FL_Rect_FromMinSize(offset, widget->size));
  }

  for (FL_Widget *child = widget->first; child; child = child->next) {
    FL_SliverContext *sliver =
        FL_Widget_GetContext(child, FL_SliverContext_ID, FL_SliverContext);
    FL_ASSERT(sliver);
    if (sliver->geometry.paint_extent > 0) {
      for (FL_Widget *child = widget->first; child; child = child->next) {
        FL_Widget_Paint(child, context, FL_Vec2_Add(offset, child->offset));
      }
    }
  }

  if (should_clip) {
    FL_Canvas_Restore(context->canvas);
  }
}

static bool FL_Viewport_OnNotification(FL_Widget *widget, FL_NotificationID id,
                                       void *data) {
  (void)widget;

  if (id == FL_ScrollNotification_ID) {
    FL_ScrollNotification *scroll = data;
    scroll->depth += 1;
  }

  return false;
}

static FL_WidgetClass FL_ViewportClass = {
    .name = "Viewport",
    .props_size = FL_SIZE_OF(FL_ViewportProps),
    .state_size = FL_SIZE_OF(FL_ViewportState),
    .layout = FL_Viewport_Layout,
    .paint = FL_Viewport_Paint,
    .hit_test = FL_Widget_HitTest_Opaque,
    .on_notification = FL_Viewport_OnNotification,
};

FL_Widget *FL_Viewport(const FL_ViewportProps *props) {
  return FL_Widget_Create(&FL_ViewportClass, props->key, props);
}

typedef struct FL_ScrollbarState {
  FL_ScrollNotification scroll;

  FL_f32 ratio;
  FL_f32 handle_padding_top;
  FL_f32 handle_extent;

  bool hovering;
} FL_ScrollbarState;

static void FL_Scrollbar_ScrollTo(void *ctx, FL_GestureDetails details) {
  FL_Widget *widget = ctx;
  FL_ScrollbarState *state = FL_Widget_GetState(widget, FL_ScrollbarState);

  FL_f32 offset = details.local_position.y - state->handle_extent / 2.0f;
  if (state->scroll.scroll_to) {
    state->scroll.scroll_to(state->scroll.scrollable, offset / state->ratio);
  }
}

static void FL_Scrollbar_OnEnterHandle(void *ctx, FL_PointerEvent event) {
  (void)event;
  FL_Widget *widget = ctx;
  FL_ScrollbarState *state = FL_Widget_GetState(widget, FL_ScrollbarState);
  state->hovering = true;
}

static void FL_Scrollbar_OnExitHandle(void *ctx, FL_PointerEvent event) {
  (void)event;
  FL_Widget *widget = ctx;
  FL_ScrollbarState *state = FL_Widget_GetState(widget, FL_ScrollbarState);
  state->hovering = false;
}

static bool FL_Scrollbar_OnNotification(FL_Widget *widget, FL_NotificationID id,
                                        void *data) {
  if (id == FL_ScrollNotification_ID) {
    FL_ScrollNotification *scroll = data;
    if (scroll->depth == 0) {
      FL_ScrollbarState *state = FL_Widget_GetState(widget, FL_ScrollbarState);
      state->scroll = *scroll;
      return true;
    }
  }

  return false;
}

static void FL_Scrollbar_Layout(FL_Widget *widget,
                                FL_BoxConstraints constraints) {
  FL_ScrollbarProps *props = FL_Widget_GetProps(widget, FL_ScrollbarProps);
  FL_ScrollbarState *state = FL_Widget_GetState(widget, FL_ScrollbarState);
  FL_Widget *child = props->child;

  if (!child) {
    return;
  }

  FL_Widget_Mount(widget, child);

  FL_f32 scrollbar_width = 10;
  FL_Widget_Layout(child,
                   FL_BoxConstraints_Deflate(constraints, scrollbar_width, 0));

  widget->size = FL_Vec2_Add(child->size, (FL_Vec2){scrollbar_width, 0});
  FL_Vec2 size = widget->size;

  state->ratio = size.y / state->scroll.max_scroll_extent;
  state->handle_extent = FL_Max(4, state->ratio * size.y);
  state->handle_padding_top = state->scroll.scroll_offset * state->ratio;
  FL_f32 handle_padding_bottom =
      size.y - state->handle_padding_top - state->handle_extent;

  FL_Widget *scrollbar = FL_GestureDetector(&(FL_GestureDetectorProps){
      .context = widget,
      .drag_start = FL_Scrollbar_ScrollTo,
      .drag_update = FL_Scrollbar_ScrollTo,
      .child = FL_Container(&(FL_ContainerProps){
          .color = FL_Color_Some((FL_Color){0.96f, 0.96f, 0.96f, 1.0f}),
          .padding = FL_EdgeInsets_Some((FL_EdgeInsets){
              0, 0, state->handle_padding_top, handle_padding_bottom}),
          .child = FL_PointerListener(&(FL_PointerListenerProps){
              .context = widget,
              .on_enter = FL_Scrollbar_OnEnterHandle,
              .on_exit = FL_Scrollbar_OnExitHandle,
              .child = FL_Container(&(FL_ContainerProps){
                  .width = FL_f32_Some(size.x),
                  .color = FL_Color_Some(
                      state->hovering ? (FL_Color){0.58f, 0.58f, 0.58f, 1.0f}
                                      : (FL_Color){0.75f, 0.75f, 0.75f, 1.0f}),
              }),
          }),
      }),
  });

  FL_Widget_Mount(widget, scrollbar);
  FL_Widget_Layout(scrollbar, FL_BoxConstraints_Tight(scrollbar_width, size.y));
  scrollbar->offset.x = child->size.x;
}

static FL_WidgetClass FL_ScrollbarClass = {
    .name = "Scrollbar",
    .props_size = FL_SIZE_OF(FL_ScrollbarProps),
    .state_size = FL_SIZE_OF(FL_ScrollbarState),
    .layout = FL_Scrollbar_Layout,
    .on_notification = FL_Scrollbar_OnNotification,
};

FL_Widget *FL_Scrollbar(const FL_ScrollbarProps *props) {
  return FL_Widget_Create(&FL_ScrollbarClass, props->key, props);
}

typedef struct FL_ScrollableState {
  FL_f32 target_scroll_offset;
  FL_f32 scroll_offset;
  FL_f32 max_scroll_offset;
} FL_ScrollableState;

static void FL_Scrollable_ScrollTo(FL_Widget *widget,
                                   FL_f32 target_scroll_offset) {
  FL_ScrollableProps *props = FL_Widget_GetProps(widget, FL_ScrollableProps);
  FL_ScrollableState *state = FL_Widget_GetState(widget, FL_ScrollableState);
  state->target_scroll_offset =
      FL_Clamp(target_scroll_offset, 0, state->max_scroll_offset);
  if (props->scroll) {
    *props->scroll = state->target_scroll_offset;
  }
}

static void FL_Scrollable_Layout(FL_Widget *widget,
                                 FL_BoxConstraints constraints) {
  FL_ScrollableProps *props = FL_Widget_GetProps(widget, FL_ScrollableProps);
  FL_ScrollableState *state = FL_Widget_GetState(widget, FL_ScrollableState);

  if (props->scroll) {
    FL_Scrollable_ScrollTo(widget, *props->scroll);
  }

  state->scroll_offset = FL_Widget_AnimateFast(widget, state->scroll_offset,
                                               state->target_scroll_offset);

  FL_Widget *viewport = FL_Viewport(&(FL_ViewportProps){
      // .axis_direction = props->axis_direction,
      // .cross_axis_direction = props->cross_axis_direction,
      .offset =
          {
              .points = state->scroll_offset,
          },
      // .cache_extent = props->cache_extent,
      .slivers = props->slivers,
  });
  FL_Widget_Mount(widget, viewport);

  FL_Widget_Layout(viewport, constraints);
  widget->size = viewport->size;

  FL_ViewportState *viewport_state =
      FL_Widget_GetState(viewport, FL_ViewportState);
  state->max_scroll_offset = viewport_state->max_scroll_offset;

  FL_ScrollNotification data = {
      .scroll_offset = state->scroll_offset,
      .max_scroll_extent = viewport_state->max_scroll_extent,
      .scrollable = widget,
      .scroll_to = FL_Scrollable_ScrollTo,
  };
  FL_Widget_SendNotification(widget, FL_ScrollNotification_ID, &data);
}

static void FL_Scrollable_OnPointerEvent(FL_Widget *widget,
                                         FL_PointerEvent event) {
  if (event.type == FL_PointerEventType_Scroll &&
      FL_PointerEventResolver_Register(widget)) {
    FL_ScrollableState *state = FL_Widget_GetState(widget, FL_ScrollableState);
    FL_Scrollable_ScrollTo(widget, state->target_scroll_offset + event.delta.y);
  }
}

static FL_WidgetClass FL_ScrollableClass = {
    .name = "Scrollable",
    .props_size = FL_SIZE_OF(FL_ScrollableProps),
    .state_size = FL_SIZE_OF(FL_ScrollableState),
    .layout = FL_Scrollable_Layout,
    .hit_test = FL_Widget_HitTest_Opaque,
    .on_pointer_event = FL_Scrollable_OnPointerEvent,
};

FL_Widget *FL_Scrollable(const FL_ScrollableProps *props) {
  return FL_Widget_Create(&FL_ScrollableClass, props->key, props);
}

static FL_Vec2 FL_Intersect(FL_f32 begin0, FL_f32 end0, FL_f32 begin1,
                            FL_f32 end1) {
  FL_ASSERT(begin0 <= end0 && begin1 <= end1);

  FL_Vec2 result = (FL_Vec2){0, 0};
  if (FL_Contains(begin1, begin0, end0)) {
    result.x = begin1;
    result.y = FL_Min(end0, end1);
  } else if (FL_Contains(end1, begin0, end0)) {
    result.x = FL_Max(begin0, begin1);
    result.y = end1;
  } else if (FL_Contains(begin0, begin1, end1)) {
    result.x = begin0;
    result.y = FL_Min(end0, end1);
  }
  return result;
}

static FL_i32 GetMinChildIndex(FL_f32 item_extent, FL_f32 scroll_offset) {
  if (item_extent <= 0.0f) {
    return 0;
  }
  FL_f32 actual = scroll_offset / item_extent;
  FL_f32 round = FL_Round(actual);
  if (FL_Abs(actual * item_extent - round * item_extent) <
      FL_PRECISION_ERROR_TOLERANCE) {
    return (FL_i32)round;
  }

  return (FL_i32)FL_Floor(actual);
}

static FL_i32 GetMaxChildIndex(FL_f32 item_extent, FL_f32 scroll_offset) {
  if (item_extent <= 0.0f) {
    return 0;
  }

  FL_f32 actual = scroll_offset / item_extent - 1;
  FL_f32 round = FL_Round(actual);
  if (FL_Abs(actual * item_extent - round * item_extent) <
      FL_PRECISION_ERROR_TOLERANCE) {
    return (FL_i32)FL_Max(0, round);
  }

  return (FL_i32)FL_Max(0, FL_Ceil(actual));
}

static void CalcItemCount(FL_f32 item_extent, FL_f32 scroll_offset,
                          FL_f32 remaining_extent, FL_i32 *first_index,
                          FL_i32 *target_last_index) {
  *first_index = GetMinChildIndex(item_extent, scroll_offset);
  FL_ASSERT(*first_index >= 0);

  FL_f32 target_end_scroll_offset = scroll_offset + remaining_extent;
  if (FL_IsFinite(target_end_scroll_offset)) {
    *target_last_index =
        GetMaxChildIndex(item_extent, target_end_scroll_offset);
  } else {
    *target_last_index = INT32_MAX;
  }
}

static void FL_SliverFixedExtentList_Layout(FL_Widget *widget,
                                            FL_BoxConstraints _constraints) {
  (void)_constraints;
  FL_SliverContext *sliver =
      FL_Widget_GetContext(widget, FL_SliverContext_ID, FL_SliverContext);
  FL_SliverConstraints constraints = sliver->constraints;
  FL_SliverFixedExtentListProps *props =
      FL_Widget_GetProps(widget, FL_SliverFixedExtentListProps);

  FL_f32 scroll_offset = constraints.scroll_offset + constraints.cache_origin;
  FL_ASSERT(scroll_offset >= 0.0f);
  FL_f32 remaining_extent = constraints.remaining_cache_extent;
  FL_ASSERT(remaining_extent >= 0.0f);

  FL_f32 item_extent = props->item_extent;
  FL_i32 first_index;
  FL_i32 target_last_index;
  CalcItemCount(item_extent, scroll_offset, remaining_extent, &first_index,
                &target_last_index);

  FL_i32 child_index = first_index;
  for (FL_i32 i = first_index; i <= target_last_index; i++) {
    if (i >= props->item_count) {
      break;
    }
    FL_Widget *child = props->item_builder.build(props->item_builder.ptr, i);
    if (!child) {
      break;
    }
    FL_Widget_Mount(widget, child);

    FL_BoxConstraints child_constraints =
        FL_BoxConstraints_FromSliverConstraints(constraints, item_extent,
                                                item_extent);
    FL_Widget_Layout(child, child_constraints);
    FL_f32 layout_offset = (FL_f32)child_index * item_extent;
    child->offset = (FL_Vec2){0, layout_offset - scroll_offset};

    child_index += 1;
  }

  FL_f32 leading_scroll_offset = scroll_offset;
  FL_f32 trailing_scroll_offset =
      scroll_offset +
      (FL_f32)(target_last_index - first_index + 1) * item_extent;

  FL_i32 item_count = props->item_count;
  FL_f32 scroll_extent = (FL_f32)item_count * item_extent;
  FL_f32 paint_extent = FL_SliverConstraints_CalcPaintOffset(
      constraints, leading_scroll_offset, trailing_scroll_offset);
  FL_f32 cache_extent = FL_SliverConstraints_CalcCacheOffset(
      constraints, leading_scroll_offset, trailing_scroll_offset);

  widget->size = (FL_Vec2){constraints.cross_axis_extent, paint_extent};

  FL_f32 target_end_scroll_offset_for_paint =
      constraints.scroll_offset + constraints.remaining_paint_extent;
  bool has_target_last_index_for_paint =
      FL_IsFinite(target_end_scroll_offset_for_paint);
  FL_i32 target_last_index_for_paint =
      has_target_last_index_for_paint
          ? GetMaxChildIndex(item_extent, target_end_scroll_offset_for_paint)
          : 0;

  sliver->geometry = (FL_SliverGeometry){
      .scroll_extent = scroll_extent,
      .paint_extent = paint_extent,
      .cache_extent = cache_extent,
      .layout_extent = paint_extent,
      .hit_test_extent = paint_extent,
      .max_paint_extent = scroll_extent,
      .has_visual_overflow = constraints.scroll_offset > 0 ||
                             (has_target_last_index_for_paint &&
                              child_index >= target_last_index_for_paint),
  };
}

static FL_Rect FL_Vec2_Intersect(FL_Vec2 begin0, FL_Vec2 end0, FL_Vec2 begin1,
                                 FL_Vec2 end1) {
  FL_Vec2 x_axis = FL_Intersect(begin0.x, end0.x, begin1.x, end1.x);
  FL_Vec2 y_axis = FL_Intersect(begin0.y, end0.y, begin1.y, end1.y);
  FL_Rect result = {
      x_axis.x,
      x_axis.y,
      y_axis.x,
      y_axis.y,
  };
  return result;
}

static void FL_SliverFixedExtentList_Paint(FL_Widget *widget,
                                           FL_PaintingContext *context,
                                           FL_Vec2 offset) {
  for (FL_Widget *child = widget->first; child; child = child->next) {
    FL_Rect intersection =
        FL_Vec2_Intersect(FL_Vec2_Zero(), widget->size, child->offset,
                          FL_Vec2_Add(child->offset, child->size));
    if (FL_Rect_GetArea(intersection) > 0) {
      FL_Widget_Paint(child, context, FL_Vec2_Add(offset, child->offset));
    }
  }
}

static FL_WidgetClass FL_SliverFixedExtentListClass = {
    .name = "SliverFixedExtentList",
    .props_size = FL_SIZE_OF(FL_SliverFixedExtentListProps),
    .layout = FL_SliverFixedExtentList_Layout,
    .paint = FL_SliverFixedExtentList_Paint,
};

FL_Widget *FL_SliverFixedExtentList(
    const FL_SliverFixedExtentListProps *props) {
  return FL_Widget_Create(&FL_SliverFixedExtentListClass, props->key, props);
}

FL_Widget *FL_ListView(const FL_ListViewProps *props) {
  return FL_Scrollable(&(FL_ScrollableProps){
      // .axis_direction = FL_AxisDirection_Down,
      // .cross_axis_direction = FL_AxisDirection_Right,
      .scroll = props->scroll,
      .slivers = FL_WidgetList_Make((FL_Widget *[]){
          FL_SliverFixedExtentList(&(FL_SliverFixedExtentListProps){
              .item_extent = props->item_extent,
              .item_count = props->item_count,
              .item_builder = props->item_builder,
          }),
          0}),
  });
}

void FL_Viewport_Init(void) {
  FL_SliverContext_ID = FL_Context_Register();
  FL_ScrollNotification_ID = FL_Notification_Register();
}
