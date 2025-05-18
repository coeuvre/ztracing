#ifndef FLICK_H
#define FLICK_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#define FL_PLATFORM_WINDOWS 1
#endif

#ifndef NDEBUG
#define FL_DEBUG_BUILD 1
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define FL_COMPILER_MSVC 1
#elif defined(__clang__)
#define FL_COMPILER_CLANG 1
#endif

#ifdef FL_COMPILER_CLANG
#pragma clang diagnostic ignored "-Wgnu-alignof-expression"
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
#endif

#if FL_COMPILER_MSVC
#define FL_ALWAYS_INLINE __forceinline
#else
#define FL_ALWAYS_INLINE __attribute__((always_inline))
#endif

#define FL_TYPE_OF(x) __typeof__(x)

typedef int32_t FL_i32;
typedef uint32_t FL_u32;
typedef ptrdiff_t FL_isize;
typedef size_t FL_usize;

typedef float FL_f32;
typedef double FL_f64;

#include <stdio.h>


#ifdef FL_COMPILER_MSVC
#define FL_DEBUG_BREAK() __debugbreak()
#define FL_UNREACHABLE __assume(0)
#else
#define FL_DEBUG_BREAK() __builtin_trap()
#define FL_UNREACHABLE __builtin_unreachable()
#endif

#define FL_ASSERTF(c, f, ...)                                               \
  do {                                                                      \
    if (!(c)) {                                                             \
      fprintf(stderr, "%s:%d: " f "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
      FL_DEBUG_BREAK();                                                     \
    }                                                                       \
  } while (0)

#define FL_ASSERT(c) FL_ASSERTF(c, #c)

#if FL_DEBUG_BUILD
#define FL_DEBUG_ASSERT(c) FL_ASSERT(c)
#define FL_DEBUG_ASSERT_F(c, f, ...) FL_ASSERTF(c, f, ##__VA_ARGS__)
#else
#define FL_DEBUG_ASSERT(c)
#define FL_DEBUG_ASSERT_F(c, f, ...)
#endif
#ifndef FL_OPTIONAL_H
#define FL_OPTIONAL_H

#include <stdbool.h>

#define FL_OPTIONAL_TYPE(Name, Type)                \
  typedef struct Name {                             \
    Type value;                                     \
    bool present;                                   \
  } Name;                                           \
                                                    \
  static inline Name Type##_Some(Type value) {      \
    return (Name){.value = value, .present = true}; \
  }                                                 \
                                                    \
  static inline Name Type##_None(void) { return (Name){.present = false}; }

#endif  // FL_OPTIONAL_H

#include <math.h>
#include <stdbool.h>


#define FL_INFINITY INFINITY

#define FL_PRECISION_ERROR_TOLERANCE 1e-5f

static inline bool FL_IsInfinite(FL_f32 a) { return a == FL_INFINITY; }

static inline bool FL_IsFinite(FL_f32 a) { return a != FL_INFINITY; }

static inline bool FL_IsNaN(FL_f32 a) { return isnan(a); }

static inline FL_f32 FL_Min(FL_f32 a, FL_f32 b) { return a < b ? a : b; }

static inline FL_f32 FL_Max(FL_f32 a, FL_f32 b) { return a >= b ? a : b; }

static inline FL_f32 FL_Abs(FL_f32 a) { return fabsf(a); }

static inline FL_f32 FL_Floor(FL_f32 a) { return floorf(a); }

static inline FL_f32 FL_Ceil(FL_f32 a) { return ceilf(a); }

static inline FL_f32 FL_Round(FL_f32 a) { return roundf(a); }

static inline FL_f32 FL_Clamp(FL_f32 a, FL_f32 min, FL_f32 max) {
  return FL_Max(FL_Min(a, max), min);
}

static inline FL_f32 FL_Sqrt(FL_f32 a) { return sqrtf(a); }

static inline FL_f32 FL_Cos(FL_f32 a) { return cosf(a); }

static inline FL_f32 FL_Sin(FL_f32 a) { return sinf(a); }

static inline FL_f32 FL_Exp(FL_f32 a) { return expf(a); }

static inline bool FL_Contains(FL_f32 val, FL_f32 begin, FL_f32 end) {
  return begin <= val && val < end;
}

FL_OPTIONAL_TYPE(FL_f32o, FL_f32)

typedef struct FL_Vec2 {
  FL_f32 x;
  FL_f32 y;
} FL_Vec2;

static inline FL_Vec2 FL_Vec2_Zero(void) { return (FL_Vec2){.x = 0, .y = 0}; }

static inline FL_Vec2 FL_Vec2_Add(FL_Vec2 a, FL_Vec2 b) {
  return (FL_Vec2){a.x + b.x, a.y + b.y};
}

static inline FL_Vec2 FL_Vec2_Sub(FL_Vec2 a, FL_Vec2 b) {
  return (FL_Vec2){a.x - b.x, a.y - b.y};
}

static inline FL_f32 FL_Vec2_GetLenSquared(FL_Vec2 a) {
  return a.x * a.x + a.y * a.y;
}

static inline FL_f32 FL_Vec2_GetLen(FL_Vec2 a) {
  return FL_Sqrt(FL_Vec2_GetLenSquared(a));
}

static inline FL_Vec2 FL_Vec2_Max(FL_Vec2 a, FL_Vec2 b) {
  return (FL_Vec2){FL_Max(a.x, b.x), FL_Max(a.y, b.y)};
}

static inline bool FL_Vec2_IsFinite(FL_Vec2 a) {
  return FL_IsFinite(a.x) && FL_IsFinite(a.y);
}

static inline bool FL_Vec2_Contains(FL_Vec2 val, FL_Vec2 begin, FL_Vec2 end) {
  return FL_Contains(val.x, begin.x, end.x) &&
         FL_Contains(val.y, begin.y, end.y);
}

typedef struct FL_Trans2 {
  FL_f32 m11;
  FL_f32 m12;
  FL_f32 m21;
  FL_f32 m22;
  FL_f32 tx;
  FL_f32 ty;
} FL_Trans2;

static inline FL_Trans2 FL_Trans2_Identity(void) {
  return (FL_Trans2){1, 0, 0, 1, 0, 0};
}

static inline FL_Trans2 FL_Trans2_Offset(FL_f32 x, FL_f32 y) {
  return (FL_Trans2){1, 0, 0, 1, x, y};
}

static inline FL_Trans2 FL_Trans2_Scale(FL_f32 x, FL_f32 y) {
  return (FL_Trans2){x, 0, 0, y, 0, 0};
}

static inline FL_Trans2 FL_Trans2_Rotate(FL_f32 rad) {
  FL_f32 cos = FL_Cos(rad);
  FL_f32 sin = FL_Sin(rad);
  return (FL_Trans2){cos, -sin, sin, cos, 0, 0};
}

static inline FL_Trans2 FL_Trans2_Dot(FL_Trans2 a, FL_Trans2 b) {
  return (FL_Trans2){
      a.m11 * b.m11 + a.m12 * b.m21,      a.m11 * b.m12 + a.m12 * b.m22,
      a.m21 * b.m11 + a.m22 * a.m21,      a.m21 * b.m12 + a.m22 * b.m22,
      a.m11 * b.tx + a.m12 * b.ty + a.tx, a.m21 * b.tx + a.m22 * b.ty + a.ty,
  };
}

