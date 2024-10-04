#include "src/ui.h"

#include "src/assert.h"
#include "src/draw.h"
#include "src/list.h"
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
  Vec2 canvas_size;
  UIBox *root;
  UIBox *current;
};

UIState g_ui_state;

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
  UIState *state = &g_ui_state;
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
  state->canvas_size = Vec2FromVec2I(GetCanvasSize());
  state->root = 0;
  state->current = 0;

  ResetArena(GetBuildArena(state));
}

const f32 kDefaultTextHeight = 16;

static void AlignMainAxis(UIBox *box, Axis2 axis, UIAlign align,
                          f32 children_size) {
  // TODO: Handle padding.
  f32 free = box->computed_size.v[axis] - children_size;
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

  for (UIBox *child = box->first; child; child = child->next) {
    child->computed_rel_pos.v[axis] = pos;
    pos += child->computed_size.v[axis];
  }
}

static void AlignCrossAxis(UIBox *box, Axis2 axis, UIAlign align,
                           f32 children_size) {
  // TODO: Handle padding.
  f32 free = box->computed_size.v[axis] - children_size;
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

  for (UIBox *child = box->first; child; child = child->next) {
    child->computed_rel_pos.v[axis] = pos;
  }
}

static void LayoutBox(UIState *state, UIBox *box, Vec2 min_size,
                      Vec2 max_size) {
  Vec2 self_size = V2(0, 0);
  Vec2 child_max_size = max_size;
  for (u32 axis = 0; axis < kAxis2Count; ++axis) {
    if (box->build.size.v[axis]) {
      // If widget has specific size, cap it within constraint and use that
      // as constraint for child.
      self_size.v[axis] =
          MaxF32(MinF32(max_size.x, box->build.size.v[axis]), min_size.v[axis]);
      child_max_size.v[axis] = box->build.size.v[axis];
    } else {
      // Otherwise, pass down the constraint and ...
      if (max_size.v[axis] >= UI_SIZE_MAX) {
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
  if (box->first) {
    for (UIBox *child = box->first; child; child = child->next) {
      LayoutBox(state, child, V2(0, 0), child_max_size);

      // Row direction
      // TODO: Column direction
      child->computed_rel_pos.x = child_size.x;
      child_size.x += child->computed_size.x;
      child_size.y = MaxF32(child_size.y, child->computed_size.y);
    }
  } else if (!IsEmptyStr8(box->build.text)) {
    // TODO: constraint text size within [(0, 0), child_max_size]
    TextMetrics metrics =
        GetTextMetricsStr8(box->build.text, kDefaultTextHeight);
    child_size = MinVec2(metrics.size, child_max_size);
  }

  for (u32 axis = 0; axis < kAxis2Count; ++axis) {
    // If widget doesn't have specific size but has child, size itself
    // around the child.
    if (!box->build.size.v[axis] && child_size.v[axis]) {
      box->computed_size.v[axis] = MaxF32(child_size.v[axis], min_size.v[axis]);
    } else {
      box->computed_size.v[axis] = self_size.v[axis];
    }
  }

  Axis2 main = box->build.main_axis;
  Axis2 cross = (main + 1) % kAxis2Count;
  AlignMainAxis(box, main, box->build.aligns[main], child_size.v[main]);
  AlignCrossAxis(box, cross, box->build.aligns[cross], child_size.v[cross]);
}

static void RenderBox(UIState *state, UIBox *box, Vec2 parent_pos) {
  Vec2 min = AddVec2(parent_pos, box->computed_rel_pos);
  Vec2 max = AddVec2(min, box->computed_size);
  box->computed_screen_rect = (Rect2){.min = min, .max = max};

  if (box->build.color) {
    DrawRect(min, max, box->build.color);
  }

  if (box->first) {
    for (UIBox *child = box->first; child; child = child->next) {
      RenderBox(state, child, min);
    }
  } else if (!IsEmptyStr8(box->build.text)) {
    // TODO: clip
    DrawTextStr8(min, box->build.text, kDefaultTextHeight);
  }
}

void UIEndFrame(void) {
  UIState *state = GetUIState();
  ASSERT(!state->current && "Mismatched Begin/End calls");

  if (state->root) {
    LayoutBox(state, state->root, state->canvas_size, state->canvas_size);
    state->root->computed_rel_pos = V2(0, 0);
    state->root->computed_screen_rect =
        (Rect2){.min = V2(0, 0), .max = state->canvas_size};

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

  state->current = box;
}

void UIEndBox(void) {
  UIState *state = GetUIState();

  ASSERT(state->current);
  state->current = state->current->parent;
}

void UISetColor(u32 color) {
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
