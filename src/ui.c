#include "src/ui.h"

#include <string.h>

#include "src/assert.h"
#include "src/list.h"
#include "src/math.h"
#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"

typedef struct UIWidgetHashSlot UIWidgetHashSlot;
struct UIWidgetHashSlot {
  UIWidgetHashSlot *prev;
  UIWidgetHashSlot *next;
  UIWidget *first;
  UIWidget *last;
};

typedef struct UIWidgetHashMap {
  u32 total_count;
  u32 slots_count;
  UIWidgetHashSlot *slots;
} UIWidgetHashMap;

typedef struct UIFrame {
  Arena arena;
  UIWidgetHashMap cache;
  UIWidget *root;
  UIWidget *current;
} UIFrame;

typedef struct UIState {
  UIFrame frames[2];
  u64 frame_index;
  UIFrame *current_frame;
  UIFrame *last_frame;

  Vec2 viewport_size;
} UIState;

THREAD_LOCAL UIState t_ui_state;

static inline UIState *ui_state_get(void) { return &t_ui_state; }

static inline UIFrame *ui_frame_get(void) {
  UIState *state = ui_state_get();
  return state->current_frame;
}

void ui_set_viewport_size(Vec2 viewport_size) {
  UIState *state = ui_state_get();
  state->viewport_size = viewport_size;
}

void ui_begin_frame(void) {
  UIState *state = ui_state_get();
  if (state->current_frame) {
    ASSERT(!state->current_frame->current);
  }

  state->frame_index += 1;
  state->current_frame =
      state->frames + (state->frame_index % ARRAY_COUNT(state->frames));
  state->last_frame =
      state->frames + ((state->frame_index - 1) % ARRAY_COUNT(state->frames));

  UIFrame *frame = state->current_frame;
  arena_clear(&frame->arena);

  frame->cache = (UIWidgetHashMap){0};
  // TODO: Adjust the number of slots based on last frame.
  frame->cache.slots_count = 4096;
  frame->cache.slots = arena_push_array(&frame->arena, UIWidgetHashSlot,
                                        frame->cache.slots_count);
  frame->root = frame->current = 0;
}

static void ui_widget_layout(UIWidget *widget, UIBoxConstraints constraints) {
  widget->vtable->layout(widget, constraints);
}

void ui_end_frame(void) {
  UIState *state = ui_state_get();
  UIFrame *frame = state->current_frame;
  ASSERTF(!frame->current, "Mismatched begin/end calls");

  // Layout and render
  if (frame->root) {
    ui_widget_layout(frame->root,
                     ui_box_constraints_make_tight(state->viewport_size));
  }
}

