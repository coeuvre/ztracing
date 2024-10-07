#include "src/ui.h"

#include <stdlib.h>

#include "src/assert.h"
#include "src/draw.h"
#include "src/list.h"
#include "src/log.h"
#include "src/math.h"
#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"

typedef struct BoxHashSlot {
  UIBox *first;
  UIBox *last;
} BoxHashSlot;

typedef struct UIState {
  Arena *arena;

  // box cache
  UIBox *first_free_box;
  u32 box_hash_slots_count;
  BoxHashSlot *box_hash_slots;

  Arena *build_arena[2];
  u64 build_index;

  // per-frame info
  f32 content_scale;
  Vec2 screen_size_in_pixel;
  Vec2 screen_size;
  UIBox *root;
  UIBox *current;
} UIState;

thread_local UIState t_ui_state;

UIKey UIKeyZero(void) {
  UIKey result = {0};
  return result;
}

UIKey UIKeyFromStr8(UIKey seed, Str8 str) {
  // djb2 hash function
  u64 hash = seed.hash ? seed.hash : 5381;
  for (usize i = 0; i < str.len; i += 1) {
    // hash * 33 + c
    hash = ((hash << 5) + hash) + str.ptr[i];
  }
  UIKey result = {hash};
  return result;
}

b32 IsEqualUIKey(UIKey a, UIKey b) {
  b32 result = a.hash == b.hash;
  return result;
}

static UIBox *PushBox(Arena *arena) {
  UIBox *result = PushArray(arena, UIBox, 1);
  return result;
}

static UIState *GetUIState(void) {
  UIState *state = &t_ui_state;
  if (!state->arena) {
    state->arena = AllocArena();
    state->box_hash_slots_count = 4096;
    state->box_hash_slots =
        PushArray(state->arena, BoxHashSlot, state->box_hash_slots_count);

    state->root = PushBox(state->arena);

    state->build_arena[0] = AllocArena();
    state->build_arena[1] = AllocArena();
    state->current = state->root;
  }
  return state;
}

static UIBox *GetOrPushBox(UIState *state) {
  UIBox *result;
  if (state->first_free_box) {
    result = state->first_free_box;
    state->first_free_box = result->next;
    ZeroMemory(result, sizeof(*result));
  } else {
    result = PushBox(state->arena);
  }
  return result;
}

static Arena *GetBuildArena(UIState *state) {
  Arena *arena =
      state->build_arena[state->build_index % ARRAY_COUNT(state->build_arena)];
  return arena;
}

void BeginUIFrame(Vec2 screen_size_in_pixel, f32 content_scale) {
  UIState *state = GetUIState();

  state->build_index += 1;
  state->content_scale = content_scale;
  state->screen_size_in_pixel = screen_size_in_pixel;
  state->screen_size =
      MulVec2(state->screen_size_in_pixel, 1.0f / state->content_scale);
  state->root = 0;
  state->current = 0;

  ResetArena(GetBuildArena(state));
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
    SetItemVec2(&child->computed.rel_pos, axis, pos);
    pos += GetItemVec2(child->computed.size, axis);
  }
}

static void AlignCrossAxis(UIBox *box, Axis2 axis, f32 padding_start,
                           f32 padding_end, UICrossAxisAlign align) {
  // TODO: Handle margin.
  for (UIBox *child = box->first; child; child = child->next) {
    f32 free = GetItemVec2(box->computed.size, axis) -
               GetItemVec2(child->computed.size, axis) - padding_start -
               padding_end;
    switch (align) {
      case kUICrossAxisAlignStart:
      case kUICrossAxisAlignStretch: {
        SetItemVec2(&child->computed.rel_pos, axis, padding_start);
      } break;

      case kUICrossAxisAlignCenter: {
        SetItemVec2(&child->computed.rel_pos, axis,
                    padding_start + free / 2.0f);
      } break;

      case kUICrossAxisAlignEnd: {
        SetItemVec2(&child->computed.rel_pos, axis, padding_start + free);
      } break;

      default: {
        UNREACHABLE;
      } break;
    }
  }
}

static inline b32 ShouldMaxAxis(UIBox *box, int axis, Axis2 main_axis,
                                Axis2 unbounded_axis) {
  // cross axis is always as small as possible
  b32 result = axis == (int)main_axis && main_axis != unbounded_axis &&
               box->build.main_axis_size == kUIMainAxisSizeMax;
  return result;
}