static inline FL_Vec2 FL_Trans2_DotVec2(FL_Trans2 a, FL_Vec2 b) {
  return (FL_Vec2){
      a.m11 * b.x + a.m12 * b.y + a.tx,
      a.m21 * b.x + a.m22 * b.y + a.ty,
  };
}

typedef struct FL_Rect {
  FL_f32 left;
  FL_f32 right;
  FL_f32 top;
  FL_f32 bottom;
} FL_Rect;

static inline FL_Rect FL_Rect_FromMinSize(FL_Vec2 min, FL_Vec2 size) {
  FL_Vec2 max = FL_Vec2_Add(min, size);
  return (FL_Rect){min.x, max.x, min.y, max.y};
}

static FL_f32 FL_Rect_GetArea(FL_Rect rect) {
  return (rect.right - rect.left) * (rect.bottom - rect.top);
}

#include <stdalign.h>
#include <stddef.h>
#include <string.h>


#define FL_COUNT_OF(a) (FL_isize)(sizeof(a) / sizeof((a)[0]))

typedef struct FL_AllocatorOps {
  void *(*alloc)(void *ctx, FL_isize size);
  void (*free)(void *ctx, void *ptr, FL_isize size);
} FL_AllocatorOps;

typedef struct FL_Allocator {
  void *ptr;
  const FL_AllocatorOps *ops;
} FL_Allocator;

FL_Allocator FL_Allocator_GetDefault(void);

FL_ALWAYS_INLINE static inline void *FL_Allocator_Alloc(FL_Allocator a,
                                                        FL_isize size) {
  return a.ops->alloc(a.ptr, size);
}

FL_ALWAYS_INLINE static inline void FL_Allocator_Free(FL_Allocator a, void *ptr,
                                                      FL_isize size) {
  a.ops->free(a.ptr, ptr, size);
}

typedef struct FL_MemoryBlock FL_MemoryBlock;
struct FL_MemoryBlock {
  FL_MemoryBlock *prev;
  FL_MemoryBlock *next;

  void *state;
  char *begin;
};

typedef struct FL_Arena {
  char *begin;
  char *end;
} FL_Arena;

typedef struct FL_ArenaOptions {
  FL_Allocator allocator;
  FL_isize page_size;
} FL_ArenaOptions;

FL_Arena *FL_Arena_Create(const FL_ArenaOptions *opts);

void FL_Arena_Destroy(FL_Arena *arena);

void FL_Arena_Reset(FL_Arena *arena);

FL_MemoryBlock *FL_Arena_GetMemoryBlock(FL_Arena *arena);

FL_Allocator FL_Arena_GetAllocator(FL_Arena *arena);

void *FL_Arena_Push(FL_Arena *arena, FL_isize size, FL_isize alignment);

/**
 * Pop `size` bytes from the arena.
 *
 * @return the `top` pointer after the pop.
 */
void *FL_Arena_Pop(FL_Arena *arena, FL_isize size);

#define FL_Arena_PushStruct(arena, S) \
  ((S *)FL_Arena_Push(arena, sizeof(S), alignof(S)))

#define FL_Arena_PushArray(arena, S, n) \
  ((S *)FL_Arena_Push(arena, sizeof(S) * (n), alignof(S)))

static inline void *FL_Arena_Dup(FL_Arena *arena, const void *src,
                                 FL_isize size, FL_isize alignment) {
  void *dst = FL_Arena_Push(arena, size, alignment);
  memcpy(dst, src, size);
  return dst;
}

#include <stdalign.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>


typedef struct FL_Str {
  char *ptr;
  FL_isize len;
} FL_Str;

static inline FL_Str FL_Str_Zero(void) { return (FL_Str){0, 0}; }

#define FL_STR_C(s) (FL_Str){(s), FL_COUNT_OF(s) - 1}

static inline bool FL_Str_IsEmpty(FL_Str s) { return s.len == 0; }

static inline FL_Str FL_Str_Dup(FL_Str s, FL_Arena *arena) {
  if (FL_Str_IsEmpty(s)) {
    return FL_Str_Zero();
  }
  return (FL_Str){FL_Arena_Dup(arena, s.ptr, s.len, alignof(char)), s.len};
}

FL_Str FL_Str_FormatV(FL_Arena *arena, const char *format, va_list ap);

FL_Str FL_Str_Format(FL_Arena *arena, const char *format, ...);

#include <stdbool.h>
#include <stdint.h>


#define FL_BUTTON_PRIMARY UINT32_C(0x01)
#define FL_BUTTON_SECONDARY UINT32_C(0x02)
#define FL_BUTTON_TERTIARY UINT32_C(0x04)

#define FL_MOUSE_BUTTON_PRIMARY FL_BUTTON_PRIMARY
#define FL_MOUSE_BUTTON_SECONDARY FL_BUTTON_SECONDARY
#define FL_MOUSE_BUTTON_TERTIARY FL_BUTTON_TERTIARY
#define FL_MOUSE_BUTTON_BACK UINT32_C(0x8)
#define FL_MOUSE_BUTTON_FORWARD UINT32_C(0x10)

typedef enum FL_PointerEventType {
  FL_PointerEventType_Unknown,

  /**
   * A pointer comes into contact with the screen (for touch pointers), or has
   * its button pressed (for mouse pointers) at this widget's location.
   */
  FL_PointerEventType_Down,

  /**
   * A pointer that triggered an FL_PointerEventType_Down changes position.
   */
  FL_PointerEventType_Move,

  /** A pointer that triggered an FL_PointerEventType_Down is no longer in
     contact with the screen. */
  FL_PointerEventType_Up,

  /** A pointer that triggered an FL_PointerEventType_Down is no longer
     directed towards this receiver. */
  FL_PointerEventType_Cancel,

  /** A pointer that has not triggered an FL_PointerEventType_Down changes
     position. */
  FL_PointerEventType_Hover,
  /** A pointer has entered this widget, with or without button pressed. */

  FL_PointerEventType_Enter,

  /** A pointer has exited this widget, with or without button pressed. */
  FL_PointerEventType_Exit,

  FL_PointerEventType_Scroll,
} FL_PointerEventType;

typedef struct FL_PointerEvent {
  FL_PointerEventType type;

  /**
   * Unique identifier for the pointer, not reused. Changes for each new
   * pointer down event.
   */
  FL_i32 pointer;

  /**
   * Bit field of FL_BUTTON_* constants that is pressed when the event is
   * generated.
   */
  FL_u32 button;

  /**
   * Coordinate of the position of the pointer, in logical pixels in the global
   * coordinate space.
   */
  FL_Vec2 position;

  /**
   * The transformation used to transform this event from the global coordinate
   * space into the coordinate space of the event receiver.
   */
  FL_Trans2 transform;

  /**
   * The position transformed into the event receiver's local coordinate
   * system.
   */
  FL_Vec2 local_position;

  FL_Vec2 delta;
} FL_PointerEvent;

