#include "src/ui.h"

#include "src/assert.h"
#include "src/draw.h"
#include "src/list.h"
#include "src/math.h"
#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"

#define WIDGET_SIZE_MAX F32_MAX

typedef struct WidgetHashSlot WidgetHashSlot;
struct WidgetHashSlot {
  Widget *first;
  Widget *last;
};

typedef struct UIState UIState;
struct UIState {
  Arena *arena;

  // widget cache
  Widget *first_free_widget;
  u32 widget_hash_slot_size;
  WidgetHashSlot *widget_hash_slots;

  Arena *build_arena[2];
  u64 build_index;

  // per-frame info
  Vec2 canvas_size;
  Widget *root;
  Widget *current;
};

UIState g_ui_state;

WidgetKey WidgetKeyZero(void) {
  WidgetKey result = {0};
  return result;
}

WidgetKey WidgetKeyFromStr8(WidgetKey seed, Str8 str) {
  // djb2 hash function
  u64 hash = seed.hash ? seed.hash : 5381;
  for (usize i = 0; i < str.len; i += 1) {
    // hash * 33 + c
    hash = ((hash << 5) + hash) + str.ptr[i];
  }
  WidgetKey result = {hash};
  return result;
}

b32 EqualWidgetKey(WidgetKey a, WidgetKey b) {
  b32 result = a.hash == b.hash;
  return result;
}

static Widget *PushWidget(Arena *arena) {
  Widget *result = PushArray(arena, Widget, 1);
  return result;
}

static UIState *GetUIState(void) {
  UIState *state = &g_ui_state;
  if (!state->arena) {
    state->arena = AllocArena();
    state->widget_hash_slot_size = 4096;
    state->widget_hash_slots =
        PushArray(state->arena, WidgetHashSlot, state->widget_hash_slot_size);

    state->root = PushWidget(state->arena);

    state->build_arena[0] = AllocArena();
    state->build_arena[1] = AllocArena();
    state->current = state->root;
  }
  return state;
}

static Widget *GetOrPushWidget(UIState *state) {
  Widget *result;
  if (state->first_free_widget) {
    result = state->first_free_widget;
    state->first_free_widget = result->next;
    ZeroMemory(result, sizeof(*result));
  } else {
    result = PushWidget(state->arena);
  }
  return result;
}

static Arena *GetBuildArena(UIState *state) {
  Arena *arena =
      state->build_arena[state->build_index % ARRAY_COUNT(state->build_arena)];
  return arena;
}

void BeginUI(void) {
  UIState *state = GetUIState();

  state->build_index += 1;
  state->canvas_size = Vec2FromVec2I(GetCanvasSize());
  state->root = 0;
  state->current = 0;

  ResetArena(GetBuildArena(state));
}

const f32 kDefaultTextHeight = 16;

static void AlignMainAxis(Widget *widget, Axis2 axis, UIAlign align,
                          f32 children_size) {
  // TODO: Handle padding.
  f32 free = widget->computed_size.v[axis] - children_size;
  f32 pos = 0.0f;
  switch (align) {
    case kUIAlignStart: {
    } break;

    case kUIAlignCenter: {
      pos = free / 2.0;
    } break;

    case kUIAlignEnd: {
      pos = free;
    } break;

    default: {
      UNREACHABLE;
    } break;
  }

  for (Widget *child = widget->first; child; child = child->next) {
    child->computed_rel_pos.v[axis] = pos;
    pos += child->computed_size.v[axis];
  }
}

static void AlignCrossAxis(Widget *widget, Axis2 axis, UIAlign align,
                           f32 children_size) {
  // TODO: Handle padding.
  f32 free = widget->computed_size.v[axis] - children_size;
  f32 pos = 0.0f;
  switch (align) {
    case kUIAlignStart: {
    } break;

    case kUIAlignCenter: {
      pos = free / 2.0;
    } break;

    case kUIAlignEnd: {
      pos = free;
    } break;

    default: {
      UNREACHABLE;
    } break;
  }

  for (Widget *child = widget->first; child; child = child->next) {
    child->computed_rel_pos.v[axis] = pos;
  }
}