static inline f32 GetPaddingStart(UIEdgeInsets padding, Axis2 axis) {
  // TODO: Handle text direction.
  f32 result;
  if (axis == kAxis2X) {
    result = padding.start;
  } else {
    result = padding.top;
  }
  return result;
}

static inline f32 GetPaddingEnd(UIEdgeInsets padding, Axis2 axis) {
  // TODO: Handle text direction.
  f32 result;
  if (axis == kAxis2X) {
    result = padding.end;
  } else {
    result = padding.bottom;
  }
  return result;
}

static void LayoutBox(UIState *state, UIBox *box, Vec2 min_size, Vec2 max_size,
                      Axis2 unbounded_axis) {
  box->computed.min_size = min_size;
  box->computed.max_size = max_size;
  box->computed.unbounded_axis = unbounded_axis;

  Axis2 main_axis = box->build.main_axis;
  Axis2 cross_axis = (main_axis + 1) % kAxis2Count;

  Vec2 self_size = V2(0, 0);
  Vec2 child_max_size = max_size;
  for (int axis = 0; axis < kAxis2Count; ++axis) {
    f32 min_size_axis = GetItemVec2(min_size, axis);
    f32 max_size_axis = GetItemVec2(max_size, axis);
    f32 self_size_axis = GetItemVec2(box->build.size, axis);
    if (self_size_axis != kUISizeUndefined) {
      // If box has specific size, use that as constraint for children and size
      // itself within the constraint.
      SetItemVec2(&child_max_size, axis, self_size_axis);
      SetItemVec2(&self_size, axis,
                  MaxF32(MinF32(max_size.x, self_size_axis), min_size_axis));
    } else {
      // Otherwise, pass down the constraint to children and ...
      SetItemVec2(&child_max_size, axis, max_size_axis);

      if (ShouldMaxAxis(box, axis, main_axis, unbounded_axis)) {
        SetItemVec2(&self_size, axis, max_size_axis);
      } else {
        // if constraint is unbounded, make it as small as possible.
        f32 min_size_axis_plus_padding =
            min_size_axis + GetPaddingStart(box->build.padding, axis) +
            GetPaddingEnd(box->build.padding, axis);
        SetItemVec2(&self_size, axis,
                    MinF32(min_size_axis_plus_padding, max_size_axis));
      }
    }
  }

  child_max_size.x -= (box->build.padding.start + box->build.padding.end);
  child_max_size.y -= (box->build.padding.top + box->build.padding.bottom);

  // TODO: handle margin.
  f32 child_main_axis_size = 0.0f;
  f32 child_cross_axis_max = 0.0f;
  if (box->first) {
    f32 total_flex = 0;
    f32 child_main_axis_free = GetItemVec2(child_max_size, main_axis);

    // First pass: layout non-flex children
    for (UIBox *child = box->first; child; child = child->next) {
      // If child doesn't have flex, doesn't apply any constraint on the child.
      total_flex += child->build.flex;
      if (!child->build.flex) {
        Vec2 max_size;
        SetItemVec2(&max_size, main_axis, child_main_axis_free);
        SetItemVec2(&max_size, cross_axis,
                    GetItemVec2(child_max_size, cross_axis));
        Vec2 min_size = {0};
        if (box->build.cross_axis_align == kUICrossAxisAlignStretch) {
          SetItemVec2(&min_size, cross_axis, GetItemVec2(max_size, cross_axis));
        }
        LayoutBox(state, child, min_size, max_size, main_axis);

        child_main_axis_free -= GetItemVec2(child->computed.size, main_axis);
        child_main_axis_size += GetItemVec2(child->computed.size, main_axis);
        child_cross_axis_max =
            MaxF32(child_cross_axis_max,
                   GetItemVec2(child->computed.size, cross_axis));
      }
    }

    // Second pass: layout flex children
    for (UIBox *child = box->first; child; child = child->next) {
      if (child->build.flex) {
        Vec2 max_size;
        SetItemVec2(&max_size, main_axis,
                    child->build.flex / total_flex * child_main_axis_free);
        SetItemVec2(&max_size, cross_axis,
                    GetItemVec2(child_max_size, cross_axis));
        Vec2 min_size = {0};
        SetItemVec2(&min_size, main_axis, GetItemVec2(max_size, main_axis));
        if (box->build.cross_axis_align == kUICrossAxisAlignStretch) {
          SetItemVec2(&min_size, cross_axis, GetItemVec2(max_size, cross_axis));
        } else {
          SetItemVec2(&min_size, cross_axis, 0.0f);
        }
        LayoutBox(state, child, min_size, max_size, kAxis2Count);

        child_main_axis_size += GetItemVec2(child->computed.size, main_axis);
        child_cross_axis_max =
            MaxF32(child_cross_axis_max,
                   GetItemVec2(child->computed.size, cross_axis));
      }
    }
  } else if (!IsEmptyStr8(box->build.text)) {
    // TODO: constraint text size within [(0, 0), child_max_size]

    // Use pixel unit to measure text
    TextMetrics metrics = GetTextMetricsStr8(
        box->build.text, KUITextSizeDefault * state->content_scale);
    Vec2 text_size_in_pixel = metrics.size;
    Vec2 text_size = MulVec2(text_size_in_pixel, 1.0f / state->content_scale);
    text_size = MinVec2(text_size, child_max_size);

    child_main_axis_size = GetItemVec2(text_size, main_axis);
    child_cross_axis_max = GetItemVec2(text_size, cross_axis);
  }

  Vec2 child_size;
  SetItemVec2(&child_size, main_axis, child_main_axis_size);
  SetItemVec2(&child_size, cross_axis, child_cross_axis_max);

  for (int axis = 0; axis < kAxis2Count; ++axis) {
    f32 child_size_axis = GetItemVec2(child_size, axis);
    if (!ShouldMaxAxis(box, axis, main_axis, unbounded_axis) &&
        GetItemVec2(box->build.size, axis) == kUISizeUndefined &&
        child_size_axis != kUISizeUndefined) {
      // Size itself around children if it doesn't have specific size
      f32 child_size_axis_plus_padding =
          child_size_axis + GetPaddingStart(box->build.padding, axis) +
          GetPaddingEnd(box->build.padding, axis);
      SetItemVec2(
          &box->computed.size, axis,
          ClampF32(child_size_axis_plus_padding, GetItemVec2(min_size, axis),
                   GetItemVec2(max_size, axis)));
    } else {
      SetItemVec2(&box->computed.size, axis, GetItemVec2(self_size, axis));
    }
  }

  AlignMainAxis(box, main_axis, GetPaddingStart(box->build.padding, main_axis),
                GetPaddingEnd(box->build.padding, main_axis),
                box->build.main_axis_align, child_main_axis_size);
  AlignCrossAxis(box, cross_axis,
                 GetPaddingStart(box->build.padding, cross_axis),
                 GetPaddingEnd(box->build.padding, cross_axis),
                 box->build.cross_axis_align);
}

