#include "src/ui.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "src/assert.h"
#include "src/draw.h"
#include "src/list.h"
#include "src/log.h"
#include "src/math.h"
#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"

typedef struct BoxHashSlot BoxHashSlot;
struct BoxHashSlot {
  BoxHashSlot *prev;
  BoxHashSlot *next;
  UIBox *first;
  UIBox *last;
};

typedef struct UIBoxCache {
  u32 total_box_count;
  // Free list for boxes
  UIBox *first_free_box;
  // Hash slots for box hash table
  u32 box_hash_slots_count;
  BoxHashSlot *box_hash_slots;
  // Linked list for non-empty box hash slots
  BoxHashSlot *first_hash_slot;
  BoxHashSlot *last_hash_slot;
} UIBoxCache;

static void InitUIBoxCache(UIBoxCache *cache, Arena *arena) {
  cache->box_hash_slots_count = 4096;
  cache->box_hash_slots =
      PushArray(arena, BoxHashSlot, cache->box_hash_slots_count);
}

static UIBox *GetOrPushBox(UIBoxCache *cache, Arena *arena) {
  UIBox *result;
  if (cache->first_free_box) {
    result = cache->first_free_box;
    cache->first_free_box = result->next;
    ZeroMemory(result, sizeof(*result));
  } else {
    result = PushArray(arena, UIBox, 1);
    ++cache->total_box_count;
  }
  return result;
}

static UIBox *GetBoxByKey(UIBoxCache *cache, UIKey key) {
  UIBox *result = 0;
  if (!IsEqualUIKey(key, UIKeyZero())) {
    BoxHashSlot *slot =
        &cache->box_hash_slots[key.hash % cache->box_hash_slots_count];
    for (UIBox *box = slot->first; box; box = box->hash_next) {
      if (IsEqualUIKey(box->key, key)) {
        result = box;
        break;
      }
    }
  }
  return result;
}

static UIBox *GetOrPushBoxByKey(UIBoxCache *cache, Arena *arena, UIKey key) {
  UIBox *box = GetBoxByKey(cache, key);
  if (!box) {
    box = GetOrPushBox(cache, arena);
    box->key = key;
    BoxHashSlot *slot =
        &cache->box_hash_slots[key.hash % cache->box_hash_slots_count];
    if (!slot->first) {
      APPEND_DOUBLY_LINKED_LIST(cache->first_hash_slot, cache->last_hash_slot,
                                slot, prev, next);
    }
    APPEND_DOUBLY_LINKED_LIST(slot->first, slot->last, box, hash_prev,
                              hash_next);
  }
  return box;
}

static void GarbageCollectBoxes(UIBoxCache *cache, u64 build_index) {
  // Garbage collect boxes that were not touched by last frame or don't have
  // key.
  for (BoxHashSlot *slot = cache->first_hash_slot; slot;) {
    BoxHashSlot *next_slot = slot->next;

    for (UIBox *box = slot->first; box;) {
      UIBox *next = box->hash_next;
      if (IsEqualUIKey(box->key, UIKeyZero()) ||
          box->last_touched_build_index < build_index) {
        REMOVE_DOUBLY_LINKED_LIST(slot->first, slot->last, box, hash_prev,
                                  hash_next);

        box->next = cache->first_free_box;
        cache->first_free_box = box;
      }
      box = next;
    }

    if (!slot->first) {
      REMOVE_DOUBLY_LINKED_LIST(cache->first_hash_slot, cache->last_hash_slot,
                                slot, prev, next);
    }

    slot = next_slot;
  }
}

typedef struct UIMouseButtonState {
  b8 is_down;
  b8 transition_count;
} UIMouseButtonState;

typedef struct UIMouseInput {
  Vec2 pos;
  Vec2 wheel;
  UIMouseButtonState buttons[kUIMouseButtonCount];

  UIBox *hovering;
  UIBox *pressed[kUIMouseButtonCount];
  Vec2 pressed_pos[kUIMouseButtonCount];
  UIBox *holding[kUIMouseButtonCount];
  UIBox *clicked[kUIMouseButtonCount];
  UIBox *scrolling;
  Vec2 scroll_delta;
} UIMouseInput;

typedef struct UIInput {
  f32 dt;
  UIMouseInput mouse;
} UIInput;

typedef struct UIState {
  Arena *arena;

  UIBoxCache cache;
  UIInput input;

  Arena *build_arena[2];
  u64 build_index;

  // per-frame info
  UIBuild next_build;
  f32 content_scale;
  Vec2 screen_size;
  UIBox *root;
  UIBox *current;
  UIBuildError *first_error;
  UIBuildError *last_error;
} UIState;

