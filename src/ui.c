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

  state->canvas_size = Vec2FromVec2I(GetCanvasSize());
  state->root = 0;
  state->current = 0;

  ResetArena(GetBuildArena(state));
}

const f32 kDefaultTextHeight = 16;

// static void CalcStandaloneSizeR(UIState *state, Widget *widget) {
//   for (u32 axis = 0; axis < kAxis2Count; ++axis) {
//     WidgetConstraint constraint = widget->constraints[axis];
//     switch (constraint.type) {
//       case kWidgetConstraintPixels: {
//         widget->computed_size[axis] = constraint.value;
//       } break;
//
//       case kWidgetConstraintTextContent: {
//         widget->computed_size[axis] =
//             GetTextMetricsStr8(widget->text,
//             kDefaultTextHeight).size.v[axis];
//       } break;
//
//       default: {
//       } break;
//     }
//   }
//
//   for (Widget *child = widget->first; child; child = child->next) {
//     CalcStandaloneSizeR(state, child);
//   }
// }
//
// static void CalcStandaloneSize(UIState *state) {
//   CalcStandaloneSizeR(state, state->root);
// }
//
// static void CalcParentBasedSizeR(UIState *state, Widget *widget) {
//   for (u32 axis = 0; axis < kAxis2Count; ++axis) {
//     WidgetConstraint constraint = widget->constraints[axis];
//     switch (constraint.type) {
//       case kWidgetConstraintPercentOfParent: {
//         widget->computed_size[axis] =
//             widget->parent->computed_size[axis] * constraint.value;
//       } break;
//
//       default: {
//       } break;
//     }
//   }
//
//   for (Widget *child = widget->first; child; child = child->next) {
//     CalcParentBasedSizeR(state, child);
//   }
// }
//
// static void CalcParentBasedSize(UIState *state) {
//   CalcParentBasedSizeR(state, state->root);
// }
//
// static void CalcScreenRectR(UIState *state, Widget *widget, Vec2 min) {
//   {
//     u32 layout_axis = widget->layout_axis;
//     f32 total = widget->computed_size[layout_axis];
//     f32 childen_total = 0.0f;
//     for (Widget *child = widget->first; child; child = child->next) {
//       childen_total += child->computed_size[layout_axis];
//     }
//     f32 extra = childen_total - total;
//     if (extra < 0.0f) {
//       extra = 0.0f;
//     }
//
//     f32 offset = 0.0f;
//     for (Widget *child = widget->first; child; child = child->next) {
//       child->computed_rel_pos[layout_axis] = offset;
//       {
//         f32 size = child->computed_size[layout_axis];
//         f32 min_size = size * child->constraints[layout_axis].strickness;
//         f32 new_size = size - extra;
//         child->computed_size[layout_axis] = MAX(new_size, min_size);
//         extra -= size - child->computed_size[layout_axis];
//       }
//       offset += child->computed_size[layout_axis];
//     }
//   }
//
//   widget->screen_rect.min = min;
//   widget->screen_rect.max = AddVec2(min,
//   Vec2FromArray(widget->computed_size));
//
//   if (!IsEmptyStr8(widget->text)) {
//     DrawTextStr8(widget->screen_rect.min, widget->text, kDefaultTextHeight);
//   }
//
//   DrawRectLine(widget->screen_rect.min, widget->screen_rect.max, 0x00FF00FF,
//                1.0);
//
//   for (Widget *child = widget->first; child; child = child->next) {
//     CalcScreenRectR(state, child, min);
//     min.v[widget->layout_axis] += child->computed_size[widget->layout_axis];
//   }
// }
//
// static void CalcScreenRect(UIState *state) {
//   Vec2 min = V2(0, 0);
//   CalcScreenRectR(state, state->root, min);
// }

static void LayoutWidget(UIState *state, Widget *widget, Vec2 min_size,
                         Vec2 max_size);

static void AssertNoChild(Widget *widget) {
  ASSERT(!widget->first && "No child widget expected");
}