static void RenderBox(UIState *state, UIBox *box, Vec2 parent_pos_in_pixel) {
  Vec2 min_in_pixel =
      AddVec2(parent_pos_in_pixel,
              MulVec2(box->computed.rel_pos, state->content_scale));
  Vec2 max_in_pixel =
      AddVec2(min_in_pixel, MulVec2(box->computed.size, state->content_scale));
  box->computed.screen_rect_in_pixel =
      (Rect2){.min = min_in_pixel, .max = max_in_pixel};

  if (box->build.color.a) {
    DrawRect(min_in_pixel, max_in_pixel, box->build.color);
  }

  // Debug outline
  // DrawRectLine(min_in_pixel, max_in_pixel, ColorU32FromHex(0xFF00FF), 1.0f);

  if (box->first) {
    for (UIBox *child = box->first; child; child = child->next) {
      RenderBox(state, child, min_in_pixel);
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
}

#if 1
static void DebugPrintUIR(UIBox *box, u32 level) {
  INFO(
      "%*s %s[min_size=(%.2f, %.2f), max_size=(%.2f, %.2f), unbounded_axis=%d, "
      "size=(%.2f, %.2f) rel_pos=(%.2f, %.2f)]",
      level * 4, "", box->build.key_str.ptr, box->computed.min_size.x,
      box->computed.min_size.y, box->computed.max_size.x,
      box->computed.max_size.y, box->computed.unbounded_axis,
      box->computed.size.x, box->computed.size.y, box->computed.rel_pos.x,
      box->computed.rel_pos.y);
  for (UIBox *child = box->first; child; child = child->next) {
    DebugPrintUIR(child, level + 1);
  }
}

static void DebugPrintUI(UIState *state) {
  if (state->root) {
    DebugPrintUIR(state->root, 0);
  }
  exit(0);
}
#endif

void EndUIFrame(void) {
  UIState *state = GetUIState();
  ASSERTF(!state->current, "Mismatched Begin/End calls");

  if (state->root) {
    LayoutBox(state, state->root, state->screen_size, state->screen_size, 0);
    state->root->computed.rel_pos = V2(0, 0);
  }

  // DebugPrintUI(state);
}

void RenderUI(void) {
  UIState *state = GetUIState();
  if (state->root) {
    RenderBox(state, state->root, V2(0, 0));
  }
}

static UIBox *GetBoxByKey(UIState *state, UIKey key) {
  UIBox *result = 0;
  if (!IsEqualUIKey(key, UIKeyZero())) {
    BoxHashSlot *slot =
        &state->box_hash_slots[key.hash % state->box_hash_slots_count];
    for (UIBox *box = slot->first; box; box = box->hash_next) {
      if (IsEqualUIKey(box->key, key)) {
        result = box;
        break;
      }
    }
  }
  return result;
}

void BeginUIBox(Str8 key_str) {
  UIState *state = GetUIState();

  UIBox *parent = state->current;
  UIKey seed = UIKeyZero();
  if (parent) {
    seed = parent->key;
  }
  UIKey key = UIKeyFromStr8(seed, key_str);
  UIBox *box = GetBoxByKey(state, key);
  if (!box) {
    box = GetOrPushBox(state);
    box->key = key;
    BoxHashSlot *slot =
        &state->box_hash_slots[key.hash % state->box_hash_slots_count];
    APPEND_DOUBLY_LINKED_LIST(slot->first, slot->last, box, hash_prev,
                              hash_next);
  }

  ASSERTF(box->last_touched_build_index < state->build_index,
          "%s is built more than once", key_str.ptr);

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
  box->build = (UIBuildData){0};

  Arena *build_arena = GetBuildArena(state);
  box->build.key_str = PushStr8(build_arena, key_str);

  state->current = box;
}

void EndUIBox(void) {
  UIState *state = GetUIState();

  ASSERT(state->current);
  state->current = state->current->parent;
}

UIBox *GetUIRoot(void) {
  UIState *state = GetUIState();
  return state->root;
}

UIBox *GetUIChild(UIBox *box, Str8 key_str) {
  UIState *state = GetUIState();

  UIKey seed = box->key;
  UIKey key = UIKeyFromStr8(seed, key_str);
  UIBox *result = GetBoxByKey(state, key);
  return result;
}

UIBox *GetUICurrent(void) {
  UIState *state = GetUIState();
  ASSERT(state->current);
  return state->current;
}

void SetUIColor(ColorU32 color) {
  UIState *state = GetUIState();
  ASSERT(state->current);
  state->current->build.color = color;
}

void SetUISize(Vec2 size) {
  UIState *state = GetUIState();
  ASSERT(state->current);
  state->current->build.size = size;
}

void SetUIText(Str8 text) {
  UIState *state = GetUIState();
  ASSERT(state->current);

  Arena *arena = GetBuildArena(state);
  state->current->build.text = PushStr8(arena, text);
}

void SetUIMainAxis(Axis2 axis) {
  UIState *state = GetUIState();
  ASSERT(state->current);

  state->current->build.main_axis = axis;
}

void SetUIMainAxisSize(UIMainAxisSize main_axis_size) {
  UIState *state = GetUIState();
  ASSERT(state->current);

  state->current->build.main_axis_size = main_axis_size;
}

void SetUIMainAxisAlign(UIMainAxisAlign main_axis_align) {
  UIState *state = GetUIState();
  ASSERT(state->current);

  state->current->build.main_axis_align = main_axis_align;
}

void SetUICrossAxisAlign(UICrossAxisAlign cross_axis_align) {
  UIState *state = GetUIState();
  ASSERT(state->current);

  state->current->build.cross_axis_align = cross_axis_align;
}

void SetUIFlex(f32 flex) {
  UIState *state = GetUIState();
  ASSERT(state->current);

  state->current->build.flex = flex;
}

void SetUIPadding(UIEdgeInsets padding) {
  GetUICurrent()->build.padding = padding;
}