thread_local UIState t_ui_state;

static UIState *GetUIState(void) {
  UIState *state = &t_ui_state;
  if (!state->arena) {
    state->arena = AllocArena();
    InitUIBoxCache(&state->cache, state->arena);
    state->build_arena[0] = AllocArena();
    state->build_arena[1] = AllocArena();

    state->input.dt = 1.0f / 60.0f;  // Assume 60 FPS by default.
    state->input.mouse.pos = V2(-1, -1);
  }
  return state;
}

static Arena *GetBuildArena(UIState *state) {
  Arena *arena =
      state->build_arena[state->build_index % ARRAY_COUNT(state->build_arena)];
  return arena;
}

static void PushUIBuildErrorF(UIState *state, const char *fmt, ...) {
  Arena *arena = GetBuildArena(state);
  UIBuildError *error = PushArray(arena, UIBuildError, 1);

  va_list ap;
  va_start(ap, fmt);
  error->message = PushStr8FV(arena, fmt, ap);
  va_end(ap);

  APPEND_DOUBLY_LINKED_LIST(state->first_error, state->last_error, error, prev,
                            next);
}

static inline b32 IsMouseButtonPressed(UIState *state, UIMouseButton button) {
  UIMouseButtonState *mouse_button_state = &state->input.mouse.buttons[button];
  b32 result =
      mouse_button_state->is_down && mouse_button_state->transition_count > 0;
  return result;
}

static inline b32 IsMouseButtonClicked(UIState *state, UIMouseButton button) {
  UIMouseButtonState *mouse_button_state = &state->input.mouse.buttons[button];
  b32 result =
      !mouse_button_state->is_down && mouse_button_state->transition_count > 0;
  return result;
}

void OnUIMousePos(Vec2 pos) {
  UIState *state = GetUIState();

  state->input.mouse.pos = pos;
}

void OnUIMouseButtonUp(Vec2 pos, UIMouseButton button) {
  UIState *state = GetUIState();

  state->input.mouse.pos = pos;

  UIMouseButtonState *mouse_button_state = &state->input.mouse.buttons[button];
  if (mouse_button_state->is_down) {
    mouse_button_state->is_down = 0;
    mouse_button_state->transition_count += 1;
  }
}

void OnUIMouseButtonDown(Vec2 pos, UIMouseButton button) {
  UIState *state = GetUIState();

  state->input.mouse.pos = pos;

  UIMouseButtonState *mouse_button_state = &state->input.mouse.buttons[button];
  if (!mouse_button_state->is_down) {
    mouse_button_state->is_down = 1;
    mouse_button_state->transition_count += 1;
  }
}

void OnUIMouseWheel(Vec2 delta) {
  UIState *state = GetUIState();

  state->input.mouse.wheel = delta;
}

void SetUIDeltaTime(f32 dt) {
  UIState *state = GetUIState();

  state->input.dt = dt;
}

f32 GetUIDeltaTime(void) {
  UIState *state = GetUIState();
  f32 result = state->input.dt;
  return result;
}

void BeginUIFrame(Vec2 screen_size, f32 content_scale) {
  UIState *state = GetUIState();

  GarbageCollectBoxes(&state->cache, state->build_index);

  state->build_index += 1;
  state->content_scale = content_scale;
  state->screen_size = screen_size;
  state->root = 0;
  state->current = 0;
  state->first_error = state->last_error = 0;

  ResetArena(GetBuildArena(state));
}

static inline f32 GetEdgeInsetsSize(UIEdgeInsets edge_insets, Axis2 axis) {
  // TODO: Handle text direction.
  f32 result;
  if (axis == kAxis2X) {
    result = edge_insets.start + edge_insets.end;
  } else {
    result = edge_insets.top + edge_insets.bottom;
  }
  return result;
}

static inline f32 GetEdgeInsetsStart(UIEdgeInsets edge_insets, Axis2 axis) {
  // TODO: Handle text direction.
  f32 result;
  if (axis == kAxis2X) {
    result = edge_insets.start;
  } else {
    result = edge_insets.top;
  }
  return result;
}

static inline f32 GetEdgeInsetsEnd(UIEdgeInsets edge_insets, Axis2 axis) {
  // TODO: Handle text direction.
  f32 result;
  if (axis == kAxis2X) {
    result = edge_insets.end;
  } else {
    result = edge_insets.bottom;
  }
  return result;
}