static void AssertAtMostOneChild(Widget *widget) {
  ASSERT((!widget->first || widget->first && widget->first == widget->last) &&
         "At most one child widget expected");
}

static void LayoutText(Widget *widget, Vec2 min_size, Vec2 max_size) {
  AssertNoChild(widget);

  // TODO: Wrap text?

  TextMetrics metrics = GetTextMetricsStr8(widget->text, kDefaultTextHeight);
  widget->computed_size = MinVec2(MaxVec2(metrics.size, min_size), max_size);
  widget->computed_rel_pos = V2(0, 0);
}

static void LayoutWidget(UIState *state, Widget *widget, Vec2 min_size,
                         Vec2 max_size) {
  switch (widget->type) {
    case kWidgetContainer:
    case kWidgetCenter: {
      AssertAtMostOneChild(widget);

      Vec2 self_size = V2(0, 0);
      Vec2 child_max_size = max_size;
      for (u32 axis = 0; axis < kAxis2Count; ++axis) {
        if (widget->size.v[axis]) {
          // If widget has specific size, cap it within constraint and use that
          // as constraint for child.
          self_size.v[axis] = MaxF32(MinF32(max_size.x, widget->size.v[axis]),
                                     min_size.v[axis]);
          child_max_size.v[axis] = widget->size.v[axis];
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

      Vec2 child_size = V2(0, 0);
      Widget *child = widget->first;
      if (child) {
        // TODO: handle padding and margin.
        LayoutWidget(state, child, V2(0, 0), child_max_size);
        child_size = child->computed_size;
      }

      for (u32 axis = 0; axis < kAxis2Count; ++axis) {
        // If widget doesn't have specific size but has child, size itself
        // around the child.
        if (!widget->size.v[axis] && child_size.v[axis]) {
          widget->computed_size.v[axis] =
              MaxF32(child_size.v[axis], min_size.v[axis]);
        } else {
          widget->computed_size.v[axis] = self_size.v[axis];
        }
      }

      // Align center
      // TODO: Add other alignment
      if (child) {
        for (u32 axis = 0; axis < kAxis2Count; ++axis) {
          child->computed_rel_pos.v[axis] =
              (widget->computed_size.v[axis] - child_size.v[axis]) / 2.0;
        }
      }
    } break;

    case kWidgetText: {
      LayoutText(widget, min_size, max_size);
    } break;

    default: {
      UNREACHABLE;
    } break;
  }
}

static void RenderWidget(UIState *state, Widget *widget, Vec2 parent_pos) {
  Vec2 min = AddVec2(parent_pos, widget->computed_rel_pos);
  Vec2 max = AddVec2(min, widget->computed_size);
  widget->computed_screen_rect = (Rect2){.min = min, .max = max};

  if (widget->color) {
    DrawRect(min, max, widget->color);
  }

  if (!IsEmptyStr8(widget->text)) {
    // TODO: clip
    DrawTextStr8(min, widget->text, kDefaultTextHeight);
  }

  for (Widget *child = widget->first; child; child = child->next) {
    RenderWidget(state, child, min);
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

void BeginWidget(Str8 key_str, WidgetType type) {
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
  widget->type = type;
  widget->color = 0;
  widget->text = (Str8){0};

  state->current = widget;
}

void EndWidget(void) {
  UIState *state = GetUIState();

  ASSERT(state->current);
  state->current = state->current->parent;
}

WidgetType GetWidgetType(void) {
  UIState *state = GetUIState();
  ASSERT(state->current);
  return state->current->type;
}

void SetWidgetColor(u32 color) {
  UIState *state = GetUIState();
  ASSERT(state->current);
  state->current->color = color;
}

void SetWidgetSize(Vec2 size) {
  UIState *state = GetUIState();
  ASSERT(state->current);
  state->current->size = size;
}

void SetWidgetText(Str8 text) {
  UIState *state = GetUIState();
  ASSERT(state->current);

  Arena *arena = GetBuildArena(state);
  state->current->text = PushStr8(arena, text);
}