FL_OPTIONAL_TYPE(FL_PointerEventO, FL_PointerEvent)

struct FL_Widget;

/** Returns true if the widget should handle the event. */
bool FL_PointerEventResolver_Register(struct FL_Widget *widget);

typedef struct FL_GestureArenaMemberOps {
  /** Called when this member wins the arena for the given pointer id. */
  void (*accept)(void *ctx, FL_i32 pointer);
  /** Called when this member loses the arena for the given pointer id.*/
  void (*reject)(void *ctx, FL_i32 pointer);
} FL_GestureArenaMemberOps;

typedef struct FL_GestureArenaEntry FL_GestureArenaEntry;

/**
 * Adds a new member (e.g., gesture recognizer) to the arena.
 */
FL_GestureArenaEntry *FL_GestureArena_Add(FL_i32 pointer,
                                          FL_GestureArenaMemberOps *ops,
                                          void *ctx);

/**
 * Updates the state of the member in the arena.
 */
void FL_GestureArena_Update(FL_GestureArenaEntry *entry, void *ctx);

/** Reject or accept a gesture recognizer. */
void FL_GestureArena_Resolve(FL_GestureArenaEntry *entry, bool accepted);

/**
 * Prevents the arena from being swept.
 *
 * Typically, a winner is chosen in an arena after all the other
 * `FLPointerUpEvent` processing. If a recognizer wishes to delay
 * resolving an arena past `FLPointerUpEvent`, the recognizer can `hold` the
 * arena open using this function. To release such a hold and let the arena
 * resolve, call `FL_GestureArena_Release`.
 */
void FL_GestureArena_Hold(FL_GestureArenaEntry *entry);

/**
 * Releases a hold, allowing the arena to be swept.
 *
 * If a sweep was attempted on a held arena, the sweep will be done
 * on release.
 */
void FL_GestureArena_Release(FL_GestureArenaEntry *entry);

#include <stdbool.h>


typedef struct FL_BoxConstraints {
  FL_f32 min_width;
  FL_f32 max_width;
  FL_f32 min_height;
  FL_f32 max_height;
} FL_BoxConstraints;

FL_OPTIONAL_TYPE(FL_BoxConstraintsO, FL_BoxConstraints)

/** Creates box constraints that is respected only by the given size.*/
static inline FL_BoxConstraints FL_BoxConstraints_Tight(FL_f32 width,
                                                        FL_f32 height) {
  return (FL_BoxConstraints){width, width, height, height};
}

/** Creates box constraints that require the given width. */
static inline FL_BoxConstraints FL_BoxConstraints_TightWidth(FL_f32 width) {
  return (FL_BoxConstraints){width, width, 0, FL_INFINITY};
}

/** Creates box constraints that require the given height. */
static inline FL_BoxConstraints FL_BoxConstraints_TightHeight(FL_f32 height) {
  return (FL_BoxConstraints){0, FL_INFINITY, height, height};
}

/**
 * Returns the width that both satisfies the constraints and is as close as
 * possible to the given width.
 */
static inline FL_f32 FL_BoxConstraints_ConstrainWidth(
    FL_BoxConstraints constraints, FL_f32 width) {
  return FL_Clamp(width, constraints.min_width, constraints.max_width);
}

/**
 * Returns the height that both satisfies the constraints and is as close as
 * possible to the given height.
 */
static inline FL_f32 FL_BoxConstraints_ConstrainHeight(
    FL_BoxConstraints constraints, FL_f32 height) {
  return FL_Clamp(height, constraints.min_height, constraints.max_height);
}

/** Returns the size that both satisfies the constraints and is as close as
 * possible to the given size.
 */
static inline FL_Vec2 FL_BoxConstraints_Constrain(FL_BoxConstraints constraints,
                                                  FL_Vec2 size) {
  return (FL_Vec2){
      FL_BoxConstraints_ConstrainWidth(constraints, size.x),
      FL_BoxConstraints_ConstrainHeight(constraints, size.y),
  };
}

/** The biggest size that satisfies the constraints. */
static inline FL_Vec2 FL_BoxConstraints_GetBiggest(
    FL_BoxConstraints constraints) {
  return (FL_Vec2){
      FL_BoxConstraints_ConstrainWidth(constraints, FL_INFINITY),
      FL_BoxConstraints_ConstrainHeight(constraints, FL_INFINITY),
  };
}

/** The smallest size that satisfies the constraints. */
static inline FL_Vec2 FL_BoxConstraints_GetSmallest(
    FL_BoxConstraints constraints) {
  return (FL_Vec2){
      FL_BoxConstraints_ConstrainWidth(constraints, 0),
      FL_BoxConstraints_ConstrainHeight(constraints, 0),
  };
}

static inline FL_BoxConstraints FL_BoxConstraints_Flip(
    FL_BoxConstraints constraints) {
  return (FL_BoxConstraints){
      constraints.min_height,
      constraints.max_height,
      constraints.min_width,
      constraints.max_width,
  };
}

static inline bool FL_BoxConstraints_HasBoundedWidth(
    FL_BoxConstraints constraints) {
  return constraints.max_width < FL_INFINITY;
}

static inline bool FL_BoxConstraints_HasBoundedHeight(
    FL_BoxConstraints constraints) {
  return constraints.max_height < FL_INFINITY;
}

/**
 * Returns new box constraints that respect the given constraints while being
 * as close as possible to the original constraints.
 */
static inline FL_BoxConstraints FL_BoxConstraints_Enforce(
    FL_BoxConstraints constraints, FL_BoxConstraints enforcement) {
  return (FL_BoxConstraints){
      FL_Clamp(constraints.min_width, enforcement.min_width,
               enforcement.max_width),
      FL_Clamp(constraints.max_width, enforcement.min_width,
               enforcement.max_width),
      FL_Clamp(constraints.min_height, enforcement.min_height,
               enforcement.max_height),
      FL_Clamp(constraints.max_height, enforcement.min_height,
               enforcement.max_height),
  };
}

static inline FL_BoxConstraints FL_BoxConstraints_Loosen(
    FL_BoxConstraints constraints) {
  return (FL_BoxConstraints){
      0,
      constraints.max_width,
      0,
      constraints.max_height,
  };
}

static inline bool FL_BoxConstraints_IsTight(FL_BoxConstraints constraints) {
  return constraints.min_width >= constraints.max_width &&
         constraints.min_height >= constraints.max_height;
}

/** Creates box constraints that require the given width or height.*/
static inline FL_BoxConstraints FL_BoxConstraints_TightFor(FL_f32o width,
                                                           FL_f32o height) {
  return (FL_BoxConstraints){
      width.present ? width.value : 0,
      width.present ? width.value : FL_INFINITY,
      height.present ? height.value : 0,
      height.present ? height.value : FL_INFINITY,
  };
}

/**
 * Returns new box constraints with a tight width and/or height as close to
 * the given width and height as possible while still respecting the original
 * box constraints.
 */