static void AlignMainAxis(UIBox *box, Axis2 axis, f32 padding_start,
                          f32 padding_end, UIMainAxisAlign align,
                          f32 children_size) {
  // TODO: Handle margin.
  f32 free = GetItemVec2(box->computed.size, axis) - children_size -
             padding_start - padding_end;
  f32 pos = padding_start;
  switch (align) {
    case kUIMainAxisAlignStart: {
    } break;

    case kUIMainAxisAlignCenter: {
      pos += free / 2.0;
    } break;

    case kUIMainAxisAlignEnd: {
      pos += free;
    } break;

    default: {
      UNREACHABLE;
    } break;
  }

  for (UIBox *child = box->first; child; child = child->next) {
    pos += GetEdgeInsetsStart(child->build.margin, axis);
    SetItemVec2(&child->computed.rel_pos, axis, pos);
    pos += GetItemVec2(child->computed.size, axis) +
           GetEdgeInsetsEnd(child->build.margin, axis);
  }
}

static void AlignCrossAxis(UIBox *box, Axis2 axis, f32 padding_start,
                           f32 padding_end, UICrossAxisAlign align) {
  // TODO: Handle margin.
  for (UIBox *child = box->first; child; child = child->next) {
    f32 free = GetItemVec2(box->computed.size, axis) -
               GetItemVec2(child->computed.size, axis) - padding_start -
               padding_end - GetEdgeInsetsSize(child->build.margin, axis);
    switch (align) {
      case kUICrossAxisAlignStart:
      case kUICrossAxisAlignStretch: {
        SetItemVec2(
            &child->computed.rel_pos, axis,
            padding_start + GetEdgeInsetsStart(child->build.margin, axis));
      } break;

      case kUICrossAxisAlignCenter: {
        SetItemVec2(&child->computed.rel_pos, axis,
                    padding_start +
                        GetEdgeInsetsStart(child->build.margin, axis) +
                        free / 2.0f);
      } break;

      case kUICrossAxisAlignEnd: {
        SetItemVec2(&child->computed.rel_pos, axis,
                    padding_start +
                        GetEdgeInsetsStart(child->build.margin, axis) + free);
      } break;

      default: {
        UNREACHABLE;
      } break;
    }
  }
}

static inline b32 ShouldMaxAxis(UIBox *box, int axis, Axis2 main_axis,
                                f32 max_size_axis) {
  // cross axis is always as small as possible
  b32 result = box->build.main_axis_size == kUIMainAxisSizeMax &&
               axis == (int)main_axis && max_size_axis != F32_INFINITY;
  return result;
}

static Vec2 LayoutText(UIState *state, UIBox *box, Vec2 max_size,
                       Axis2 main_axis, Axis2 cross_axis) {
  ASSERT(!IsEmptyStr8(box->build.text));

  // TODO: constraint text size within [(0, 0), max_size]

  // Use pixel unit to measure text
  TextMetrics metrics = GetTextMetricsStr8(
      box->build.text, KUITextSizeDefault * state->content_scale);
  Vec2 text_size_in_pixel = metrics.size;
  Vec2 text_size = MulVec2(text_size_in_pixel, 1.0f / state->content_scale);
  text_size = MinVec2(text_size, max_size);

  Vec2 children_size;
  SetItemVec2(&children_size, main_axis, GetItemVec2(text_size, main_axis));
  SetItemVec2(&children_size, cross_axis, GetItemVec2(text_size, cross_axis));
  return children_size;
}

static void LayoutBox(UIState *state, UIBox *box, Vec2 min_size, Vec2 max_size);

