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
  UIMouseButtonState buttons[kUIMouseButtonCount];

  UIBox *hovering;
  UIBox *pressed[kUIMouseButtonCount];
  UIBox *holding[kUIMouseButtonCount];
  UIBox *clicked[kUIMouseButtonCount];
} UIMouseInput;

typedef struct UIInput {
  UIMouseInput mouse;
} UIInput;

typedef struct UIState {
  Arena *arena;

  UIBoxCache cache;
  UIInput input;

  Arena *build_arena[2];
  u64 build_index;

  // per-frame info
  Str8 next_ui_key_str;
  f32 content_scale;
  Vec2 screen_size;
  UIBox *root;
  UIBox *current;
} UIState;

thread_local UIState t_ui_state;

static UIState *GetUIState(void) {
  UIState *state = &t_ui_state;
  if (!state->arena) {
    state->arena = AllocArena();
    InitUIBoxCache(&state->cache, state->arena);
    state->build_arena[0] = AllocArena();
    state->build_arena[1] = AllocArena();

    state->input.mouse.pos = V2(-1, -1);
  }
  return state;
}

static Arena *GetBuildArena(UIState *state) {
  Arena *arena =
      state->build_arena[state->build_index % ARRAY_COUNT(state->build_arena)];
  return arena;
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

void BeginUIFrame(Vec2 screen_size, f32 content_scale) {
  UIState *state = GetUIState();

  GarbageCollectBoxes(&state->cache, state->build_index);

  state->build_index += 1;
  state->content_scale = content_scale;
  state->screen_size = screen_size;
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
      SetItemVec2(&self_size, axis,
                  MaxF32(MinF32(max_size_axis, self_size_axis), min_size_axis));
      SetItemVec2(&child_max_size, axis, GetItemVec2(self_size, axis));
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
        Vec2 this_child_max_size;
        SetItemVec2(&this_child_max_size, main_axis, child_main_axis_free);
        SetItemVec2(&this_child_max_size, cross_axis,
                    GetItemVec2(child_max_size, cross_axis));
        Vec2 this_child_min_size = {0};
        if (box->build.cross_axis_align == kUICrossAxisAlignStretch) {
          SetItemVec2(&this_child_min_size, cross_axis,
                      GetItemVec2(this_child_max_size, cross_axis));
        }
        LayoutBox(state, child, this_child_min_size, this_child_max_size,
                  main_axis);

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

static void RenderBox(UIState *state, UIBox *box, Vec2 parent_pos) {
  Vec2 min = AddVec2(parent_pos, box->computed.rel_pos);
  Vec2 max = AddVec2(min, box->computed.size);
  Vec2 min_in_pixel = MulVec2(min, state->content_scale);
  Vec2 max_in_pixel = MulVec2(max, state->content_scale);
  box->computed.screen_rect = (Rect2){.min = min, .max = max};

  if (box->build.color.a) {
    DrawRect(min_in_pixel, max_in_pixel, box->build.color);
  }

  // Debug outline
  // DrawRectLine(min_in_pixel, max_in_pixel, ColorU32FromHex(0xFF00FF), 1.0f);

  if (box->first) {
    for (UIBox *child = box->first; child; child = child->next) {
      RenderBox(state, child, min);
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
    }
  }
}

static void ProcessInput(UIState *state) {
  for (int button = 0; button < kUIMouseButtonCount; ++button) {
    state->input.mouse.hovering = 0;
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
}

void EndUIFrame(void) {
  UIState *state = GetUIState();
  ASSERTF(!state->current, "Mismatched Begin/End calls");

  if (state->root) {
    LayoutBox(state, state->root, state->screen_size, state->screen_size, 0);
    state->root->computed.rel_pos = V2(0, 0);
  }

  ProcessInput(state);

  // DebugPrintUI(state);
}

void RenderUI(void) {
  UIState *state = GetUIState();
  if (state->root) {
    RenderBox(state, state->root, V2(0, 0));
  }
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

void BeginUIBox(void) {
  UIState *state = GetUIState();

  Str8 key_str = state->next_ui_key_str;
  state->next_ui_key_str = Str8Zero();

  UIBox *parent = state->current;
  UIKey seed = UIKeyZero();
  if (parent) {
    seed = parent->key;
  }
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
  box->build = (UIBuildData){0};

  if (!IsEmptyStr8(key_str)) {
    Arena *build_arena = GetBuildArena(state);
    box->build.key_str = PushStr8(build_arena, key_str);
  }

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

UIBox *GetUIBoxByKey(UIBox *parent, Str8 key_str) {
  UIBox *result = 0;

  UIState *state = GetUIState();
  if (!parent) {
    parent = state->root;
  }

  if (parent) {
    UIKey seed = UIKeyFromHash((u64)parent);
    UIKey key = UIKeyFromStr8(seed, key_str);
    result = GetBoxByKey(&state->cache, key);
  }

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

UIBox *GetUICurrent(void) {
  UIState *state = GetUIState();
  ASSERT(state->current);
  return state->current;
}

void SetNextUIKey(Str8 key) {
  UIState *state = GetUIState();

  Arena *arena = GetBuildArena(state);
  state->next_ui_key_str = PushStr8(arena, key);
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

static UIBox *GetUIBoxForLastFrameData(UIState *state) {
  UIBox *box = state->current;
  ASSERT(box);
  ASSERTF(!IsEqualUIKey(box->key, UIKeyZero()),
          "Must assign a key to the box in order to get computed property");
  return box;
}

UIComputedData GetUIComputed(void) {
  UIState *state = GetUIState();
  UIBox *box = GetUIBoxForLastFrameData(state);
  return box->computed;
}

Vec2 GetUIMouseRelPos(void) {
  UIState *state = GetUIState();
  UIBox *box = GetUIBoxForLastFrameData(state);
  Vec2 result = SubVec2(state->input.mouse.pos, box->computed.screen_rect.min);
  return result;
}

b32 IsUIHovering(void) {
  UIState *state = GetUIState();
  UIBox *box = GetUIBoxForLastFrameData(state);
  box->build.hoverable = 1;

  b32 result = state->input.mouse.hovering == box;
  return result;
}

b32 IsUIPressed(UIMouseButton button) {
  UIState *state = GetUIState();
  UIBox *box = GetUIBoxForLastFrameData(state);
  box->build.clickable[button] = 1;

  b32 result = state->input.mouse.pressed[button] == box;
  return result;
}

b32 IsUIHolding(UIMouseButton button) {
  UIState *state = GetUIState();
  UIBox *box = GetUIBoxForLastFrameData(state);
  box->build.clickable[button] = 1;

  b32 result = state->input.mouse.holding[button] == box;
  return result;
}

b32 IsUIClicked(UIMouseButton button) {
  UIState *state = GetUIState();
  UIBox *box = GetUIBoxForLastFrameData(state);
  box->build.clickable[button] = 1;

  b32 result = state->input.mouse.clicked[button] == box;
  return result;
}
