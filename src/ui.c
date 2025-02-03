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

  Vec2 viewport_min;
  Vec2 viewport_max;
} UIState;

THREAD_LOCAL UIState t_ui_state;

static inline UIState *ui_state_get(void) { return &t_ui_state; }

static inline UIFrame *ui_frame_get(void) {
  UIState *state = ui_state_get();
  return state->current_frame;
}

void ui_set_viewport(Vec2 min, Vec2 max) {
  UIState *state = ui_state_get();
  state->viewport_min = min;
  state->viewport_max = max;
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
  UIWidgetVTable *vtable = widget->vtable;
  while (vtable) {
    if (vtable->layout) {
      vtable->layout(widget, constraints);
      break;
    }
    vtable = vtable->parent;
  }
}

static void ui_widget_paint(UIWidget *widget, UIPaintingContext *context,
                            Vec2 offset) {
  UIWidgetVTable *vtable = widget->vtable;
  while (vtable) {
    if (vtable->paint) {
      vtable->paint(widget, context, offset);
      break;
    }
    vtable = vtable->parent;
  }
}

static void *ui_is_instance_of(UIWidget *widget,
                               UIWidgetVTable *target_vtable) {
  UIWidgetVTable *vtable = widget->vtable;
  while (vtable) {
    if (vtable == target_vtable) {
      return (void *)widget;
    }
    vtable = vtable->parent;
  }
  return 0;
}