static void LayoutWidget(UIState *state, Widget *widget, Vec2 min_size,
                         Vec2 max_size) {
  Vec2 self_size = V2(0, 0);
  Vec2 child_max_size = max_size;
  for (u32 axis = 0; axis < kAxis2Count; ++axis) {
    if (widget->build.size.v[axis]) {
      // If widget has specific size, cap it within constraint and use that
      // as constraint for child.
      self_size.v[axis] = MaxF32(MinF32(max_size.x, widget->build.size.v[axis]),
                                 min_size.v[axis]);
      child_max_size.v[axis] = widget->build.size.v[axis];
    } else {
      // Otherwise, pass down the constraint and ...
      if (max_size.v[axis] >= WIDGET_SIZE_MAX) {
        // ... if constraint is unbounded, make widget as small as possible.
        self_size.v[axis] = min_size.v[axis];
      } else {
        // Otherwise, make widget as large as possible.
        self_size.v[axis] = max_size.v[axis];
      }
      child_max_size.v[axis] = max_size.v[axis];
    }
  }

  // TODO: handle padding and margin.
  Vec2 child_size = V2(0, 0);
  if (widget->first) {
    for (Widget *child = widget->first; child; child = child->next) {
      LayoutWidget(state, child, V2(0, 0), child_max_size);

      // Row direction
      // TODO: Column direction
      child->computed_rel_pos.x = child_size.x;
      child_size.x += child->computed_size.x;
      child_size.y = MaxF32(child_size.y, child->computed_size.y);
    }
  } else if (!IsEmptyStr8(widget->build.text)) {
    // TODO: constraint text size within [(0, 0), child_max_size]
    TextMetrics metrics =
        GetTextMetricsStr8(widget->build.text, kDefaultTextHeight);
    child_size = MinVec2(metrics.size, child_max_size);
  }

  for (u32 axis = 0; axis < kAxis2Count; ++axis) {
    // If widget doesn't have specific size but has child, size itself
    // around the child.
    if (!widget->build.size.v[axis] && child_size.v[axis]) {
      widget->computed_size.v[axis] =
          MaxF32(child_size.v[axis], min_size.v[axis]);
    } else {
      widget->computed_size.v[axis] = self_size.v[axis];
    }
  }

  Axis2 main = widget->build.main_axis;
  Axis2 cross = (main + 1) % kAxis2Count;
  AlignMainAxis(widget, main, widget->build.aligns[main], child_size.v[main]);
  AlignCrossAxis(widget, cross, widget->build.aligns[cross],
                 child_size.v[cross]);
}

static void RenderWidget(UIState *state, Widget *widget, Vec2 parent_pos) {
  Vec2 min = AddVec2(parent_pos, widget->computed_rel_pos);
  Vec2 max = AddVec2(min, widget->computed_size);
  widget->computed_screen_rect = (Rect2){.min = min, .max = max};

  if (widget->build.color) {
    DrawRect(min, max, widget->build.color);
  }

  if (widget->first) {
    for (Widget *child = widget->first; child; child = child->next) {
      RenderWidget(state, child, min);
    }
  } else if (!IsEmptyStr8(widget->build.text)) {
    // TODO: clip
    DrawTextStr8(min, widget->build.text, kDefaultTextHeight);
  }
}

void EndUI(void) {
  UIState *state = GetUIState();
  ASSERT(!state->current && "Mismatched Begin/End calls");

  if (state->root) {
    LayoutWidget(state, state->root, state->canvas_size, state->canvas_size);
    state->root->computed_rel_pos = V2(0, 0);
    state->root->computed_screen_rect =
        (Rect2){.min = V2(0, 0), .max = state->canvas_size};

    RenderWidget(state, state->root, V2(0, 0));
  }
}

static Widget *GetWidgetByKey(UIState *state, WidgetKey key) {
  Widget *result = 0;
  if (!EqualWidgetKey(key, WidgetKeyZero())) {
    WidgetHashSlot *slot =
        &state->widget_hash_slots[key.hash % state->widget_hash_slot_size];
    for (Widget *widget = slot->first; widget; widget = widget->hash_next) {
      if (EqualWidgetKey(widget->key, key)) {
        result = widget;
        break;
      }
    }
  }
  return result;
}

void BeginWidget(Str8 key_str) {
  UIState *state = GetUIState();

  Widget *parent = state->current;
  WidgetKey seed = WidgetKeyZero();
  if (parent) {
    seed = parent->key;
  }
  WidgetKey key = WidgetKeyFromStr8(seed, key_str);
  Widget *widget = GetWidgetByKey(state, key);
  if (!widget) {
    widget = GetOrPushWidget(state);
    widget->key = key;
    WidgetHashSlot *slot =
        &state->widget_hash_slots[key.hash % state->widget_hash_slot_size];
    APPEND_DOUBLY_LINKED_LIST(slot->first, slot->last, widget, hash_prev,
                              hash_next);
  }

  if (parent) {
    APPEND_DOUBLY_LINKED_LIST(parent->first, parent->last, widget, prev, next);
  } else {
    ASSERT(!state->root && "More than one root widget provided");
    state->root = widget;
  }
  widget->parent = parent;
  widget->last_touched_build_index = state->build_index;

  // Clear per frame state
  widget->first = widget->last = 0;
  widget->build = (UIWidgetBuildData){0};

  state->current = widget;
}

void EndWidget(void) {
  UIState *state = GetUIState();

  ASSERT(state->current);
  state->current = state->current->parent;
}

void SetWidgetColor(u32 color) {
  UIState *state = GetUIState();
  ASSERT(state->current);
  state->current->build.color = color;
}

void SetWidgetSize(Vec2 size) {
  UIState *state = GetUIState();
  ASSERT(state->current);
  state->current->build.size = size;
}

void SetWidgetText(Str8 text) {
  UIState *state = GetUIState();
  ASSERT(state->current);

  Arena *arena = GetBuildArena(state);
  state->current->build.text = PushStr8(arena, text);
}

void UISetMainAxis(Axis2 axis) {
  UIState *state = GetUIState();
  ASSERT(state->current);

  state->current->build.main_axis = axis;
}

void UISetMainAxisAlignment(UIAlign align) {
  UIState *state = GetUIState();
  ASSERT(state->current);

  state->current->build.aligns[state->current->build.main_axis] = align;
}

void UISetCrossAxisAlignment(UIAlign align) {
  UIState *state = GetUIState();
  ASSERT(state->current);

  state->current->build
      .aligns[(state->current->build.main_axis + 1) % kAxis2Count] = align;
}