static Vec2 LayoutChildren(UIState *state, UIBox *box, Vec2 max_size,
                           Axis2 main_axis, Axis2 cross_axis) {
  ASSERT(box->first);

  f32 max_main_axis_size = GetItemVec2(max_size, main_axis);
  f32 max_cross_axis_size = GetItemVec2(max_size, cross_axis);

  f32 child_main_axis_size = 0.0f;
  f32 child_cross_axis_size = 0.0f;

  f32 total_flex = 0;
  UIBox *last_flex = 0;

  // First pass: layout non-flex children
  for (UIBox *child = box->first; child; child = child->next) {
    total_flex += child->build.flex;
    if (!child->build.flex) {
      Vec2 this_child_max_size;
      SetItemVec2(&this_child_max_size, main_axis,
                  max_main_axis_size - child_main_axis_size);
      SetItemVec2(&this_child_max_size, cross_axis, max_cross_axis_size);
      Vec2 this_child_min_size = {0};
      if (box->build.cross_axis_align == kUICrossAxisAlignStretch) {
        SetItemVec2(&this_child_min_size, cross_axis,
                    GetItemVec2(this_child_max_size, cross_axis));
      }

      // Leave space for margin
      f32 margin_x = child->build.margin.start + child->build.margin.end;
      f32 margin_y = child->build.margin.top + child->build.margin.bottom;
      this_child_max_size.x = MaxF32(this_child_max_size.x - margin_x, 0);
      this_child_max_size.y = MaxF32(this_child_max_size.y - margin_y, 0);
      LayoutBox(state, child, this_child_min_size, this_child_max_size);

      // Add margin back
      f32 this_child_main_axis_size =
          GetItemVec2(child->computed.size, main_axis) +
          GetEdgeInsetsSize(child->build.margin, main_axis);
      f32 this_child_cross_axis_size =
          GetItemVec2(child->computed.size, cross_axis) +
          GetEdgeInsetsSize(child->build.margin, cross_axis);

      if (max_main_axis_size == kUISizeInfinity &&
          this_child_main_axis_size == kUISizeInfinity) {
        PushUIBuildErrorF(
            state, "Cannot have unbounded content within unbounded constraint");
      }

      child_main_axis_size += this_child_main_axis_size;
      child_cross_axis_size =
          MaxF32(child_cross_axis_size, this_child_cross_axis_size);
    } else {
      last_flex = child;
    }
  }

  ASSERT(ContainsF32IncludingEnd(child_main_axis_size, 0, max_main_axis_size) ||
         child_main_axis_size == kUISizeInfinity);

  // Second pass: layout flex children
  f32 child_main_axis_flex = max_main_axis_size - child_main_axis_size;
  for (UIBox *child = box->first; child; child = child->next) {
    if (child->build.flex) {
      if (max_main_axis_size == kUISizeInfinity) {
        PushUIBuildErrorF(state, "Unbounded constraint doesn't work with flex");
      }

      f32 this_child_max_main_axis_size;
      if (child == last_flex) {
        this_child_max_main_axis_size =
            max_main_axis_size - child_main_axis_size;
      } else {
        this_child_max_main_axis_size =
            ClampF32(child->build.flex / total_flex * child_main_axis_flex, 0,
                     max_main_axis_size - child_main_axis_size);
      }

      // Tight constraint for child
      Vec2 this_child_max_size;
      SetItemVec2(&this_child_max_size, main_axis,
                  this_child_max_main_axis_size);
      SetItemVec2(&this_child_max_size, cross_axis, max_cross_axis_size);
      Vec2 this_child_min_size;
      SetItemVec2(&this_child_min_size, main_axis,
                  this_child_max_main_axis_size);
      if (box->build.cross_axis_align == kUICrossAxisAlignStretch) {
        SetItemVec2(&this_child_min_size, cross_axis, max_cross_axis_size);
      } else {
        SetItemVec2(&this_child_min_size, cross_axis, 0.0f);
      }

      // Leave space for margin
      f32 margin_x = child->build.margin.start + child->build.margin.end;
      f32 margin_y = child->build.margin.top + child->build.margin.bottom;
      this_child_max_size.x = MaxF32(this_child_max_size.x - margin_x, 0);
      this_child_max_size.y = MaxF32(this_child_max_size.y - margin_y, 0);
      LayoutBox(state, child, this_child_min_size, this_child_max_size);

      f32 this_child_main_axis_size = this_child_max_main_axis_size;
      // Add margin back
      f32 this_child_cross_axis_size =
          GetItemVec2(child->computed.size, cross_axis) +
          GetEdgeInsetsSize(child->build.margin, cross_axis);

      child_main_axis_size += this_child_main_axis_size;
      child_cross_axis_size =
          MaxF32(child_cross_axis_size, this_child_cross_axis_size);
    }
  }

  Vec2 children_size = V2(0, 0);
  SetItemVec2(&children_size, main_axis, child_main_axis_size);
  SetItemVec2(&children_size, cross_axis, child_cross_axis_size);
  return children_size;
}