static inline FL_BoxConstraints FL_BoxConstraints_Tighten(
    FL_BoxConstraints constraints, FL_f32o width, FL_f32o height) {
  return (FL_BoxConstraints){
      width.present
          ? FL_Clamp(width.value, constraints.min_width, constraints.max_width)
          : constraints.min_width,
      width.present
          ? FL_Clamp(width.value, constraints.min_width, constraints.max_width)
          : constraints.max_width,
      height.present ? FL_Clamp(height.value, constraints.min_height,
                                constraints.max_height)
                     : constraints.min_height,
      height.present ? FL_Clamp(height.value, constraints.min_height,
                                constraints.max_height)
                     : constraints.max_height,
  };
}

/** Returns new box constraints that are smaller by the given edge dimensions.
 */
static inline FL_BoxConstraints FL_BoxConstraints_Deflate(
    FL_BoxConstraints constraints, FL_f32 x, FL_f32 y) {
  FL_f32 deflated_min_width = FL_Max(0, constraints.min_width - x);
  FL_f32 deflated_min_height = FL_Max(0, constraints.min_height - y);
  return (FL_BoxConstraints){
      deflated_min_width,
      FL_Max(deflated_min_width, constraints.max_width - x),
      deflated_min_height,
      FL_Max(deflated_min_height, constraints.max_height - y),
  };
}

typedef enum FL_AxisDirection {
  FL_AxisDirection_Up,
  FL_AxisDirection_Down,
  FL_AxisDirection_Left,
  FL_AxisDirection_Right,
} FL_AxisDirection;

typedef enum FL_GrowthDirection {
  FL_GrowthDirection_Forward,
  FL_GrowthDirection_Reverse,
} FL_GrowthDirection;

typedef enum FL_ScrollDirection {
  FL_ScrollDirection_Idle,
  FL_ScrollDirection_Forward,
  FL_ScrollDirection_Reverse,
} FL_ScrollDirection;

typedef struct FL_SliverConstraints {
  /**
   * The direction in which the `scroll_offset` and `remaining_paint_extent`
   * increase.
   */
  FL_AxisDirection axis_direction;

  /**
   * The direction in which the contents of slivers are ordered, relative to the
   * `axis_direction`.
   */
  FL_GrowthDirection growth_direction;

  /**
   * The direction in which the user is attempting to scroll, relative to the
   * `axis_direction` and `growth_direction`.
   */
  FL_ScrollDirection scroll_direction;

  /** The scroll offset, in this sliver's coordinate system, that corresponds to
   * the earliest visible part of this sliver in the `axis_direction`.
   */
  FL_f32 scroll_offset;

  /**
   * The scroll distance that has been consumed by all slivers that came before
   * this sliver.
   */
  FL_f32 preceeding_scroll_extent;

  /**
   * The number of points from where the points corresponding to the
   * `scroll_offset` will be painted up to the first point that has not yet
   * been painted on by an earlier sliver, in the `axis_direction`.
   */
  FL_f32 overlap;

  /**
   * The number of points of content that the sliver should consider providing.
   * (Providing more pixels than this is inefficient.)
   */
  FL_f32 remaining_paint_extent;

  /** The number of points in the cross-axis. */
  FL_f32 cross_axis_extent;

  /** The direction in which children should be placed in the cross axis. */
  FL_AxisDirection cross_axis_direction;

  /** The number of points the viewport can display in the main axis. */
  FL_f32 main_axis_extent;

  /** Where the cache area starts relative to the `scroll_offset`. */
  FL_f32 cache_origin;

  /**
   * Describes how much content the sliver should provide starting from the
   * `cache_origin`.
   */
  FL_f32 remaining_cache_extent;
} FL_SliverConstraints;

static inline FL_f32 FL_SliverConstraints_CalcPaintOffset(
    FL_SliverConstraints constraints, FL_f32 from, FL_f32 to) {
  FL_f32 a = constraints.scroll_offset;
  FL_f32 b = constraints.scroll_offset + constraints.remaining_paint_extent;
  return FL_Clamp(FL_Clamp(to, a, b) - FL_Clamp(from, a, b), 0,
                  constraints.remaining_paint_extent);
}

static inline FL_f32 FL_SliverConstraints_CalcCacheOffset(
    FL_SliverConstraints constraints, FL_f32 from, FL_f32 to) {
  FL_f32 a = constraints.scroll_offset + constraints.cache_origin;
  FL_f32 b = constraints.scroll_offset + constraints.remaining_cache_extent;
  return FL_Clamp(FL_Clamp(to, a, b) - FL_Clamp(from, a, b), 0,
                  constraints.remaining_cache_extent);
}

typedef enum FL_Axis {
  FL_Axis_Horizontal,
  FL_Axis_Vertical,
} FL_Axis;

static inline FL_Axis FL_Axis_FromAxisDirection(FL_AxisDirection self) {
  switch (self) {
    case FL_AxisDirection_Up:
    case FL_AxisDirection_Down: {
      return FL_Axis_Vertical;
    } break;

    case FL_AxisDirection_Left:
    case FL_AxisDirection_Right: {
      return FL_Axis_Horizontal;
    } break;

    default: {
      FL_UNREACHABLE;
    } break;
  }
}

static inline FL_BoxConstraints FL_BoxConstraints_FromSliverConstraints(
    FL_SliverConstraints constraints, FL_f32 min_extent, FL_f32 max_extent) {
  FL_Axis axis = FL_Axis_FromAxisDirection(constraints.axis_direction);
  switch (axis) {
    case FL_Axis_Horizontal: {
      return (FL_BoxConstraints){
          min_extent,
          max_extent,
          constraints.cross_axis_extent,
          constraints.cross_axis_extent,
      };
    } break;

    case FL_Axis_Vertical: {
      return (FL_BoxConstraints){
          constraints.cross_axis_extent,
          constraints.cross_axis_extent,
          min_extent,
          max_extent,
      };
    } break;

    default: {
      FL_UNREACHABLE;
    } break;
  }
}


typedef struct FL_Color {
  FL_f32 r;
  FL_f32 g;
  FL_f32 b;
  FL_f32 a;
} FL_Color;

FL_OPTIONAL_TYPE(FL_ColorO, FL_Color)

typedef struct FL_TextMetrics {
  FL_f32 width;
  FL_f32 font_bounding_box_ascent;
  FL_f32 font_bounding_box_descent;
} FL_TextMetrics;

typedef struct FL_Canvas {
  void *ctx;

  /** Saves a copy of the current transform and clip on the save stack. */
  void (*save)(void *ctx);

  /**
   * Pops the current save stack, if there is anything to pop. Otherwise, does
   * nothing.
   */
  void (*restore)(void *ctx);

  /**
   * Reduces the clip region to the intersection of the current clip and the
   * given rectangle.
   */
  void (*clip_rect)(void *ctx, FL_Rect rect);

  void (*fill_rect)(void *ctx, FL_Rect rect, FL_Color color);

  void (*stroke_rect)(void *ctx, FL_Rect rect, FL_Color color,
                      FL_f32 line_width);

  FL_TextMetrics (*measure_text)(void *ctx, FL_Str text, FL_f32 font_size);

  void (*fill_text)(void *ctx, FL_Str text, FL_f32 x, FL_f32 y,
                    FL_f32 font_size, FL_Color color);
} FL_Canvas;

static inline void FL_Canvas_Save(FL_Canvas *canvas) {
  canvas->save(canvas->ctx);
}