static UIKey ui_key_from_str8(UIKey seed, Str8 str) {
  UIKey result = seed;
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

static UIKey ui_key_from_u8(UIKey seed, u8 ch) {
  u8 str[2] = {ch, 0};
  UIKey key = ui_key_from_str8(seed, (Str8){.ptr = str, .len = 1});
  return key;
}

static UIKey ui_key_from_u32(UIKey seed, u32 num) {
  UIKey key = seed;
  u32 s = num;
  while (s > 0) {
    key = ui_key_from_u8(key, (u8)(s & 0xFF));
    s = s >> 8;
  }
  return key;
}

// TODO: Global Key
static UIKey ui_key_make_local(UIKey seed, u32 seq, const char *tag, Str8 id) {
  // key = seed + tag + (id || seq)
  UIKey key = seed;
  key = ui_key_from_str8(key, str8_from_cstr(tag));
  if (!str8_is_empty(id)) {
    key = ui_key_from_str8(key, id);
  } else {
    key = ui_key_from_u32(key, seq);
  }
  return key;
}

static void ui_widget_layout_stub(void *widget, UIBoxConstraints constraints) {
  (void)widget;
  (void)constraints;
  ASSERTF(false, "You must override layout method.");
}

static UIWidgetVTable ui_widget_vtable = (UIWidgetVTable){
    .layout = &ui_widget_layout_stub,
};

static UIWidget *ui_widget_get(UIFrame *frame, UIKey key) {
  UIWidget *result = 0;
  UIWidgetHashMap *cache = &frame->cache;
  if (!ui_key_is_zero(key) && cache->slots) {
    UIWidgetHashSlot *slot = cache->slots + (key.hash % cache->slots_count);
    for (UIWidget *widget = slot->first; widget; widget = widget->hash.next) {
      if (ui_key_is_equal(widget->key, key)) {
        result = widget;
        break;
      }
    }
  }
  return result;
}

static UIWidget *ui_widget_push(UIFrame *frame, usize size, UIKey key) {
  UIWidget *widget = arena_push(&frame->arena, size, 0);
  widget->key = key;

  UIWidgetHashSlot *slot =
      frame->cache.slots + (key.hash % frame->cache.slots_count);
  DLL_APPEND(slot->first, slot->last, widget, hash.prev, hash.next);
  ++frame->cache.total_count;

  return widget;
}

static void *ui_widget_begin(const char *tag, usize size, UIKey key) {
  UIState *state = ui_state_get();
  UIFrame *frame = state->current_frame;
  UIFrame *last_frame = state->last_frame;
  UIWidget *parent = frame->current;

  if (ui_key_is_zero(key)) {
    UIKey seed = key;
    if (parent) {
      seed = parent->key;
    }
    u32 seq = 0;
    if (parent) {
      seq = parent->child_count;
    }
    key = ui_key_make_local(seed, seq, tag, str8_zero());
  }

  DEBUG_ASSERT(size >= sizeof(UIWidget));
  UIWidget *widget = ui_widget_push(frame, size, key);
  widget->vtable = &ui_widget_vtable;
  if (parent) {
    DLL_APPEND(parent->tree.first, parent->tree.last, widget, tree.prev,
               tree.next);
    ++parent->child_count;
    widget->tree.parent = parent;
  } else {
    frame->root = widget;
  }
  widget->tag = tag;

  // Copy additional data from last frame
  usize additional_bytes = size - sizeof(UIWidget);
  if (additional_bytes > 0) {
    UIWidget *last_widget = ui_widget_get(last_frame, key);
    if (last_widget) {
      ASSERTF(strcmp(last_widget->tag, tag) == 0,
              "tag is changed from %s to %s", last_widget->tag, tag);
      memory_copy(widget + 1, last_widget + 1, additional_bytes);
    }
  }

  frame->current = widget;

  return widget;
}

static void ui_widget_end(const char *tag) {
  UIFrame *frame = ui_frame_get();
  UIWidget *widget = frame->current;
  ASSERT(widget);
  ASSERTF(strcmp(widget->tag, tag) == 0,
          "Mismatched begin/end calls. Begin with %s, end with %s", widget->tag,
          tag);

  frame->current = widget->tree.parent;
}

/// Returns the width that both satisfies the constraints and is as close as
/// possible to the given width.
static inline f32 ui_box_constraints_constrain_width(
    UIBoxConstraints constraints, f32 width) {
  return f32_clamp(width, constraints.min.x, constraints.max.x);
}

/// Returns the height that both satisfies the constraints and is as close as
/// possible to the given height.
static inline f32 ui_box_constraints_constrain_height(
    UIBoxConstraints constraints, f32 height) {
  return f32_clamp(height, constraints.min.x, constraints.max.x);
}

/// The biggest size that satisfies the constraints.
static inline Vec2 ui_box_constraints_get_biggest(
    UIBoxConstraints constraints) {
  return v2(ui_box_constraints_constrain_width(constraints, F32_INFINITY),
            ui_box_constraints_constrain_height(constraints, F32_INFINITY));
}

static inline UIBoxConstraints ui_box_constraints_make_for_non_flex_child(
    UIFlex *flex, UIBoxConstraints constraints) {
  bool should_fill_cross_axis = false;
  if (flex->cross_axis_alignment == UI_CROSS_AXIS_ALIGNMENT_STRETCH) {
    should_fill_cross_axis = true;
  }

  UIBoxConstraints result;
  switch (flex->direction) {
    case UI_AXIS_HORIZONTAL: {
      if (should_fill_cross_axis) {
        result = ui_box_constraints_make_tight_height(constraints.max.y);
      } else {
        result = (UIBoxConstraints){
            .min = v2(0, 0),
            .max = v2(F32_INFINITY, constraints.max.y),
        };
      }
    } break;
    case UI_AXIS_VERTICAL: {
      if (should_fill_cross_axis) {
        result = ui_box_constraints_make_tight_width(constraints.max.x);
      } else {
        result = (UIBoxConstraints){
            .min = v2(0, 0),
            .max = v2(constraints.max.x, F32_INFINITY),
        };
      }
    } break;
    default:
      UNREACHABLE;
  }
  return result;
}

typedef struct AxisSize {
  f32 main;
  f32 cross;
} AxisSize;

static inline AxisSize axis_size_make(f32 main, f32 cross) {
  return (AxisSize){.main = main, .cross = cross};
}

static inline Vec2 convert_size(Vec2 size, UIAxis direction) {
  switch (direction) {
    case UI_AXIS_HORIZONTAL: {
      return size;
    } break;
    case UI_AXIS_VERTICAL: {
      return v2(size.y, size.x);
    } break;
    default:
      UNREACHABLE;
  }
}

static inline AxisSize axis_size_from_size(Vec2 size, UIAxis direction) {
  Vec2 converted = convert_size(size, direction);
  return axis_size_make(converted.x, converted.y);
}

static void ui_flex_layout(void *widget, UIBoxConstraints constraints) {
  UIFlex *flex = widget;

  /// 1. Layout each child with a null or zero flex factor with unbounded main
  ///    axis constraints and the incoming cross axis constraints. If the
  ///    [crossAxisAlignment] is [CrossAxisAlignment.stretch], instead use tight
  ///    cross axis constraints that match the incoming max extent in the cross
  ///    axis.

  // Determine used flex factor, size inflexible items, calculate free space.
  f32 max_main_size;
  switch (flex->direction) {
    case UI_AXIS_HORIZONTAL: {
      max_main_size =
          ui_box_constraints_constrain_width(constraints, F32_INFINITY);
    } break;
    case UI_AXIS_VERTICAL: {
      max_main_size =
          ui_box_constraints_constrain_height(constraints, F32_INFINITY);
    } break;
    default:
      UNREACHABLE;
  }
  bool can_flex = f32_is_finite(max_main_size);
  UIBoxConstraints non_flex_child_constraints =
      ui_box_constraints_make_for_non_flex_child(flex, constraints);
  // TODO: Baseline aligned

  // The first pass lays out non-flex children and computes total flex.
  i32 total_flex = 0;
  UIWidget *first_flex_child = 0;
  // Initially, accumulatedSize is the sum of the spaces between children in the
  // main axis.
  AxisSize accumulated_size =
      axis_size_make(flex->spacing * (flex->widget.child_count - 1), 0.0f);
  for (UIWidget *child = flex->widget.tree.first; child;
       child = child->tree.next) {
    i32 child_flex = 0;
    if (can_flex && child->vtable->get_flex_data) {
      UIFlexData flex_data = child->vtable->get_flex_data(child);
      child_flex = flex_data.flex;
    }

    if (child_flex > 0) {
      total_flex += child_flex;
      if (!first_flex_child) {
        first_flex_child = child;
      }
    } else {
      ui_widget_layout(child, non_flex_child_constraints);
      AxisSize child_size = axis_size_from_size(child->size, flex->direction);

      accumulated_size.main += child_size.main;
      accumulated_size.cross += child_size.cross;
    }
  }

  DEBUG_ASSERT((total_flex == 0) == (first_flex_child == 0));
  DEBUG_ASSERT(first_flex_child == 0 || can_flex);

  // The second pass distributes free space to flexible children.

  // Position all children in visual order: starting from the top-left child and
  // work towards the child that's farthest away from the origin.
}

static UIWidgetVTable ui_flex_vtable = (UIWidgetVTable){
    .layout = &ui_flex_layout,
};

void ui_flex_begin(UIFlexProps props) {
  UIFlex *flex = ui_widget_begin("Flex", sizeof(UIFlex), props.key);
  flex->widget.vtable = &ui_flex_vtable;
  flex->direction = props.direction;
  flex->main_axis_alignment = props.main_axis_alignment;
  flex->main_axis_size = props.main_axis_size;
  flex->cross_axis_alignment = props.cross_axis_alignment;
  flex->spacing = props.spacing;
}

void ui_flex_end(void) { ui_widget_end("Flex"); }