static void LayoutBox(UIState *state, UIBox *box, Vec2 min_size,
                      Vec2 max_size) {
  ASSERTF(ContainsVec2IncludingEnd(min_size, V2(0, 0), max_size),
          "min_size=(%.2f, %.2f), max_size=(%.2f, %.2f)", min_size.x,
          min_size.y, max_size.x, max_size.y);

  box->computed.min_size = min_size;
  box->computed.max_size = max_size;

  Vec2 children_max_size = max_size;
  for (int axis = 0; axis < kAxis2Count; ++axis) {
    f32 min_size_axis = GetItemVec2(min_size, axis);
    f32 max_size_axis = GetItemVec2(max_size, axis);
    f32 build_size_axis = GetItemVec2(box->build.size, axis);
    if (build_size_axis == kUISizeInfinity) {
      // If it's infinity, let children be infinity.
      SetItemVec2(&children_max_size, axis, kUISizeInfinity);
    } else if (build_size_axis != kUISizeUndefined) {
      // If box has specific size, and is not infinity, use that (but also
      // respect the constraint) as constraint for children.
      SetItemVec2(&children_max_size, axis,
                  ClampF32(build_size_axis, min_size_axis, max_size_axis));
    } else {
      // Otherwise, pass down the constraint to children to make them as large
      // as possible
      SetItemVec2(&children_max_size, axis, max_size_axis);
    }
  }
  // Leave space for padding
  children_max_size.x = MaxF32(
      children_max_size.x - (box->build.padding.start + box->build.padding.end),
      0);
  children_max_size.y =
      MaxF32(children_max_size.y -
                 (box->build.padding.top + box->build.padding.bottom),
             0);

  Axis2 main_axis = box->build.main_axis;
  Axis2 cross_axis = (main_axis + 1) % kAxis2Count;
  Vec2 children_size = V2(0, 0);
  if (box->first) {
    children_size =
        LayoutChildren(state, box, children_max_size, main_axis, cross_axis);
  } else if (!IsEmptyStr8(box->build.text)) {
    children_size =
        LayoutText(state, box, children_max_size, main_axis, cross_axis);
  }

  // Size box itself
  for (int axis = 0; axis < kAxis2Count; ++axis) {
    f32 min_size_axis = GetItemVec2(min_size, axis);
    f32 max_size_axis = GetItemVec2(max_size, axis);

    f32 build_size_axis = GetItemVec2(box->build.size, axis);
    if (build_size_axis != kUISizeUndefined) {
      // If box has specific size, use that size but also respect the
      // constraint.
      SetItemVec2(&box->computed.size, axis,
                  ClampF32(build_size_axis, min_size_axis, max_size_axis));
    } else if (ShouldMaxAxis(box, axis, main_axis, max_size_axis)) {
      // If box should maximize this axis, regardless of it's children, do it.
      SetItemVec2(&box->computed.size, axis, max_size_axis);
    } else {
      // Size itself around children
      f32 padding_start_axis = GetEdgeInsetsStart(box->build.padding, axis);
      f32 padding_end_axis = GetEdgeInsetsEnd(box->build.padding, axis);
      f32 children_size_axis = GetItemVec2(children_size, axis);
      f32 content_size_axis =
          children_size_axis + padding_start_axis + padding_end_axis;
      SetItemVec2(&box->computed.size, axis,
                  ClampF32(content_size_axis, min_size_axis, max_size_axis));
    }
  }

  ASSERTF(ContainsVec2IncludingEnd(box->computed.size, min_size, max_size),
          "computed_size=(%.2f, %.2f), min_size=(%.2f, %.2f), max_size=(%.2f, "
          "%.2f)",
          box->computed.size.x, box->computed.size.y, min_size.x, min_size.y,
          max_size.x, max_size.y);

  // Clip if content size exceeds self size.
  box->computed.clip =
      children_size.x + box->build.padding.start + box->build.padding.end >
          box->computed.size.x ||
      children_size.y + box->build.padding.top + box->build.padding.bottom >
          box->computed.size.y;

  AlignMainAxis(
      box, main_axis, GetEdgeInsetsStart(box->build.padding, main_axis),
      GetEdgeInsetsEnd(box->build.padding, main_axis),
      box->build.main_axis_align, GetItemVec2(children_size, main_axis));
  AlignCrossAxis(box, cross_axis,
                 GetEdgeInsetsStart(box->build.padding, cross_axis),
                 GetEdgeInsetsEnd(box->build.padding, cross_axis),
                 box->build.cross_axis_align);
}