static inline void FL_Canvas_Restore(FL_Canvas *canvas) {
  canvas->restore(canvas->ctx);
}

static inline void FL_Canvas_ClipRect(FL_Canvas *canvas, FL_Rect rect) {
  canvas->clip_rect(canvas->ctx, rect);
}

static inline void FL_Canvas_FillRect(FL_Canvas *canvas, FL_Rect rect,
                                      FL_Color color) {
  canvas->fill_rect(canvas->ctx, rect, color);
}

static inline void FL_Canvas_StrokeRect(FL_Canvas *canvas, FL_Rect rect,
                                        FL_Color color, FL_f32 line_width) {
  canvas->stroke_rect(canvas->ctx, rect, color, line_width);
}

static inline FL_TextMetrics FL_Canvas_MeasureText(FL_Canvas *canvas,
                                                   FL_Str text,
                                                   FL_f32 font_size) {
  return canvas->measure_text(canvas->ctx, text, font_size);
}

static inline void FL_Canvas_FillText(FL_Canvas *canvas, FL_Str text, FL_f32 x,
                                      FL_f32 y, FL_f32 font_size,
                                      FL_Color color) {
  canvas->fill_text(canvas->ctx, text, x, y, font_size, color);
}

typedef struct FL_PaintingContext {
  FL_Canvas *canvas;
} FL_PaintingContext;

#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


typedef struct FL_Widget FL_Widget;

typedef struct FL_StructSize {
  FL_isize size;
  FL_isize alignment;
} FL_StructSize;

#define FL_SIZE_OF(S) \
  (FL_StructSize) { sizeof(S), alignof(S) }

struct FL_HitTestContext;
typedef struct FL_HitTestContext FL_HitTestContext;

typedef struct FL_SliverGeometry {
  /**
   * The total scrollable extent that this sliver has content for.
   *
   * This is the amount of scrolling the user needs to do to get from the
   * beginning of this sliver to the end of this sliver.
   */
  FL_f32 scroll_extent;

  /**
   * The visual location of the first visible part of this sliver relative to
   * its layout position.
   */
  FL_f32 paint_origin;

  /**
   * The amount of currently visible visual space that was taken by the sliver
   * to render the subset of the sliver that covers all or part of the
   * `FL_SliverConstraints.remaining_paint_extent` in the current viewport.
   */
  FL_f32 paint_extent;

  /**
   * The distance from the first visible part of this sliver to the first
   * visible part of the next sliver, assuming the next sliver's
   * `FL_SliverConstraints.scroll_offset` is zero.
   */
  FL_f32 layout_extent;

  /**
   * The total paint extent that this sliver would be able to provide if the
   * `FL_SliverConstraints.remaining_paint_extent` was infinite.
   */
  FL_f32 max_paint_extent;

  /**
   * The distance from where this sliver started painting to the bottom of
   * where it should accept hits.
   */
  FL_f32 hit_test_extent;

  /**
   * If any slivers have visual overflow, the viewport will apply a clip to its
   * children.
   */
  bool has_visual_overflow;

  /**
   * If this is non-zero after, the scroll offset will be adjusted by the
   * parent and then the entire layout of the parent will be rerun.
   */
  FL_f32 scroll_offset_correction;

  /**
   * How many points the sliver has consumed in the
   * `FL_SliverConstraints.remaining_cache_extent`.
   */
  FL_f32 cache_extent;
} FL_SliverGeometry;

void FL_HitTest_AddWidget(FL_HitTestContext *context, FL_Widget *widget);

typedef FL_i32 FL_NotificationID;

FL_NotificationID FL_Notification_Register(void);

typedef struct FL_WidgetClass {
  const char *name;
  FL_StructSize props_size;
  FL_StructSize state_size;

  // -- lifetime ---------------------------------------------------------------
  void (*mount)(FL_Widget *widget);
  void (*unmount)(FL_Widget *widget);

  // -- layout -----------------------------------------------------------------
  void (*layout)(FL_Widget *widget, FL_BoxConstraints constraints);

  // -- paint ------------------------------------------------------------------
  void (*paint)(FL_Widget *widget, FL_PaintingContext *context, FL_Vec2 offset);

  // -- event handling ---------------------------------------------------------
  bool (*hit_test)(FL_Widget *widget, FL_HitTestContext *context);
  void (*on_pointer_event)(FL_Widget *widget, FL_PointerEvent event);

  /**
   * Returns true to cancel the notification bubbling. Returns false to allow
   * the notification to continue to be dispatched to further ancestors.
   */
  bool (*on_notification)(FL_Widget *widget, FL_NotificationID id, void *data);
} FL_WidgetClass;

typedef struct FL_Key {
  uint64_t hash;
} FL_Key;

static inline FL_Key FL_Key_Zero(void) { return (FL_Key){.hash = 0}; }

static inline bool FL_Key_IsEqual(FL_Key a, FL_Key b) {
  return a.hash == b.hash;
}

typedef FL_i32 FL_ContextID;

typedef struct FL_ContextData {
  FL_isize len;
  void *ptr;
} FL_ContextData;

struct FL_Context;

typedef struct FL_WidgetListEntry FL_WidgetListEntry;
struct FL_WidgetListEntry {
  FL_WidgetListEntry *prev;
  FL_WidgetListEntry *next;
  FL_Widget *widget;
};

typedef struct FL_WidgetList {
  FL_WidgetListEntry *first;
  FL_WidgetListEntry *last;
} FL_WidgetList;

/**
 * Constructs `FL_WidgetList` from an array of `FL_Widget *`.
 *
 * The last element of the array must be `null`.
 */
FL_WidgetList FL_WidgetList_Make(FL_Widget *widgets[]);

struct FL_Widget {
  const FL_WidgetClass *klass;
  FL_Key key;

  FL_Widget *parent;
  FL_Widget *first;
  FL_Widget *last;
  FL_Widget *prev;
  FL_Widget *next;
  FL_isize child_count;

  void *build;
  FL_Widget *last_child_of_link;

  /**
   * The widget's counterpart from the other frame. If this widget is from the
   * previous frame, link is from the current frame, and vice-versa.
   */
  FL_Widget *link;
  void *props;
  void *state;
  struct FL_Context *context;

  /**
   * The offset at which to paint the child in the parent's coordinate system.
   *
   * The value is initialized to the offset of the same widget from last frame
   * before layout.
   */
  FL_Vec2 offset;

  /**
   * The size of this box computed during layout.
   *
   * The value is initialized to the size of it's link from last frame before
   * layout.
   */
  FL_Vec2 size;
};

FL_Widget *FL_Widget_Create_(const FL_WidgetClass *klass, FL_Key key,
                             const void *props, FL_StructSize props_size);

#define FL_Widget_Create(klass, key, props) \
  FL_Widget_Create_(klass, key, props, FL_SIZE_OF(*(props)))

void FL_Widget_Mount(FL_Widget *widget, FL_Widget *child);

FL_ALWAYS_INLINE static inline void *FL_Widget_GetProps_(FL_Widget *widget,
                                                         FL_isize props_size) {
  FL_ASSERTF(widget->klass->props_size.size == props_size,
             "%s: expected props size %td, but got %td", widget->klass->name,
             widget->klass->props_size.size, props_size);
  return widget->props;
}

