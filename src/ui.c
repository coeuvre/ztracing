#include "src/ui.h"

#include "src/assert.h"
#include "src/draw.h"
#include "src/list.h"
#include "src/math.h"
#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"

typedef struct WidgetHashSlot WidgetHashSlot;
struct WidgetHashSlot {
  Widget *first;
  Widget *last;
};

typedef struct NextWidgetSettings NextWidgetSettings;
struct NextWidgetSettings {
  Str8 key;
  WidgetConstraint constraints[kAxis2Count];
  Axis2 layout_axis;
  Str8 text;
};

typedef struct UIState UIState;
struct UIState {
  Arena *arena;

  // widget cache
  Widget *first_free_widget;
  u32 widget_hash_slot_size;
  WidgetHashSlot *widget_hash_slots;

  Widget *root;
  u64 build_index;

  // per-frame info
  Arena *build_arena[2];
  Vec2 canvas_size;
  Widget *current;
  NextWidgetSettings next_widget_settings;
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

  Widget *root = state->root;
  root->first = root->last = 0;
  state->canvas_size = Vec2FromVec2I(GetCanvasSize());
  root->constraints[kAxis2X] = (WidgetConstraint){
      .type = kWidgetConstraintPixels, .value = state->canvas_size.x};
  root->constraints[kAxis2Y] = (WidgetConstraint){
      .type = kWidgetConstraintPixels, .value = state->canvas_size.y};

  state->current = state->root;

  ResetArena(GetBuildArena(state));
}

const f32 kDefaultTextHeight = 16;

static void CalcStandaloneSizeR(UIState *state, Widget *widget) {
  for (u32 axis = 0; axis < kAxis2Count; ++axis) {
    WidgetConstraint constraint = widget->constraints[axis];
    switch (constraint.type) {
      case kWidgetConstraintPixels: {
        widget->computed_size[axis] = constraint.value;
      } break;

      case kWidgetConstraintTextContent: {
        widget->computed_size[axis] =
            GetTextMetricsStr8(widget->text, kDefaultTextHeight).size.v[axis];
      } break;

      default: {
      } break;
    }
  }

  for (Widget *child = widget->first; child; child = child->next) {
    CalcStandaloneSizeR(state, child);
  }
}

static void CalcStandaloneSize(UIState *state) {
  CalcStandaloneSizeR(state, state->root);
}

static void CalcParentBasedSizeR(UIState *state, Widget *widget) {
  for (u32 axis = 0; axis < kAxis2Count; ++axis) {
    WidgetConstraint constraint = widget->constraints[axis];
    switch (constraint.type) {
      case kWidgetConstraintPercentOfParent: {
        widget->computed_size[axis] =
            widget->parent->computed_size[axis] * constraint.value;
      } break;

      default: {
      } break;
    }
  }

  for (Widget *child = widget->first; child; child = child->next) {
    CalcParentBasedSizeR(state, child);
  }
}

static void CalcParentBasedSize(UIState *state) {
  CalcParentBasedSizeR(state, state->root);
}

static void CalcScreenRectR(UIState *state, Widget *widget, Vec2 min) {
  {
    u32 layout_axis = widget->layout_axis;
    f32 total = widget->computed_size[layout_axis];
    f32 childen_total = 0.0f;
    for (Widget *child = widget->first; child; child = child->next) {
      childen_total += child->computed_size[layout_axis];
    }
    f32 extra = childen_total - total;
    if (extra < 0.0f) {
      extra = 0.0f;
    }

    f32 offset = 0.0f;
    for (Widget *child = widget->first; child; child = child->next) {
      child->computed_rel_pos[layout_axis] = offset;
      {
        f32 size = child->computed_size[layout_axis];
        f32 min_size = size * child->constraints[layout_axis].strickness;
        f32 new_size = size - extra;
        child->computed_size[layout_axis] = MAX(new_size, min_size);
        extra -= size - child->computed_size[layout_axis];
      }
      offset += child->computed_size[layout_axis];
    }
  }

  widget->screen_rect.min = min;
  widget->screen_rect.max = AddVec2(min, Vec2FromArray(widget->computed_size));

  if (!IsEmptyStr8(widget->text)) {
    DrawTextStr8(widget->screen_rect.min, widget->text, kDefaultTextHeight);
  }

  DrawRectLine(widget->screen_rect.min, widget->screen_rect.max, 0x00FF00FF,
               1.0);

  for (Widget *child = widget->first; child; child = child->next) {
    CalcScreenRectR(state, child, min);
    min.v[widget->layout_axis] += child->computed_size[widget->layout_axis];
  }
}