void ui_end_frame(void) {
  UIState *state = ui_state_get();
  UIFrame *frame = state->current_frame;
  ASSERTF(!frame->current, "Mismatched begin/end calls");

  // Layout and paint
  if (frame->root) {
    Vec2 viewport_size = vec2_sub(state->viewport_max, state->viewport_min);
    ui_widget_layout(frame->root, ui_box_constraints_make_tight(viewport_size));
    frame->root->offset = vec2_zero();

    UIPaintingContext context = {0};
    ui_widget_paint(frame->root, &context, state->viewport_min);
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

static void ui_widget_layout_default(void *widget,
                                     UIBoxConstraints constraints) {
  // The default layout just stacks children.
  Vec2 max_size = vec2_zero();
  UIWidget *w = widget;
  for (UIWidget *child = w->tree.first; child; child = child->tree.next) {
    ui_widget_layout(child, constraints);
    child->offset = vec2_zero();

    max_size = vec2_max(max_size, child->size);
  }

  w->size = max_size;
}

static void ui_widget_paint_default(void *widget, UIPaintingContext *context,
                                    Vec2 offset) {
  UIWidget *w = widget;
  for (UIWidget *child = w->tree.first; child; child = child->tree.next) {
    ui_widget_paint(child, context, vec2_add(offset, child->offset));
  }
}

static UIWidgetVTable ui_widget_vtable = (UIWidgetVTable){
    .layout = &ui_widget_layout_default,
    .paint = &ui_widget_paint_default,
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

static void *ui_widget_begin(UIWidgetVTable *vtable, usize size, UIKey key) {
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
    key = ui_key_make_local(seed, seq, vtable->name, str8_zero());
  }

  DEBUG_ASSERT(size >= sizeof(UIWidget));
  UIWidget *widget = ui_widget_push(frame, size, key);
  widget->vtable = vtable;
  if (parent) {
    DLL_APPEND(parent->tree.first, parent->tree.last, widget, tree.prev,
               tree.next);
    ++parent->child_count;
    widget->tree.parent = parent;
  } else {
    frame->root = widget;
  }

  // Copy additional data from last frame
  usize additional_bytes = size - sizeof(UIWidget);
  if (additional_bytes > 0) {
    UIWidget *last_widget = ui_widget_get(last_frame, key);
    if (last_widget) {
      ASSERTF(strcmp(last_widget->vtable->name, vtable->name) == 0,
              "tag is changed from %s to %s", last_widget->vtable->name,
              vtable->name);
      memory_copy(widget + 1, last_widget + 1, additional_bytes);
    }
  }

  frame->current = widget;

  return widget;
}

static void ui_widget_end(UIWidgetVTable *vtable) {
  UIFrame *frame = ui_frame_get();
  UIWidget *widget = frame->current;
  ASSERT(widget);
  ASSERTF(strcmp(widget->vtable->name, vtable->name) == 0,
          "Mismatched begin/end calls. Begin with %s, end with %s",
          widget->vtable->name, vtable->name);

  frame->current = widget->tree.parent;
}

static UIWidgetVTable ui_flexible_vtable = (UIWidgetVTable){
    .parent = &ui_widget_vtable,
    .name = "Flexible",
};

static inline UIFlexible *ui_flexible_from_widget(UIWidget *widget) {
  return ui_is_instance_of(widget, &ui_flexible_vtable);
}

void ui_flexible_begin(UIFlexibleProps props) {
  UIFlexible *flexible =
      ui_widget_begin(&ui_flexible_vtable, sizeof(UIFlexible), props.key);
  flexible->flex = props.flex;
  flexible->fit = props.fit;
}

void ui_flexible_end(void) { ui_widget_end(&ui_flexible_vtable); }

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

static inline UIBoxConstraints ui_box_constraints_make_for_flex_child(
    UIFlex *flex, UIBoxConstraints constraints, f32 max_child_extent,
    UIFlexible *flexible) {
  DEBUG_ASSERT(flexible->flex > 0);
  DEBUG_ASSERT(max_child_extent >= 0.0f);
  f32 min_child_extent = 0.0;
  if (flexible->fit == UI_FLEX_FIT_TIGHT) {
    min_child_extent = max_child_extent;
  }
  bool should_fill_cross_axis = false;
  if (flex->cross_axis_alignment == UI_CROSS_AXIS_ALIGNMENT_STRETCH) {
    should_fill_cross_axis = true;
  }
  UIBoxConstraints result;
  if (flex->direction == UI_AXIS_HORIZONTAL) {
    result = (UIBoxConstraints){
        .min = v2(min_child_extent,
                  should_fill_cross_axis ? constraints.max.y : 0),
        .max = v2(max_child_extent, constraints.max.y),
    };
  } else {
    result = (UIBoxConstraints){
        .min = v2(should_fill_cross_axis ? constraints.max.x : 0,
                  min_child_extent),
        .max = v2(constraints.max.y, max_child_extent),
    };
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

static AxisSize axis_size_constrains(AxisSize axis_size,
                                     UIBoxConstraints constraints,
                                     UIAxis direction) {
  UIBoxConstraints effective_constraints = constraints;
  if (direction != UI_AXIS_HORIZONTAL) {
    effective_constraints = ui_box_constraints_flip(constraints);
  }
  Vec2 constrained_size = ui_box_constraints_constrain(
      effective_constraints, v2(axis_size.main, axis_size.cross));
  return axis_size_make(constrained_size.x, constrained_size.y);
}

typedef struct UIFlexLayoutSize {
  AxisSize axis_size;
  f32 main_axis_free_space;
  bool can_flex;
  f32 space_per_flex;
} UIFlexLayoutSize;

static UIFlexLayoutSize ui_flex_compute_size(UIFlex *flex,
                                             UIBoxConstraints constraints) {
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
  // Initially, accumulated_size is the sum of the spaces between children in
  // the main axis.
  AxisSize accumulated_size =
      axis_size_make(flex->spacing * (flex->widget.child_count - 1), 0.0f);
  for (UIWidget *child = flex->widget.tree.first; child;
       child = child->tree.next) {
    i32 child_flex = 0;
    if (can_flex) {
      UIFlexible *flexible = ui_flexible_from_widget(child);
      if (flexible) {
        child_flex = flexible->flex;
      }
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
  f32 flex_space = f32_max(0.0f, max_main_size - accumulated_size.main);
  f32 space_per_flex = flex_space / total_flex;
  for (UIWidget *child = flex->widget.tree.first; child && total_flex > 0;
       child = child->tree.next) {
    UIFlexible *flexible = ui_flexible_from_widget(child);
    if (!flexible || flexible->flex <= 0) {
      continue;
    }
    total_flex -= flexible->flex;
    DEBUG_ASSERT(f32_is_finite(space_per_flex));
    f32 max_child_extent = space_per_flex * flexible->flex;
    DEBUG_ASSERT(flexible->fit == UI_FLEX_FIT_LOOSE ||
                 max_child_extent < F32_INFINITY);
    UIBoxConstraints child_constraints = ui_box_constraints_make_for_flex_child(
        flex, constraints, max_child_extent, flexible);
    ui_widget_layout(child, child_constraints);
    AxisSize child_size = axis_size_from_size(child->size, flex->direction);

    accumulated_size.main += child_size.main;
    accumulated_size.cross += child_size.cross;
  }
  DEBUG_ASSERT(total_flex == 0);

  f32 ideal_main_size;
  if (flex->main_axis_size == UI_MAIN_AXIS_SIZE_MAX &&
      f32_is_finite(max_main_size)) {
    ideal_main_size = max_main_size;
  } else {
    ideal_main_size = accumulated_size.main;
  }

  AxisSize axis_size = axis_size_make(ideal_main_size, accumulated_size.cross);
  AxisSize constrained_axis_size =
      axis_size_constrains(axis_size, constraints, flex->direction);

  return (UIFlexLayoutSize){
      .axis_size = constrained_axis_size,
      .main_axis_free_space =
          constrained_axis_size.main - accumulated_size.main,
      .can_flex = can_flex,
      .space_per_flex = can_flex ? space_per_flex : 0,
  };
}

static void ui_flex_distribute_space(UIMainAxisAlignment main_axis_alignment,
                                     f32 free_space, u32 item_count,
                                     bool flipped, f32 spacing,
                                     f32 *leading_space, f32 *between_space) {
  switch (main_axis_alignment) {
    case UI_MAIN_AXIS_ALIGNMENT_START: {
      if (flipped) {
        *leading_space = free_space;
      } else {
        *leading_space = 0;
      }
      *between_space = spacing;
    } break;

    case UI_MAIN_AXIS_ALIGNMENT_END: {
      ui_flex_distribute_space(UI_MAIN_AXIS_ALIGNMENT_START, free_space,
                               item_count, !flipped, spacing, leading_space,
                               between_space);
    } break;

    case UI_MAIN_AXIS_ALIGNMENT_SPACE_BETWEEN: {
      if (item_count < 2) {
        ui_flex_distribute_space(UI_MAIN_AXIS_ALIGNMENT_START, free_space,
                                 item_count, flipped, spacing, leading_space,
                                 between_space);
      } else {
        *leading_space = 0;
        *between_space = free_space / (item_count - 1) + spacing;
      }
    } break;

    case UI_MAIN_AXIS_ALIGNMENT_SPACE_AROUND: {
      if (item_count == 0) {
        ui_flex_distribute_space(UI_MAIN_AXIS_ALIGNMENT_START, free_space,
                                 item_count, flipped, spacing, leading_space,
                                 between_space);
      } else {
        *leading_space = free_space / item_count / 2;
        *between_space = free_space / item_count + spacing;
      }
    } break;

    case UI_MAIN_AXIS_ALIGNMENT_CENTER: {
      *leading_space = free_space / 2.0f;
      *between_space = spacing;
    } break;

    case UI_MAIN_AXIS_ALIGNMENT_SPACE_EVENLY: {
      *leading_space = free_space / (item_count + 1);
      *between_space = free_space / (item_count + 1) + spacing;
    } break;

    default:
      UNREACHABLE;
  }
}

static f32 ui_flex_get_child_cross_axis_offset(
    UICrossAxisAlignment cross_axis_alignment, f32 free_space, bool flipped) {
  switch (cross_axis_alignment) {
    case UI_CROSS_AXIS_ALIGNMENT_STRETCH:
    case UI_CROSS_AXIS_ALIGNMENT_BASELINE: {
      return 0.0f;
    } break;

    case UI_CROSS_AXIS_ALIGNMENT_START: {
      return flipped ? free_space : 0.0f;
    } break;

    case UI_CROSS_AXIS_ALIGNMENT_CENTER: {
      return free_space / 2.0f;
    } break;

    case UI_CROSS_AXIS_ALIGNMENT_END: {
      return ui_flex_get_child_cross_axis_offset(UI_CROSS_AXIS_ALIGNMENT_START,
                                                 free_space, !flipped);
    } break;

    default:
      UNREACHABLE;
  }
}

static f32 ui_flex_get_cross_size(Vec2 size, UIAxis direction) {
  if (direction == UI_AXIS_HORIZONTAL) {
    return size.y;
  } else {
    return size.x;
  }
}

static f32 ui_flex_get_main_size(Vec2 size, UIAxis direction) {
  if (direction == UI_AXIS_HORIZONTAL) {
    return size.x;
  } else {
    return size.y;
  }
}

static void ui_flex_layout(void *widget, UIBoxConstraints constraints) {
  UIFlex *flex = widget;

  UIFlexLayoutSize sizes = ui_flex_compute_size(flex, constraints);
  flex->widget.size = convert_size(
      v2(sizes.axis_size.main, sizes.axis_size.cross), flex->direction);
  // TODO: Handle overflow.

  f32 remaining_space = f32_max(0.0f, sizes.main_axis_free_space);
  // TODO: Handle text direction and vertical direction.
  f32 leading_space;
  f32 between_space;
  ui_flex_distribute_space(flex->main_axis_alignment, remaining_space,
                           flex->widget.child_count, /* flipped= */ false,
                           flex->spacing, &leading_space, &between_space);

  // Position all children in visual order: starting from the top-left child and
  // work towards the child that's farthest away from the origin.
  f32 child_main_position = leading_space;
  for (UIWidget *child = flex->widget.tree.first; child;
       child = child->tree.next) {
    f32 child_cross_position = ui_flex_get_child_cross_axis_offset(
        flex->cross_axis_alignment,
        ui_flex_get_cross_size(child->size, flex->direction),
        /* flipped= */ false);
    if (flex->direction == UI_AXIS_HORIZONTAL) {
      child->offset = v2(child_main_position, child_cross_position);
    } else {
      child->offset = v2(child_cross_position, child_main_position);
    }
    child_main_position +=
        ui_flex_get_main_size(child->size, flex->direction) + between_space;
  }
}

static UIWidgetVTable ui_flex_vtable = (UIWidgetVTable){
    .parent = &ui_widget_vtable,
    .name = "Flex",
    .layout = &ui_flex_layout,
};

static void *ui_flex_begin_(UIWidgetVTable *vtable, usize size,
                            UIFlexProps props) {
  UIFlex *flex = ui_widget_begin(vtable, size, props.key);
  flex->direction = props.direction;
  flex->main_axis_alignment = props.main_axis_alignment;
  flex->main_axis_size = props.main_axis_size;
  flex->cross_axis_alignment = props.cross_axis_alignment;
  flex->spacing = props.spacing;
  return flex;
}

void ui_flex_begin(UIFlexProps props) {
  ui_flex_begin_(&ui_flex_vtable, sizeof(UIFlex), props);
}

void ui_flex_end(void) { ui_widget_end(&ui_flex_vtable); }

static UIWidgetVTable ui_column_vtable = (UIWidgetVTable){
    .parent = &ui_flex_vtable,
    .name = "Column",
};

void ui_column_begin(UIColumnProps props) {
  ui_flex_begin_(&ui_column_vtable, sizeof(UIColumn),
                 (UIFlexProps){
                     .key = props.key,
                     .direction = UI_AXIS_VERTICAL,
                     .main_axis_alignment = props.main_axis_alignment,
                     .main_axis_size = props.main_axis_size,
                     .cross_axis_alignment = props.cross_axis_alignment,
                     .spacing = props.spacing,
                 });
}

void ui_column_end(void) { ui_widget_end(&ui_column_vtable); }