#define FL_Widget_GetProps(widget, S) \
  (S *)FL_Widget_GetProps_(widget, sizeof(S))

FL_ALWAYS_INLINE static inline void *FL_Widget_GetState_(FL_Widget *widget,
                                                         FL_isize state_size) {
  FL_ASSERTF(widget->klass->state_size.size == state_size,
             "%s: expected state size %td, but got %td", widget->klass->name,
             widget->klass->state_size.size, state_size);
  FL_ASSERTF(state_size > 0 && widget->state, "%s: state is not available",
             widget->klass->name);
  return widget->state;
}

#define FL_Widget_GetState(widget, S) \
  (S *)FL_Widget_GetState_(widget, sizeof(S))

void *FL_Widget_GetContext_(FL_Widget *widget, FL_ContextID key, FL_isize size);

#define FL_Widget_GetContext(self, key, S) \
  (S *)FL_Widget_GetContext_(self, key, sizeof(S))

void *FL_Widget_SetContext_(FL_Widget *self, FL_ContextID key,
                            FL_StructSize size);

#define FL_Widget_SetContext(self, key, S) \
  (S *)FL_Widget_SetContext_(self, key, FL_SIZE_OF(S))

void FL_Widget_Layout_Default(FL_Widget *widget, FL_BoxConstraints constraints);

FL_ALWAYS_INLINE static inline void FL_Widget_Layout(
    FL_Widget *widget, FL_BoxConstraints constraints) {
  if (widget->klass->layout) {
    widget->klass->layout(widget, constraints);
  } else {
    FL_Widget_Layout_Default(widget, constraints);
  }
}

bool FL_Widget_HitTest(FL_Widget *widget, FL_HitTestContext *result);

bool FL_Widget_HitTest_DeferToChild(FL_Widget *widget,
                                    FL_HitTestContext *context);

bool FL_Widget_HitTest_Transluscent(FL_Widget *widget,
                                    FL_HitTestContext *context);

bool FL_Widget_HitTest_Opaque(FL_Widget *widget, FL_HitTestContext *context);

typedef enum FL_HitTestBehaviour {
  FL_HitTestBehaviour_DeferToChild,
  FL_HitTestBehaviour_Translucent,
  FL_HitTestBehaviour_Opaque,
} FL_HitTestBehaviour;

bool FL_Widget_HitTest_ByBehaviour(FL_Widget *widget,
                                   FL_HitTestContext *context,
                                   FL_HitTestBehaviour behaviour);

void FL_Widget_Paint_Default(FL_Widget *widget, FL_PaintingContext *context,
                             FL_Vec2 offset);

FL_ALWAYS_INLINE static inline void FL_Widget_Paint(FL_Widget *widget,
                                                    FL_PaintingContext *context,
                                                    FL_Vec2 offset) {
  if (widget->klass->paint) {
    widget->klass->paint(widget, context, offset);
  } else {
    FL_Widget_Paint_Default(widget, context, offset);
  }
}

FL_ALWAYS_INLINE static inline void FL_Widget_OnPointerEvent(
    FL_Widget *widget, FL_PointerEvent event) {
  if (widget->klass->on_pointer_event) {
    widget->klass->on_pointer_event(widget, event);
  }
}

void FL_Widget_SendNotification(FL_Widget *widget, FL_NotificationID id,
                                void *data);

FL_f32 FL_Widget_GetDeltaTime(FL_Widget *widget);

FL_f32 FL_Widget_AnimateFast(FL_Widget *widget, FL_f32 value, FL_f32 target);

FL_Arena *FL_Widget_GetArena(FL_Widget *widget);

#include <stddef.h>
#include <stdint.h>


typedef struct FL_InitOptions {
  FL_Allocator allocator;
  FL_Canvas canvas;
} FL_InitOptions;

void FL_Init(const FL_InitOptions *opts);

void FL_Deinit(void);

FL_ContextID FL_Context_Register(void);

void FL_OnMouseMove(FL_Vec2 pos);

void FL_OnMouseButtonDown(FL_Vec2 pos, FL_u32 button);

void FL_OnMouseButtonUp(FL_Vec2 pos, FL_u32 button);

void FL_OnMouseScroll(FL_Vec2 pos, FL_Vec2 delta);

typedef struct FL_RunOptions {
  FL_Widget *widget;
  FL_Rect viewport;
  FL_f32 delta_time;
} FL_RunOptions;

void FL_Run(const FL_RunOptions *opts);

FL_TextMetrics FL_MeasureText(FL_Str text, FL_f32 font_size);

FL_Str FL_Format(const char *format, ...);


typedef struct FL_ColoredBoxProps {
  FL_Key key;
  FL_Color color;
  FL_Widget *child;
} FL_ColoredBoxProps;

/**
 * A widget that paints its area with a specified color and then draws its
 * child on top of that color.
 */
FL_Widget *FL_ColoredBox(const FL_ColoredBoxProps *props);

typedef struct FL_ConstrainedBoxProps {
  FL_Key key;
  FL_BoxConstraints constraints;
  FL_Widget *child;
} FL_ConstrainedBoxProps;

/**
 * A widget that imposes additional constraints on its child.
 */
FL_Widget *FL_ConstrainedBox(const FL_ConstrainedBoxProps *props);

typedef struct FL_LimitedBoxProps {
  FL_Key key;
  FL_f32 max_width;
  FL_f32 max_height;
  FL_Widget *child;
} FL_LimitedBoxProps;

/**
 * A box that limits its size only when it's unconstrained.
 */
FL_Widget *FL_LimitedBox(const FL_LimitedBoxProps *props);

/**
 * A point within a rectangle.
 *
 * `(0.0, 0.0)` represents the center of the rectangle. The distance from -1.0
 * to +1.0 is the distance from one side of the rectangle to the other side of
 * the rectangle. Therefore, 2.0 units horizontally (or vertically) is
 * equivalent to the width (or height) of the rectangle.
 *
 * `(-1.0, -1.0)` represents the top left of the rectangle.
 *
 * `(1.0, 1.0)` represents the bottom right of the rectangle.
 *
 * `(0.0, 3.0)` represents a point that is horizontally centered with respect to
 * the rectangle and vertically below the bottom of the rectangle by the height
 * of the rectangle.
 *
 * `(0.0, -0.5)` represents a point that is horizontally centered with respect
 * to the rectangle and vertically half way between the top edge and the center.
 *
 * `(x, y)` in a rectangle with height h and width w describes the point (x *
 * w/2 + w/2, y * h/2 + h/2) in the coordinate system of the rectangle.
 */
typedef struct FL_Alignment {
  FL_f32 x;
  FL_f32 y;
} FL_Alignment;

FL_OPTIONAL_TYPE(FL_AlignmentO, FL_Alignment)

static inline FL_Vec2 FL_Alignment_AlignOffset(FL_Alignment alignment,
                                               FL_Vec2 offset) {
  FL_f32 center_x = offset.x / 2.0f;
  FL_f32 center_y = offset.y / 2.0f;
  return (FL_Vec2){
      center_x + alignment.x * center_x,
      center_y + alignment.y * center_y,
  };
}