static void CalcScreenRect(UIState *state) {
  Vec2 min = V2(0, 0);
  CalcScreenRectR(state, state->root, min);
}

void EndUI(void) {
  UIState *state = GetUIState();

  CalcStandaloneSize(state);
  CalcParentBasedSize(state);
  // Third pass: calculate downwards-dependent sizes.
  CalcScreenRect(state);

  state->build_index++;
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

void BeginWidget(void) {
  UIState *state = GetUIState();

  Str8 key_str = state->next_widget_settings.key;
  ASSERT(!IsEmptyStr8(key_str) &&
         "Use SetNextWidgetKey to set a key for this widget");
  state->next_widget_settings.key = (Str8){0};

  Widget *parent = state->current;
  WidgetKey key = WidgetKeyFromStr8(parent->key, key_str);
  Widget *widget = GetWidgetByKey(state, key);
  if (!widget) {
    widget = GetOrPushWidget(state);
    widget->key = key;
    WidgetHashSlot *slot =
        &state->widget_hash_slots[key.hash % state->widget_hash_slot_size];
    APPEND_DOUBLY_LINKED_LIST(slot->first, slot->last, widget, hash_prev,
                              hash_next);
  }

  APPEND_DOUBLY_LINKED_LIST(parent->first, parent->last, widget, prev, next);
  widget->parent = parent;
  widget->last_touched_build_index = state->build_index;

  // Clear per frame state
  widget->first = widget->last = 0;

  widget->layout_axis = state->next_widget_settings.layout_axis;
  state->next_widget_settings.layout_axis = 0;

  for (u32 axis = 0; axis < kAxis2Count; ++axis) {
    if (state->next_widget_settings.constraints[axis].type) {
      widget->constraints[axis] = state->next_widget_settings.constraints[axis];
      state->next_widget_settings.constraints[axis] = (WidgetConstraint){0};
    }
  }

  if (!IsEmptyStr8(state->next_widget_settings.text)) {
    widget->text = state->next_widget_settings.text;
    state->next_widget_settings.text = (Str8){0};
  }

  state->current = widget;
}

void EndWidget(void) {
  UIState *state = GetUIState();

  ASSERT(state->current && state->current != state->root &&
         "Unmatched BeginWidget and EndWidget calls");

  state->current = state->current->parent;
}

Str8 GetNextWidgetKey(void) {
  UIState *state = GetUIState();
  Str8 result = state->next_widget_settings.key;
  return result;
}

void SetNextWidgetKey(Str8 key) {
  UIState *state = GetUIState();
  Arena *arena = GetBuildArena(state);
  state->next_widget_settings.key = PushStr8(arena, key);
}

WidgetConstraint GetNextWidgetConstraint(Axis2 axis) {
  UIState *state = GetUIState();
  WidgetConstraint result = state->next_widget_settings.constraints[axis];
  return result;
}

void SetNextWidgetConstraint(Axis2 axis, WidgetConstraint constraint) {
  UIState *state = GetUIState();

  state->next_widget_settings.constraints[axis] = constraint;
}

void SetNextWidgetLayoutAxis(Axis2 axis) {
  UIState *state = GetUIState();
  state->next_widget_settings.layout_axis = axis;
}

void SetNextWidgetTextContent(Str8 text) {
  UIState *state = GetUIState();
  Arena *arena = GetBuildArena(state);
  state->next_widget_settings.text = PushStr8(arena, text);
}