static void RenderBox(UIState *state, UIBox *box, Vec2 parent_pos,
                      Rect2 parent_clip_rect) {
  Vec2 min = AddVec2(parent_pos, box->computed.rel_pos);
  Vec2 max = AddVec2(min, box->computed.size);
  Vec2 min_in_pixel = MulVec2(min, state->content_scale);
  Vec2 max_in_pixel = MulVec2(max, state->content_scale);
  box->computed.screen_rect = R2(min, max);

  Rect2 intersection =
      Rect2FromIntersection(parent_clip_rect, box->computed.screen_rect);
  f32 intersection_area = GetRect2Area(intersection);
  if (intersection_area > 0) {
    b32 need_clip = box->computed.clip;
    if (need_clip) {
      PushClipRect(MulVec2(intersection.min, state->content_scale),
                   MulVec2(intersection.max, state->content_scale));
    }

    if (box->build.color.a) {
      DrawRect(min_in_pixel, max_in_pixel, box->build.color);
    }

    // Debug outline
    // DrawRectLine(min_in_pixel, max_in_pixel,
    // ColorU32FromHex(0xFF00FF), 1.0f);

    if (box->first) {
      for (UIBox *child = box->first; child; child = child->next) {
        RenderBox(state, child, min, intersection);
      }
      if (!IsEmptyStr8(box->build.text)) {
        WARN("%s: text content is ignored because it has children",
             box->build.key_str.ptr);
      }
    } else if (!IsEmptyStr8(box->build.text)) {
      // TODO: clip
      DrawTextStr8(min_in_pixel, box->build.text,
                   KUITextSizeDefault * state->content_scale);
    }

    if (need_clip) {
      PopClipRect();
    }
  }
}

#if 1
static void DebugPrintUIR(UIBox *box, u32 level) {
  INFO(
      "%*s %s[id=%s, min_size=(%.2f, %.2f), max_size=(%.2f, %.2f), "
      "build_size=(%.2f, %.2f), size=(%.2f, %.2f), rel_pos=(%.2f, %.2f)]",
      level * 4, "", box->build.tag, box->build.key_str.ptr,
      box->computed.min_size.x, box->computed.min_size.y,
      box->computed.max_size.x, box->computed.max_size.y, box->build.size.x,
      box->build.size.y, box->computed.size.x, box->computed.size.y,
      box->computed.rel_pos.x, box->computed.rel_pos.y);
  for (UIBox *child = box->first; child; child = child->next) {
    DebugPrintUIR(child, level + 1);
  }
}

static void DebugPrintUI(UIState *state) {
  if (state->build_index > 1) {
    if (state->root) {
      DebugPrintUIR(state->root, 0);
    }
    exit(0);
  }
}
#endif

static void ProcessInputR(UIState *state, UIBox *box) {
  for (UIBox *child = box->first; child; child = child->next) {
    ProcessInputR(state, child);
  }

  // Mouse input
  if (!state->input.mouse.hovering && box->build.hoverable &&
      ContainsVec2(state->input.mouse.pos, box->computed.screen_rect.min,
                   box->computed.screen_rect.max)) {
    state->input.mouse.hovering = box;
  }

  for (int button = 0; button < kUIMouseButtonCount; ++button) {
    if (!state->input.mouse.pressed[button] && box->build.clickable[button] &&
        ContainsVec2(state->input.mouse.pos, box->computed.screen_rect.min,
                     box->computed.screen_rect.max) &&
        IsMouseButtonPressed(state, button)) {
      state->input.mouse.pressed[button] = box;
      state->input.mouse.pressed_pos[button] = state->input.mouse.pos;
    }
  }

  if (!state->input.mouse.scrolling && box->build.scrollable &&
      !IsZeroVec2(state->input.mouse.wheel) &&
      ContainsVec2(state->input.mouse.pos, box->computed.screen_rect.min,
                   box->computed.screen_rect.max)) {
    state->input.mouse.scrolling = box;
    state->input.mouse.scroll_delta = state->input.mouse.wheel;
  }
}

static void ProcessInput(UIState *state) {
  state->input.mouse.hovering = 0;
  state->input.mouse.scrolling = 0;
  for (int button = 0; button < kUIMouseButtonCount; ++button) {
    state->input.mouse.pressed[button] = 0;
    state->input.mouse.clicked[button] = 0;
  }

  if (state->root) {
    ProcessInputR(state, state->root);
  }

  for (int button = 0; button < kUIMouseButtonCount; ++button) {
    if (state->input.mouse.pressed[button]) {
      state->input.mouse.holding[button] = state->input.mouse.pressed[button];
    }

    if (IsMouseButtonClicked(state, button)) {
      UIBox *box = state->input.mouse.holding[button];
      if (box &&
          ContainsVec2(state->input.mouse.pos, box->computed.screen_rect.min,
                       box->computed.screen_rect.max)) {
        state->input.mouse.clicked[button] = box;
      }
      state->input.mouse.holding[button] = 0;
    }

    state->input.mouse.buttons[button].transition_count = 0;
  }
  state->input.mouse.wheel = V2(0, 0);
}