static inline FL_Alignment FL_Alignment_TopLeft(void) {
  return (FL_Alignment){-1, -1};
}

static inline FL_Alignment FL_Alignment_TopCenter(void) {
  return (FL_Alignment){0, -1};
}

static inline FL_Alignment FL_Alignment_TopRight(void) {
  return (FL_Alignment){1, -1};
}

static inline FL_Alignment FL_Alignment_CenterLeft(void) {
  return (FL_Alignment){-1, 0};
}

static inline FL_Alignment FL_Alignment_Center(void) {
  return (FL_Alignment){0, 0};
}

static inline FL_Alignment FL_Alignment_CenterRight(void) {
  return (FL_Alignment){1, 0};
}

static inline FL_Alignment FL_Alignment_BottomLeft(void) {
  return (FL_Alignment){-1, 1};
}

static inline FL_Alignment FL_Alignment_BottomCenter(void) {
  return (FL_Alignment){0, 1};
}

static inline FL_Alignment FL_Alignment_BottomRight(void) {
  return (FL_Alignment){1, 1};
}

typedef struct FL_AlignProps {
  FL_Key key;
  FL_Alignment alignment;
  FL_f32o width;
  FL_f32o height;
  FL_Widget *child;
} FL_AlignProps;

/**
 * A widget that aligns its child within itself and optionally sizes itself
 * based on the child's size.
 */
FL_Widget *FL_Align(const FL_AlignProps *props);

typedef struct FL_CenterProps {
  FL_Key key;
  FL_f32o width;
  FL_f32o height;
  FL_Widget *child;
} FL_CenterProps;

/**
 * A widget that centers its child within itself.
 */
FL_Widget *FL_Center(const FL_CenterProps *props);

typedef struct FL_EdgeInsets {
  FL_f32 start;
  FL_f32 end;
  FL_f32 top;
  FL_f32 bottom;
} FL_EdgeInsets;

FL_OPTIONAL_TYPE(FL_EdgeInsetsO, FL_EdgeInsets)

static inline FL_EdgeInsets FL_EdgeInsets_All(FL_f32 val) {
  return (FL_EdgeInsets){val, val, val, val};
}

static inline FL_EdgeInsets FL_EdgeInsets_Zero(void) {
  return FL_EdgeInsets_All(0);
}

static inline FL_EdgeInsets FL_EdgeInsets_Symmetric(FL_f32 x, FL_f32 y) {
  return (FL_EdgeInsets){x, x, y, y};
}

typedef struct FL_PaddingProps {
  FL_Key key;
  FL_EdgeInsets padding;
  FL_Widget *child;
} FL_PaddingProps;

/**
 * A widget that insets its child by the given padding.
 */
FL_Widget *FL_Padding(const FL_PaddingProps *props);

typedef struct FL_ContainerProps {
  FL_Key key;

  FL_f32o width;
  FL_f32o height;

  FL_AlignmentO alignment;
  FL_EdgeInsetsO padding;
  /** The color to paint behind the children. */
  FL_ColorO color;

  // TODO: Decoration

  /** Additional constraints to apply to the children. */
  FL_BoxConstraintsO constraints;
  /** Empty space to surround the decoration and children. */
  FL_EdgeInsetsO margin;
  FL_Widget *child;
} FL_ContainerProps;

/**
 * A convenience widget that combines common painting, positioning, and sizing
 * widgets.
 */
FL_Widget *FL_Container(const FL_ContainerProps *props);

typedef struct FL_UnconstrainedBoxProps {
  FL_Key key;
  FL_Alignment alignment;
  FL_Widget *child;
} FL_UnconstrainedBoxProps;

/**
 * A widget that imposes no constraints on its child, allowing it to render at
 * its "natural" size.
 */
FL_Widget *FL_UnconstrainedBox(const FL_UnconstrainedBoxProps *props);

void FL_Basic_Init(void);

#include <stdint.h>


typedef enum FL_FlexFit {
  /**
   * The child is forced to fill the available space.
   */
  FL_FlexFit_Tight,
  /**
   * The child can be at most as large as the available space (but is allowed
   * to be smaller).
   */
  FL_FlexFit_Loose,
} FL_FlexFit;

typedef enum FL_MainAxisAlignment {
  FL_MainAxisAlignment_Start,
  FL_MainAxisAlignment_End,
  FL_MainAxisAlignment_Center,
  FL_MainAxisAlignment_SpaceBetween,
  FL_MainAxisAlignment_SpaceAround,
  FL_MainAxisAlignment_SpaceEvenly,
} FL_MainAxisAlignment;

typedef enum FL_CrossAxisAlignment {
  FL_CrossAxisAlignment_Center,
  FL_CrossAxisAlignment_Start,
  FL_CrossAxisAlignment_End,
  FL_CrossAxisAlignment_Stretch,
  FL_CrossAxisAlignment_Baseline,
} FL_CrossAxisAlignment;

typedef enum FL_MainAxisSize {
  FL_MainAxisSize_Max,
  FL_MainAxisSize_Min,
} FL_MainAxisSize;

typedef struct FL_FlexProps {
  FL_Key key;
  FL_Axis direction;
  FL_MainAxisAlignment main_axis_alignment;
  FL_MainAxisSize main_axis_size;
  FL_CrossAxisAlignment cross_axis_alignment;
  // TODO: text direction
  // TODO: vertical direction
  // TODO: text baseline
  FL_f32 spacing;
  FL_WidgetList children;
} FL_FlexProps;

/**
 * A widget that displays its children in a one-dimensional array.
 */
FL_Widget *FL_Flex(const FL_FlexProps *props);

typedef struct FL_ColumnProps {
  FL_Key key;
  FL_MainAxisAlignment main_axis_alignment;
  FL_MainAxisSize main_axis_size;
  FL_CrossAxisAlignment cross_axis_alignment;
  FL_f32 spacing;
  FL_WidgetList children;
} FL_ColumnProps;

/**
 * A widget that displays its children in a vertical array.
 */
FL_Widget *FL_Column(const FL_ColumnProps *props);

typedef struct FL_RowProps {
  FL_Key key;
  FL_MainAxisAlignment main_axis_alignment;
  FL_MainAxisSize main_axis_size;
  FL_CrossAxisAlignment cross_axis_alignment;
  FL_f32 spacing;
  FL_WidgetList children;
} FL_RowProps;

/**
 * A widget that displays its children in a horizontal array.
 */
FL_Widget *FL_Row(const FL_RowProps *props);

typedef struct FL_FlexibleProps {
  FL_Key key;
  FL_i32 flex;
  FL_FlexFit fit;
  FL_Widget *child;
} FL_FlexibleProps;

/**
 * A widget that controls how a child of a `Row`, `Column`, or `Flex` flexes.
 */
FL_Widget *FL_Flexible(const FL_FlexibleProps *props);

typedef struct FL_ExpandedProps {
  FL_Key key;
  FL_i32 flex;
  FL_Widget *child;
} FL_ExpandedProps;

