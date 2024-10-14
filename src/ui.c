#include "src/ui.h"

#include <stdarg.h>
#include <string.h>

#include "src/assert.h"
#include "src/draw.h"
#include "src/list.h"
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
  if (!IsZeroUIKey(key)) {
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
      if (IsZeroUIKey(box->key) ||
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
  Vec2 screen_size;

  UILayer *first_layer;
  UILayer *last_layer;
  UILayer *current_layer;

  UIBuildError *first_error;
  UIBuildError *last_error;
} UIState;

thread_local UIState t_ui_state;

static UIState *GetUIState(void) {
  UIState *state = &t_ui_state;
  ASSERTF(state->arena, "InitUI is not called");
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

void InitUI(void) {
  UIState *state = &t_ui_state;
  ASSERTF(!state->arena, "InitUI called more than once");
  state->arena = AllocArena();
  InitUIBoxCache(&state->cache, state->arena);
  state->build_arena[0] = AllocArena();
  state->build_arena[1] = AllocArena();

  state->input.dt = 1.0f / 60.0f;  // Assume 60 FPS by default.
  state->input.mouse.pos = V2(-1, -1);
}

void QuitUI(void) {
  UIState *state = GetUIState();
  FreeArena(state->build_arena[0]);
  FreeArena(state->build_arena[1]);
  FreeArena(state->arena);
  *state = (UIState){0};
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

void BeginUIFrame(Vec2 screen_size) {
  UIState *state = GetUIState();

  GarbageCollectBoxes(&state->cache, state->build_index);

  state->build_index += 1;
  state->screen_size = screen_size;
  state->first_layer = state->last_layer = state->current_layer = 0;
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
  f32 size_axis = GetItemVec2(box->computed.size, axis);
  f32 free = size_axis - children_size - padding_start - padding_end;
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
    pos += GetEdgeInsetsStart(child->props.margin, axis);
    if (pos < 0 || pos > size_axis) {
      box->computed.clip = 1;
    }
    SetItemVec2(&child->computed.rel_pos, axis, pos);
    pos += GetItemVec2(child->computed.size, axis) +
           GetEdgeInsetsEnd(child->props.margin, axis);
  }
}

static void AlignCrossAxis(UIBox *box, Axis2 axis, f32 padding_start,
                           f32 padding_end, UICrossAxisAlign align) {
  // TODO: Handle margin.
  for (UIBox *child = box->first; child; child = child->next) {
    f32 free = GetItemVec2(box->computed.size, axis) -
               GetItemVec2(child->computed.size, axis) - padding_start -
               padding_end - GetEdgeInsetsSize(child->props.margin, axis);
    switch (align) {
      case kUICrossAxisAlignStart:
      case kUICrossAxisAlignStretch: {
        SetItemVec2(
            &child->computed.rel_pos, axis,
            padding_start + GetEdgeInsetsStart(child->props.margin, axis));
      } break;

      case kUICrossAxisAlignCenter: {
        SetItemVec2(&child->computed.rel_pos, axis,
                    padding_start +
                        GetEdgeInsetsStart(child->props.margin, axis) +
                        free / 2.0f);
      } break;

      case kUICrossAxisAlignEnd: {
        SetItemVec2(&child->computed.rel_pos, axis,
                    padding_start +
                        GetEdgeInsetsStart(child->props.margin, axis) + free);
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
  b32 result = box->props.main_axis_size == kUIMainAxisSizeMax &&
               axis == (int)main_axis && max_size_axis != F32_INFINITY;
  return result;
}

static f32 GetFirstNonZeroFontSize(UIBox *box) {
  f32 font_size = box->props.font_size;
  if (font_size <= 0 && box->parent) {
    font_size = GetFirstNonZeroFontSize(box->parent);
  }
  return font_size;
}

static Vec2 LayoutText(UIBox *box, Vec2 max_size, Axis2 main_axis,
                       Axis2 cross_axis) {
  ASSERT(!IsEmptyStr8(box->props.text));

  // TODO: constraint text size within [(0, 0), max_size]

  f32 font_size = GetFirstNonZeroFontSize(box);
  if (font_size <= 0) {
    font_size = kUIFontSizeDefault;
  }
  box->computed.font_size = font_size;
  TextMetrics metrics = GetTextMetricsStr8(box->props.text, font_size);
  Vec2 text_size = metrics.size;
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
    total_flex += child->props.flex;
    if (!child->props.flex) {
      Vec2 this_child_max_size;
      SetItemVec2(&this_child_max_size, main_axis,
                  max_main_axis_size - child_main_axis_size);
      SetItemVec2(&this_child_max_size, cross_axis, max_cross_axis_size);
      Vec2 this_child_min_size = {0};
      if (box->props.cross_axis_align == kUICrossAxisAlignStretch) {
        SetItemVec2(&this_child_min_size, cross_axis,
                    GetItemVec2(this_child_max_size, cross_axis));
      }

      // Leave space for margin
      f32 margin_x = child->props.margin.start + child->props.margin.end;
      f32 margin_y = child->props.margin.top + child->props.margin.bottom;
      this_child_max_size.x = MaxF32(this_child_max_size.x - margin_x, 0);
      this_child_max_size.y = MaxF32(this_child_max_size.y - margin_y, 0);
      LayoutBox(state, child, this_child_min_size, this_child_max_size);

      // Add margin back
      f32 this_child_main_axis_size =
          GetItemVec2(child->computed.size, main_axis) +
          GetEdgeInsetsSize(child->props.margin, main_axis);
      f32 this_child_cross_axis_size =
          GetItemVec2(child->computed.size, cross_axis) +
          GetEdgeInsetsSize(child->props.margin, cross_axis);

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
    if (child->props.flex) {
      if (max_main_axis_size == kUISizeInfinity) {
        PushUIBuildErrorF(state, "Unbounded constraint doesn't work with flex");
      }

      f32 this_child_max_main_axis_size;
      if (child == last_flex) {
        this_child_max_main_axis_size =
            max_main_axis_size - child_main_axis_size;
      } else {
        this_child_max_main_axis_size =
            ClampF32(child->props.flex / total_flex * child_main_axis_flex, 0,
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
      if (box->props.cross_axis_align == kUICrossAxisAlignStretch) {
        SetItemVec2(&this_child_min_size, cross_axis, max_cross_axis_size);
      } else {
        SetItemVec2(&this_child_min_size, cross_axis, 0.0f);
      }

      // Leave space for margin
      f32 margin_x = child->props.margin.start + child->props.margin.end;
      f32 margin_y = child->props.margin.top + child->props.margin.bottom;
      this_child_max_size.x = MaxF32(this_child_max_size.x - margin_x, 0);
      this_child_max_size.y = MaxF32(this_child_max_size.y - margin_y, 0);
      LayoutBox(state, child, this_child_min_size, this_child_max_size);

      f32 this_child_main_axis_size = this_child_max_main_axis_size;
      // Add margin back
      f32 this_child_cross_axis_size =
          GetItemVec2(child->computed.size, cross_axis) +
          GetEdgeInsetsSize(child->props.margin, cross_axis);

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
    f32 build_size_axis = GetItemVec2(box->props.size, axis);
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
      children_max_size.x - (box->props.padding.start + box->props.padding.end),
      0);
  children_max_size.y =
      MaxF32(children_max_size.y -
                 (box->props.padding.top + box->props.padding.bottom),
             0);

  Axis2 main_axis = box->props.main_axis;
  Axis2 cross_axis = (main_axis + 1) % kAxis2Count;
  Vec2 children_size = V2(0, 0);
  if (box->first) {
    if (!IsEmptyStr8(box->props.text)) {
      PushUIBuildErrorF(state,
                        "%s: text content is ignored because box has children",
                        box->props.key.str.ptr);
    }
    children_size =
        LayoutChildren(state, box, children_max_size, main_axis, cross_axis);
  } else if (!IsEmptyStr8(box->props.text)) {
    children_size = LayoutText(box, children_max_size, main_axis, cross_axis);
  }

  // Size box itself
  for (int axis = 0; axis < kAxis2Count; ++axis) {
    f32 min_size_axis = GetItemVec2(min_size, axis);
    f32 max_size_axis = GetItemVec2(max_size, axis);

    f32 build_size_axis = GetItemVec2(box->props.size, axis);
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
      f32 padding_start_axis = GetEdgeInsetsStart(box->props.padding, axis);
      f32 padding_end_axis = GetEdgeInsetsEnd(box->props.padding, axis);
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

  AlignMainAxis(
      box, main_axis, GetEdgeInsetsStart(box->props.padding, main_axis),
      GetEdgeInsetsEnd(box->props.padding, main_axis),
      box->props.main_axis_align, GetItemVec2(children_size, main_axis));
  AlignCrossAxis(box, cross_axis,
                 GetEdgeInsetsStart(box->props.padding, cross_axis),
                 GetEdgeInsetsEnd(box->props.padding, cross_axis),
                 box->props.cross_axis_align);
  // Clip if content size exceeds self size.
  box->computed.clip =
      box->computed.clip ||
      children_size.x + box->props.padding.start + box->props.padding.end >
          box->computed.size.x ||
      children_size.y + box->props.padding.top + box->props.padding.bottom >
          box->computed.size.y;
}

static void RenderBox(UIState *state, UIBox *box, Vec2 parent_pos,
                      Rect2 parent_clip_rect) {
  Vec2 min = AddVec2(parent_pos, box->computed.rel_pos);
  Vec2 max = AddVec2(min, box->computed.size);
  box->computed.screen_rect = R2(min, max);

  Rect2 intersection =
      Rect2FromIntersection(parent_clip_rect, box->computed.screen_rect);
  f32 intersection_area = GetRect2Area(intersection);
  if (intersection_area > 0) {
    b32 need_clip = box->computed.clip;
    if (need_clip) {
      PushClipRect(intersection.min, intersection.max);
    }

    if (box->props.color.a) {
      DrawRect(min, max, box->props.color);
    }

    // Debug outline
    // DrawRectLine(min_in_pixel, max_in_pixel,
    // ColorU32FromHex(0xFF00FF), 1.0f);

    if (box->first) {
      for (UIBox *child = box->first; child; child = child->next) {
        RenderBox(state, child, min, intersection);
      }
    } else if (!IsEmptyStr8(box->props.text)) {
      DrawTextStr8(min, box->props.text, box->computed.font_size);
    }

    if (need_clip) {
      PopClipRect();
    }
  }
}

#if 0
#include <stdlib.h>

#include "src/log.h"
static void DebugPrintUIR(UIBox *box, u32 level) {
  INFO(
      "%*s %s[id=%s, min_size=(%.2f, %.2f), max_size=(%.2f, %.2f), "
      "build_size=(%.2f, %.2f), size=(%.2f, %.2f), rel_pos=(%.2f, %.2f)]",
      level * 4, "", box->computed.tag, box->props.key.str.ptr,
      box->computed.min_size.x, box->computed.min_size.y,
      box->computed.max_size.x, box->computed.max_size.y, box->props.size.x,
      box->props.size.y, box->computed.size.x, box->computed.size.y,
      box->computed.rel_pos.x, box->computed.rel_pos.y);
  for (UIBox *child = box->first; child; child = child->next) {
    DebugPrintUIR(child, level + 1);
  }
}

static void DebugPrintUI(UIState *state) {
  if (state->build_index > 1) {
    for (UILayer *layer = state->first_layer; layer; layer = layer->next) {
      if (layer->root) {
        DebugPrintUIR(layer->root, 0);
      }
    }
    exit(0);
  }
}
#else
static void DebugPrintUI(UIState *state) { (void)state; }
#endif

static void ProcessInputR(UIState *state, UIBox *box) {
  for (UIBox *child = box->first; child; child = child->next) {
    ProcessInputR(state, child);
  }

  // Mouse input
  if (!state->input.mouse.hovering && box->props.hoverable &&
      ContainsVec2(state->input.mouse.pos, box->computed.screen_rect.min,
                   box->computed.screen_rect.max)) {
    state->input.mouse.hovering = box;
  }

  for (int button = 0; button < kUIMouseButtonCount; ++button) {
    if (!state->input.mouse.pressed[button] && box->props.clickable[button] &&
        ContainsVec2(state->input.mouse.pos, box->computed.screen_rect.min,
                     box->computed.screen_rect.max) &&
        IsMouseButtonPressed(state, button)) {
      state->input.mouse.pressed[button] = box;
      state->input.mouse.pressed_pos[button] = state->input.mouse.pos;
    }
  }

  if (!state->input.mouse.scrolling && box->props.scrollable &&
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

  for (UILayer *layer = state->last_layer; layer; layer = layer->prev) {
    if (layer->root) {
      ProcessInputR(state, layer->root);
    }
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
  ASSERTF(!state->current_layer, "Mismatched BeginLayer/EndLayer calls");

  for (UILayer *layer = state->first_layer; layer; layer = layer->next) {
    if (layer->root) {
      LayoutBox(state, layer->root, state->screen_size, state->screen_size);
      layer->root->computed.rel_pos = V2(0, 0);
    }
  }

  ProcessInput(state);

  DebugPrintUI(state);
}

void RenderUI(void) {
  UIState *state = GetUIState();

  ASSERTF(!state->first_error, "%s", state->first_error->message.ptr);

  for (UILayer *layer = state->first_layer; layer; layer = layer->next) {
    if (layer->root) {
      RenderBox(state, layer->root, V2(0, 0), R2(V2(0, 0), state->screen_size));
    }
  }
}

void BeginUILayer(void) {
  UIState *state = GetUIState();
  Arena *arena = GetBuildArena(state);
  UILayer *layer = PushArray(arena, UILayer, 1);
  INSERT_DOUBLY_LINKED_LIST(state->first_layer, state->last_layer,
                            state->current_layer, layer, prev, next);
  layer->parent = state->current_layer;
  state->current_layer = layer;
}

void EndUILayer(void) {
  UIState *state = GetUIState();
  ASSERTF(state->current_layer, "Mismatched BeginLayer/EndLayer calls");
  ASSERTF(!state->current_layer->current,
          "Mismatched BeginUIBox/EndUIBox calls");

  state->current_layer = state->current_layer->parent;
}

UIBuildError *GetFirstUIBuildError(void) {
  UIState *state = GetUIState();
  UIBuildError *result = state->first_error;
  return result;
}

static UIKey UIKeyFromStr8(UIKey seed, Str8 str) {
  UIKey result = UIKeyZero();
  if (str.len) {
    // djb2 hash function
    u64 hash = seed.hash ? seed.hash : 5381;
    for (usize i = 0; i < str.len; i += 1) {
      // hash * 33 + c
      hash = ((hash << 5) + hash) + str.ptr[i];
    }
    result.hash = hash;
    result.str = str;
  }
  return result;
}

b32 IsEqualUIKey(UIKey a, UIKey b) {
  b32 result = a.hash == b.hash && IsEqualStr8(a.str, b.str);
  return result;
}

static UIKey GetFirstNonZeroUIKeyR(UIBox *box) {
  UIKey result = UIKeyZero();
  if (box) {
    if (!IsZeroUIKey(box->key)) {
      result = box->key;
    } else {
      result = GetFirstNonZeroUIKeyR(box->parent);
    }
  }
  return result;
}

static UIKey GetFirstNonZeroUIKey(UIState *state) {
  UIKey result = UIKeyZero();
  if (state->current_layer) {
    result = GetFirstNonZeroUIKeyR(state->current_layer->current);
  }
  return result;
}

static UIKey PushUIKeyWithStrFromBuildArena(Str8 key_str) {
  UIState *state = GetUIState();
  UIKey seed = GetFirstNonZeroUIKey(state);
  UIKey result = UIKeyFromStr8(seed, key_str);
  return result;
}

UIKey PushUIKey(Str8 key_str) {
  UIState *state = GetUIState();
  Arena *arena = GetBuildArena(state);
  Str8 key_str_copy = PushStr8(arena, key_str);
  UIKey seed = GetFirstNonZeroUIKey(state);
  UIKey result = UIKeyFromStr8(seed, key_str_copy);
  return result;
}

UIKey PushUIKeyF(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  UIKey result = PushUIKeyFV(fmt, ap);
  va_end(ap);
  return result;
}

UIKey PushUIKeyFV(const char *fmt, va_list ap) {
  UIState *state = GetUIState();
  Arena *arena = GetBuildArena(state);
  Str8 key_str = PushStr8FV(arena, fmt, ap);
  UIKey result = PushUIKeyWithStrFromBuildArena(key_str);
  return result;
}

Str8 PushUIText(Str8 key_str) {
  UIState *state = GetUIState();
  Arena *arena = GetBuildArena(state);
  Str8 result = PushStr8(arena, key_str);
  return result;
}

Str8 PushUITextF(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  Str8 result = PushUITextFV(fmt, ap);
  va_end(ap);
  return result;
}

Str8 PushUITextFV(const char *fmt, va_list ap) {
  UIState *state = GetUIState();
  Arena *arena = GetBuildArena(state);
  Str8 result = PushStr8FV(arena, fmt, ap);
  return result;
}

void BeginUIBoxWithTag(const char *tag, UIProps props) {
  UIState *state = GetUIState();

  UILayer *layer = state->current_layer;
  ASSERTF(layer, "No active UILayer");

  UIBox *parent = layer->current;
  UIKey key = props.key;
  UIBox *box = GetOrPushBoxByKey(&state->cache, state->arena, key);
  ASSERTF(box->last_touched_build_index < state->build_index,
          "%s is built more than once",
          IsEmptyStr8(key.str) ? "<unknown>" : (char *)key.str.ptr);

  if (parent) {
    APPEND_DOUBLY_LINKED_LIST(parent->first, parent->last, box, prev, next);
  } else {
    ASSERTF(!layer->root, "More than one root provided");
    layer->root = box;
  }
  box->parent = parent;
  box->last_touched_build_index = state->build_index;

  // Clear per frame state
  box->first = box->last = 0;
  box->props = props;
  box->computed.tag = tag;

  layer->current = box;
}

void EndUIBoxWithExpectedTag(const char *tag) {
  UIState *state = GetUIState();
  UILayer *layer = state->current_layer;
  ASSERTF(layer, "No active UILayer");

  ASSERT(layer->current);
  ASSERTF(strcmp(layer->current->computed.tag, tag) == 0,
          "Mismatched Begin/End calls. Begin with %s, end with %s",
          layer->current->computed.tag, tag);
  layer->current = layer->current->parent;
}

static inline UIBox *GetUIBoxByKeyInternal(UIState *state, UIKey key) {
  UIBox *result = GetBoxByKey(&state->cache, key);
  return result;
}

UIBox *GetUIBox(UIKey key) {
  UIState *state = GetUIState();
  UIBox *result = GetUIBoxByKeyInternal(state, key);
  return result;
}

UIComputed GetUIComputed(UIKey key) {
  UIState *state = GetUIState();
  UIBox *box = GetUIBoxByKeyInternal(state, key);
  UIComputed result = {0};
  if (box) {
    result = box->computed;
  }
  return result;
}

Vec2 GetUIMouseRelPos(UIKey key) {
  UIState *state = GetUIState();
  UIBox *box = GetUIBoxByKeyInternal(state, key);
  Vec2 result = V2(0, 0);
  if (box) {
    result = SubVec2(state->input.mouse.pos, box->computed.screen_rect.min);
  }
  return result;
}

static inline b32 IsEqualUIKeyAndNonZero(UIBox *box, UIKey key) {
  b32 result = 0;
  if (box && !IsZeroUIKey(key)) {
    result = IsEqualUIKey(box->key, key);
  }
  return result;
}

b32 IsUIMouseHovering(UIKey key) {
  UIState *state = GetUIState();
  b32 result = IsEqualUIKeyAndNonZero(state->input.mouse.hovering, key);
  return result;
}

b32 IsUIMouseButtonPressed(UIKey key, UIMouseButton button) {
  UIState *state = GetUIState();
  b32 result = IsEqualUIKeyAndNonZero(state->input.mouse.pressed[button], key);
  return result;
}

b32 IsUIMouseButtonDown(UIKey key, UIMouseButton button) {
  UIState *state = GetUIState();
  b32 result = IsEqualUIKeyAndNonZero(state->input.mouse.holding[button], key);
  return result;
}

b32 IsUIMouseButtonClicked(UIKey key, UIMouseButton button) {
  UIState *state = GetUIState();
  b32 result = IsEqualUIKeyAndNonZero(state->input.mouse.clicked[button], key);
  return result;
}

b32 IsUIMouseButtonDragging(UIKey key, UIMouseButton button, Vec2 *delta) {
  UIState *state = GetUIState();
  f32 result = IsEqualUIKeyAndNonZero(state->input.mouse.holding[button], key);
  if (result && delta) {
    *delta =
        SubVec2(state->input.mouse.pos, state->input.mouse.pressed_pos[button]);
  }
  return result;
}

b32 IsUIMouseScrolling(UIKey key, Vec2 *delta) {
  UIState *state = GetUIState();
  f32 result = IsEqualUIKeyAndNonZero(state->input.mouse.scrolling, key);
  if (result && delta) {
    *delta = state->input.mouse.scroll_delta;
  }
  return result;
}
