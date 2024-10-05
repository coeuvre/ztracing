#include "src/ui.h"

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

typedef struct UIState UIState;
struct UIState {
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
};

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

b32 UIKeyEqual(UIKey a, UIKey b) {
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

void UIBeginFrame(void) {
  UIState *state = GetUIState();

  state->build_index += 1;
  state->content_scale = GetDrawContentScale();
  state->screen_size_in_pixel = Vec2FromVec2I(GetDrawSizeInPixel());
  state->screen_size =
      MulVec2(state->screen_size_in_pixel, 1.0f / state->content_scale);
  state->root = 0;
  state->current = 0;

  ResetArena(GetBuildArena(state));
}

const f32 kDefaultTextHeight = 16.0f;

static void AlignMainAxis(UIBox *box, Axis2 axis, UIMainAxisAlign align,
                          f32 children_size) {
  // TODO: Handle padding.
  f32 free = box->computed.size.v[axis] - children_size;
  f32 pos = 0.0f;
  switch (align) {
    case kUIMainAxisAlignStart: {
    } break;

    case kUIMainAxisAlignCenter: {
      pos = free / 2.0;
    } break;

    case kUIMainAxisAlignEnd: {
      pos = free;
    } break;

    default: {
      UNREACHABLE;
    } break;
  }

  for (UIBox *child = box->first; child; child = child->next) {
    child->computed.rel_pos.v[axis] = pos;
    pos += child->computed.size.v[axis];
  }
}

static void AlignCrossAxis(UIBox *box, Axis2 axis, UICrossAxisAlign align) {
  // TODO: Handle padding.
  for (UIBox *child = box->first; child; child = child->next) {
    switch (align) {
      case kUICrossAxisAlignStart:
      case kUICrossAxisAlignStretch: {
        child->computed.rel_pos.v[axis] = 0.0f;
      } break;

      case kUICrossAxisAlignCenter: {
        child->computed.rel_pos.v[axis] =
            (box->computed.size.v[axis] - child->computed.size.v[axis]) / 2.0f;
      } break;

      case kUICrossAxisAlignEnd: {
        child->computed.rel_pos.v[axis] =
            box->computed.size.v[axis] - child->computed.size.v[axis];
      } break;

      default: {
        UNREACHABLE;
      } break;
    }
  }
}

static void LayoutBox(UIState *state, UIBox *box, Vec2 min_size, Vec2 max_size,
                      b32 unbounded) {
  box->computed.min_size = min_size;
  box->computed.max_size = max_size;
  box->computed.unbounded = unbounded;

  Axis2 main_axis = box->build.main_axis;
  Axis2 cross_axis = (main_axis + 1) % kAxis2Count;

  Vec2 self_size = V2(0, 0);
  Vec2 child_max_size = max_size;
  for (int axis = 0; axis < kAxis2Count; ++axis) {
    if (box->build.size.v[axis] != kUISizeUndefined) {
      // If widget has specific size, use that as constraint for children and
      // size itself within the constraint.
      child_max_size.v[axis] = box->build.size.v[axis];
      self_size.v[axis] =
          MaxF32(MinF32(max_size.x, box->build.size.v[axis]), min_size.v[axis]);
    } else {
      // Otherwise, pass down the constraint to children and ...
      child_max_size.v[axis] = max_size.v[axis];

      if (axis == main_axis) {
        // ... for main axis,
        if (unbounded) {
          // if constraint is unbounded, make it as small as possible.
          self_size.v[axis] = min_size.v[axis];
        } else {
          // otherwise, make it as large as possible.
          self_size.v[axis] = max_size.v[axis];
        }
      } else {
        // for cross axis, make it as small as possible
        self_size.v[axis] = min_size.v[axis];
      }
    }
  }

  // TODO: handle padding and margin.
  f32 child_main_axis_size = 0.0f;
  f32 child_cross_axis_max = 0.0f;
  if (box->first) {
    f32 total_flex = 0;
    f32 child_main_axis_free = child_max_size.v[main_axis];

    // First pass: layout non-flex children
    for (UIBox *child = box->first; child; child = child->next) {
      // If child doesn't have flex, doesn't apply any constraint on the child.
      total_flex += child->build.flex;
      if (!child->build.flex) {
        Vec2 max_size;
        max_size.v[main_axis] = child_main_axis_free;
        max_size.v[cross_axis] = child_max_size.v[cross_axis];
        Vec2 min_size;
        min_size.v[main_axis] = 0.0f;
        min_size.v[cross_axis] = 0.0f;
        if (box->build.cross_axis_align == kUICrossAxisAlignStretch) {
          min_size.v[cross_axis] = max_size.v[cross_axis];
        }
        LayoutBox(state, child, min_size, max_size, 1);

        child_main_axis_free -= child->computed.size.v[main_axis];
        child_main_axis_size += child->computed.size.v[main_axis];
        child_cross_axis_max =
            MaxF32(child_cross_axis_max, child->computed.size.v[cross_axis]);
      }
    }

    // Second pass: layout flex children
    for (UIBox *child = box->first; child; child = child->next) {
      if (child->build.flex) {
        Vec2 max_size;
        max_size.v[main_axis] =
            child->build.flex / total_flex * child_main_axis_free;
        max_size.v[cross_axis] = child_max_size.v[cross_axis];
        Vec2 min_size;
        min_size.v[main_axis] = max_size.v[main_axis];
        min_size.v[cross_axis] = 0.0f;
        if (box->build.cross_axis_align == kUICrossAxisAlignStretch) {
          min_size.v[cross_axis] = max_size.v[cross_axis];
        }
        LayoutBox(state, child, min_size, max_size, 0);

        child_main_axis_size += child->computed.size.v[main_axis];
        child_cross_axis_max =
            MaxF32(child_cross_axis_max, child->computed.size.v[cross_axis]);
      }
    }

    box->computed.text_size = V2(0, 0);
  } else if (!IsEmptyStr8(box->build.text)) {
    // TODO: constraint text size within [(0, 0), child_max_size]

    // Use pixel unit to measure text
    TextMetrics metrics = GetTextMetricsStr8(
        box->build.text, kDefaultTextHeight * state->content_scale);
    Vec2 text_size_in_pixel = metrics.size;
    Vec2 text_size = MulVec2(text_size_in_pixel, 1.0f / state->content_scale);
    text_size = MinVec2(text_size, child_max_size);

    box->computed.text_size = text_size;
    child_main_axis_size = text_size.v[main_axis];
    child_cross_axis_max = text_size.v[cross_axis];
  }

  Vec2 child_size;
  child_size.v[main_axis] = child_main_axis_size;
  child_size.v[cross_axis] = child_cross_axis_max;

  for (int axis = 0; axis < kAxis2Count; ++axis) {
    // Size itself around children if it doesn't have specific size
    if (!box->build.size.v[axis] && child_size.v[axis] != kUISizeUndefined) {
      box->computed.size.v[axis] = MinF32(
          MaxF32(child_size.v[axis], min_size.v[axis]), max_size.v[axis]);
    } else {
      box->computed.size.v[axis] = self_size.v[axis];
    }
  }

  AlignMainAxis(box, main_axis, box->build.main_axis_align,
                child_main_axis_size);
  AlignCrossAxis(box, cross_axis, box->build.cross_axis_align);
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
  // DrawRectLine(min, max, 0xFFFF00FF, 1.0f);

  if (box->first) {
    for (UIBox *child = box->first; child; child = child->next) {
      RenderBox(state, child, min_in_pixel);
    }
  } else if (!IsEmptyStr8(box->build.text)) {
    // TODO: clip

    // Always center align text
    Vec2 pos_in_pixel =
        AddVec2(min_in_pixel,
                MulVec2(SubVec2(box->computed.size, box->computed.text_size),
                        0.5f * state->content_scale));
    DrawTextStr8(pos_in_pixel, box->build.text,
                 kDefaultTextHeight * state->content_scale);
  }
}

#if 0
static void UIDebugPrintR(UIBox *box, u32 level) {
  TempMemory scratch = BeginScratch(0, 0);
  for (u32 i = 0; i < level; ++i) {
    OutputDebugStringA("  ");
  }
  OutputDebugStringA(box->build.key_str.ptr);
  OutputDebugStringA(
      PushStr8F(scratch.arena,
                " min_size=(%.2f, %.2f), max_size=(%.2f, %.2f), unbounded=%d, "
                "size=(%.2f, %.2f) rel_pos=(%.2f, %.2f)\n",
                box->computed.min_size.x, box->computed.min_size.y,
                box->computed.max_size.x, box->computed.max_size.y,
                box->computed.unbounded, box->computed.size.x,
                box->computed.size.y, box->computed.rel_pos.x,
                box->computed.rel_pos.y)
          .ptr);
  for (UIBox *child = box->first; child; child = child->next) {
    UIDebugPrintR(child, level + 1);
  }
  EndScratch(scratch);
}

static void UIDebugPrint(UIState *state) {
  if (state->root) {
    UIDebugPrintR(state->root, 0);
  }
  exit(0);
}
#endif

void UIEndFrame(void) {
  UIState *state = GetUIState();
  ASSERTF(!state->current, "Mismatched Begin/End calls");

  if (state->root) {
    LayoutBox(state, state->root, state->screen_size, state->screen_size, 0);
    state->root->computed.rel_pos = V2(0, 0);
  }

  // UIDebugPrint(state);
}

void UIRender(void) {
  UIState *state = GetUIState();
  if (state->root) {
    RenderBox(state, state->root, V2(0, 0));
  }
}

static UIBox *GetBoxByKey(UIState *state, UIKey key) {
  UIBox *result = 0;
  if (!UIKeyEqual(key, UIKeyZero())) {
    BoxHashSlot *slot =
        &state->box_hash_slots[key.hash % state->box_hash_slots_count];
    for (UIBox *box = slot->first; box; box = box->hash_next) {
      if (UIKeyEqual(box->key, key)) {
        result = box;
        break;
      }
    }
  }
  return result;
}

void UIBeginBox(Str8 key_str) {
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

  if (parent) {
    APPEND_DOUBLY_LINKED_LIST(parent->first, parent->last, box, prev, next);
  } else {
    ASSERT(!state->root && "More than one root widget provided");
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

void UIEndBox(void) {
  UIState *state = GetUIState();

  ASSERT(state->current);
  state->current = state->current->parent;
}

void UISetColor(DrawColor color) {
  UIState *state = GetUIState();
  ASSERT(state->current);
  state->current->build.color = color;
}

void UISetSize(Vec2 size) {
  UIState *state = GetUIState();
  ASSERT(state->current);
  state->current->build.size = size;
}

void UISetText(Str8 text) {
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

void UISetMainAxisAlignment(UIMainAxisAlign main_axis_align) {
  UIState *state = GetUIState();
  ASSERT(state->current);

  state->current->build.main_axis_align = main_axis_align;
}

void UISetCrossAxisAlignment(UICrossAxisAlign cross_axis_align) {
  UIState *state = GetUIState();
  ASSERT(state->current);

  state->current->build.cross_axis_align = cross_axis_align;
}

void UISetFlex(f32 flex) {
  UIState *state = GetUIState();
  ASSERT(state->current);

  state->current->build.flex = flex;
}