/**
 * A widget that expands a child of a `Row`, `Column`, or `Flex` so that the
 * child fills the available space.
 */
FL_Widget *FL_Expanded(const FL_ExpandedProps *props);

void FL_Flex_Init(void);


typedef void FL_PointerListenerCallback(void *ctx, FL_PointerEvent event);

typedef struct FL_PointerListenerProps {
  FL_Key key;
  FL_HitTestBehaviour behaviour;
  void *context;
  FL_PointerListenerCallback *on_down;
  FL_PointerListenerCallback *on_move;
  FL_PointerListenerCallback *on_up;
  FL_PointerListenerCallback *on_enter;
  FL_PointerListenerCallback *on_hover;
  FL_PointerListenerCallback *on_exit;
  FL_PointerListenerCallback *on_cancel;
  FL_PointerListenerCallback *on_scroll;
  FL_Widget *child;
} FL_PointerListenerProps;

FL_Widget *FL_PointerListener(const FL_PointerListenerProps *props);

typedef struct FL_GestureDetails {
  FL_Vec2 local_position;
  FL_Vec2 delta;
} FL_GestureDetails;

FL_OPTIONAL_TYPE(FL_GestureDetailsO, FL_GestureDetails)

typedef void FL_GestureCallback(void *ctx, FL_GestureDetails details);

typedef struct FL_GestureDetectorProps {
  FL_Key key;
  FL_HitTestBehaviour behaviour;

  void *context;

  FL_GestureCallback *tap_down;
  FL_GestureCallback *tap_up;
  FL_GestureCallback *tap;
  FL_GestureCallback *tap_cancel;

  /**
   * A pointer has contacted the screen with a primary button and might begin
   * to move.
   */
  FL_GestureCallback *drag_down;

  /**
   * A pointer has contacted the screen with a primary button and has begun to
   * move.
   */
  FL_GestureCallback *drag_start;

  /** A pointer that is in contact with the screen with a primary button and
   * moving has moved again. */
  FL_GestureCallback *drag_update;

  /**
   * A pointer that was previously in contact with the screen with a primary
   * button and moving is no longer in contact with the screen and was moving at
   * a specific velocity when it stopped contacting the screen.
   */
  FL_GestureCallback *drag_end;

  /** The pointer that previously triggered [drag_down] did not complete. */
  FL_GestureCallback *drag_cancel;

  FL_Widget *child;
} FL_GestureDetectorProps;

FL_Widget *FL_GestureDetector(const FL_GestureDetectorProps *props);


typedef enum FL_StackFit {
  FL_StackFit_Loose,
  FL_StackFit_Expand,
  FL_StackFit_Passthrough,
} FL_StackFit;

typedef struct FL_StackProps {
  FL_Key key;
  FL_StackFit fit;
  FL_WidgetList children;
} FL_StackProps;

/**
 * A widget that positions its children relative to the edges of its box.
 */
FL_Widget *FL_Stack(const FL_StackProps *props);

typedef struct FL_PositionedProps {
  FL_Key key;
  FL_f32o left;
  FL_f32o right;
  FL_f32o top;
  FL_f32o bottom;
  FL_f32o width;
  FL_f32o height;
  FL_Widget *child;
} FL_PositionedProps;

/**
 * A widget that controls where a child of a `Stack` is positioned.
 */
FL_Widget *FL_Positioned(const FL_PositionedProps *props);

void FL_Stack_Init(void);


typedef struct FL_TextStyle {
  FL_ColorO color;
  FL_f32o font_size;
} FL_TextStyle;

FL_OPTIONAL_TYPE(FL_TextStyleO, FL_TextStyle)

typedef struct FL_TextProps {
  FL_Key key;
  /** Text must be valid until next build. */
  FL_Str text;
  FL_TextStyleO style;
} FL_TextProps;

/**
 * A run of text with a single style.
 */
FL_Widget *FL_Text(const FL_TextProps *props);

#include <stdint.h>


extern FL_ContextID FL_SliverContext_ID;

typedef struct FL_SliverContext {
  FL_SliverConstraints constraints;
  FL_SliverGeometry geometry;

  /**
   * The position of the child relative to the zero scroll offset. In a typical
   * list, this does not change as the parent is scrolled.
   */
  FL_f32 layout_offset;
} FL_SliverContext;

typedef struct FL_ViewportOffset {
  /**
   * The number of points to offset the children in the opposite of the axis
   * direction.
   */
  FL_f32 points;

  FL_ScrollDirection scroll_direction;
} FL_ViewportOffset;

typedef struct FL_ViewportProps {
  FL_Key key;

  /** The direction in which the `offset` increases. */
  FL_AxisDirection axis_direction;

  /** The direction in which child should be laid out in the cross axis. */
  FL_AxisDirection cross_axis_direction;

  /** The relative position of the zero scroll offset. */
  FL_f32 anchor;

  /** Which part of the content inside the viewport should be visible. */
  FL_ViewportOffset offset;

  /**
   * The viewport has an area before and after the visible area to cache items
   * that are about to become visible when the user scrolls.
   */
  FL_f32 cache_extent;

  // TODO: clip and center

  FL_WidgetList slivers;
} FL_ViewportProps;

/**
 * A widget through which a portion of larger content can be viewed. The
 * children of FL_Viewport must be slivers.
 */
FL_Widget *FL_Viewport(const FL_ViewportProps *props);

typedef struct FL_ScrollableProps {
  FL_Key key;
  // TODO: Support other directions
  // FL_AxisDirection axis_direction;
  // FL_AxisDirection cross_axis_direction;
  // FL_f32 cache_extent;

  /** The pointer must be valid longer than the widget. */
  FL_f32 *scroll;

  // TODO: ViewportBuilder
  FL_WidgetList slivers;
} FL_ScrollableProps;

/**
 * A widget that manages scrolling in one dimension and informs the
 * `FL_Viewport` through which the content is viewed.
 */
FL_Widget *FL_Scrollable(const FL_ScrollableProps *props);

typedef struct FL_ScrollbarProps {
  FL_Key key;
  FL_Widget *child;
} FL_ScrollbarProps;

FL_Widget *FL_Scrollbar(const FL_ScrollbarProps *props);

typedef struct FL_ItemBuilder {
  void *ptr;
  FL_Widget *(*build)(void *ctx, FL_i32 item_index);
} FL_ItemBuilder;

typedef struct FL_SliverFixedExtentListProps {
  FL_Key key;
  FL_f32 item_extent;
  FL_i32 item_count;
  FL_ItemBuilder item_builder;
} FL_SliverFixedExtentListProps;

/**
 * A sliver that places multiple box children with the same main axis extent in
 * a linear array.
 */
FL_Widget *FL_SliverFixedExtentList(const FL_SliverFixedExtentListProps *props);

typedef struct FL_ListViewProps {
  FL_Key key;
  FL_f32 item_extent;
  FL_i32 item_count;
  FL_ItemBuilder item_builder;
  /** The pointer must be valid longer than the widget. */
  FL_f32 *scroll;
} FL_ListViewProps;

FL_Widget *FL_ListView(const FL_ListViewProps *props);

void FL_Viewport_Init(void);
#endif // FLICK_H