void EndUIFrame(void) {
  UIState *state = GetUIState();
  ASSERTF(!state->current, "Mismatched Begin/End calls");

  if (state->root) {
    LayoutBox(state, state->root, state->screen_size, state->screen_size);
    state->root->computed.rel_pos = V2(0, 0);
  }

  ProcessInput(state);

  // DebugPrintUI(state);
}

void RenderUI(void) {
  UIState *state = GetUIState();

  ASSERTF(!state->first_error, "%s", state->first_error->message.ptr);

  if (state->root) {
    RenderBox(state, state->root, V2(0, 0), R2(V2(0, 0), state->screen_size));
  }
}

UIBuildError *GetFirstUIBuildError(void) {
  UIState *state = GetUIState();
  UIBuildError *result = state->first_error;
  return result;
}

UIKey UIKeyZero(void) {
  UIKey result = {0};
  return result;
}

UIKey UIKeyFromHash(u64 hash) {
  UIKey result = {hash};
  return result;
}

UIKey UIKeyFromStr8(UIKey seed, Str8 str) {
  UIKey result = UIKeyZero();
  if (str.len) {
    // djb2 hash function
    u64 hash = seed.hash ? seed.hash : 5381;
    for (usize i = 0; i < str.len; i += 1) {
      // hash * 33 + c
      hash = ((hash << 5) + hash) + str.ptr[i];
    }
    result.hash = hash;
  }
  return result;
}

b32 IsEqualUIKey(UIKey a, UIKey b) {
  b32 result = a.hash == b.hash;
  return result;
}

static UIKey GetFirstNonZeroUIKey(UIBox *box) {
  UIKey result = UIKeyZero();
  if (box) {
    if (!IsEqualUIKey(box->key, UIKeyZero())) {
      result = box->key;
    } else {
      result = GetFirstNonZeroUIKey(box->parent);
    }
  }
  return result;
}

void BeginUIBox(void) {
  UIState *state = GetUIState();

  Str8 key_str = state->next_build.key_str;
  UIBox *parent = state->current;
  UIKey seed = GetFirstNonZeroUIKey(parent);
  UIKey key = UIKeyFromStr8(seed, key_str);
  UIBox *box = GetOrPushBoxByKey(&state->cache, state->arena, key);
  ASSERTF(box->last_touched_build_index < state->build_index,
          "%s is built more than once",
          IsEmptyStr8(key_str) ? "<unknown>" : (char *)key_str.ptr);

  if (parent) {
    APPEND_DOUBLY_LINKED_LIST(parent->first, parent->last, box, prev, next);
  } else {
    ASSERTF(!state->root, "More than one root box provided");
    state->root = box;
  }
  box->parent = parent;
  box->last_touched_build_index = state->build_index;

  // Clear per frame state
  box->first = box->last = 0;
  box->build = state->next_build;
  state->next_build = (UIBuild){0};
  if (!box->build.tag) {
    box->build.tag = "Box";
  }

  state->current = box;
}

void EndUIBoxWithExpectedTag(const char *tag) {
  UIState *state = GetUIState();

  ASSERT(state->current);
  ASSERTF(strcmp(state->current->build.tag, tag) == 0,
          "Mismatched Begin/End calls. Begin with %s, end with %s",
          state->current->build.tag, tag);
  state->current = state->current->parent;
}

UIBox *GetUIRoot(void) {
  UIState *state = GetUIState();
  return state->root;
}

UIBox *GetUIBoxByKey(UIBox *parent, Str8 key_str) {
  UIState *state = GetUIState();
  if (!parent) {
    parent = state->root;
  }
  UIKey seed = GetFirstNonZeroUIKey(parent);
  UIKey key = UIKeyFromStr8(seed, key_str);
  UIBox *result = GetBoxByKey(&state->cache, key);
  return result;
}

UIBox *GetUIBox(UIBox *parent, u32 index) {
  UIBox *result = 0;

  UIState *state = GetUIState();
  if (!parent) {
    result = state->root;
  } else {
    u32 j = 0;
    for (UIBox *child = parent->first; child; child = child->next) {
      if (j++ == index) {
        result = child;
        break;
      }
    }
  }

  return result;
}

UIBox *GetCurrentUIBox(void) {
  UIState *state = GetUIState();
  ASSERT(state->current);
  return state->current;
}

void SetNextUIKey(Str8 key) {
  UIState *state = GetUIState();

  Arena *arena = GetBuildArena(state);
  state->next_build.key_str = PushStr8(arena, key);
}

void SetNextUIKeyF(const char *fmt, ...) {
  UIState *state = GetUIState();

  Arena *arena = GetBuildArena(state);
  va_list ap;
  va_start(ap, fmt);
  state->next_build.key_str = PushStr8FV(arena, fmt, ap);
  va_end(ap);
}

void SetNextUITag(const char *tag) {
  UIState *state = GetUIState();

  state->next_build.tag = tag;
}

void SetNextUIColor(ColorU32 color) {
  UIState *state = GetUIState();

  state->next_build.color = color;
}

void SetNextUISize(Vec2 size) {
  UIState *state = GetUIState();

  state->next_build.size = size;
}

void SetNextUIText(Str8 text) {
  UIState *state = GetUIState();

  Arena *arena = GetBuildArena(state);
  state->next_build.text = PushStr8(arena, text);
}

void SetNextUIMainAxis(Axis2 axis) {
  UIState *state = GetUIState();

  state->next_build.main_axis = axis;
}

void SetNextUIMainAxisSize(UIMainAxisSize main_axis_size) {
  UIState *state = GetUIState();

  state->next_build.main_axis_size = main_axis_size;
}

void SetNextUIMainAxisAlign(UIMainAxisAlign main_axis_align) {
  UIState *state = GetUIState();

  state->next_build.main_axis_align = main_axis_align;
}

void SetNextUICrossAxisAlign(UICrossAxisAlign cross_axis_align) {
  UIState *state = GetUIState();

  state->next_build.cross_axis_align = cross_axis_align;
}

void SetNextUIFlex(f32 flex) {
  UIState *state = GetUIState();

  state->next_build.flex = flex;
}

void SetNextUIPadding(UIEdgeInsets padding) {
  UIState *state = GetUIState();

  state->next_build.padding = padding;
}

void SetNextUIMargin(UIEdgeInsets margin) {
  UIState *state = GetUIState();

  state->next_build.margin = margin;
}

static UIBox *GetUIBoxByNextUIKey(UIState *state) {
  ASSERTF(
      !IsEmptyStr8(state->next_build.key_str),
      "Must assign a key to the next box in order to get computed property");
  UIBox *result = GetUIBoxByKey(state->current, state->next_build.key_str);
  return result;
}

UIComputed GetNextUIComputed(void) {
  UIState *state = GetUIState();
  UIBox *box = GetUIBoxByNextUIKey(state);
  UIComputed result = {0};
  if (box) {
    result = box->computed;
  }
  return result;
}

Vec2 GetNextUIMouseRelPos(void) {
  UIState *state = GetUIState();
  UIBox *box = GetUIBoxByNextUIKey(state);
  Vec2 result = V2(0, 0);
  if (box) {
    result = SubVec2(state->input.mouse.pos, box->computed.screen_rect.min);
  }
  return result;
}

b32 IsNextUIMouseHovering(void) {
  UIState *state = GetUIState();
  state->next_build.hoverable = 1;

  UIBox *box = GetUIBoxByNextUIKey(state);
  b32 result = box && state->input.mouse.hovering == box;
  return result;
}

b32 IsNextUIMouseButtonPressed(UIMouseButton button) {
  UIState *state = GetUIState();
  state->next_build.clickable[button] = 1;

  UIBox *box = GetUIBoxByNextUIKey(state);
  b32 result = box && state->input.mouse.pressed[button] == box;
  return result;
}

b32 IsNextUIMouseButtonDown(UIMouseButton button) {
  UIState *state = GetUIState();
  state->next_build.clickable[button] = 1;

  UIBox *box = GetUIBoxByNextUIKey(state);
  b32 result = box && state->input.mouse.holding[button] == box;
  return result;
}

b32 IsNextUIMouseButtonClicked(UIMouseButton button) {
  UIState *state = GetUIState();
  state->next_build.clickable[button] = 1;

  UIBox *box = GetUIBoxByNextUIKey(state);
  b32 result = box && state->input.mouse.clicked[button] == box;
  return result;
}

b32 IsNextUIMouseButtonDragging(UIMouseButton button, Vec2 *delta) {
  UIState *state = GetUIState();
  state->next_build.clickable[button] = 1;

  UIBox *box = GetUIBoxByNextUIKey(state);
  f32 result = box && state->input.mouse.holding[button] == box;
  if (result && delta) {
    *delta =
        SubVec2(state->input.mouse.pos, state->input.mouse.pressed_pos[button]);
  }
  return result;
}

b32 IsNextUIMouseScrolling(Vec2 *delta) {
  UIState *state = GetUIState();
  state->next_build.scrollable = 1;

  UIBox *box = GetUIBoxByNextUIKey(state);
  f32 result = box && state->input.mouse.scrolling == box;
  if (result && delta) {
    *delta = state->input.mouse.scroll_delta;
  }
  return result;
}
