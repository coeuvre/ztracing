#include "src/ui.h"

#include <string.h>

#include "src/assert.h"
#include "src/draw.h"
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
  UIWidgetMessageLayout message = {
      .type = UI_WIDGET_MESSAGE_LAYOUT,
      .constraints = constraints,
  };
  widget->klass->callback(widget, (UIWidgetMessage *)&message);
}

static void ui_widget_paint(UIWidget *widget, UIPaintingContext *context,
                            Vec2 offset) {
  UIWidgetMessagePaint message = {
      .type = UI_WIDGET_MESSAGE_PAINT,
      .context = context,
      .offset = offset,
  };
  widget->klass->callback(widget, (UIWidgetMessage *)&message);
}

static bool ui_widget_get_parent_data(UIWidget *widget, u32 parent_data_id,
                                      void *parent_data) {
  UIWidgetMessageGetParentData message = {
      .type = UI_WIDGET_MESSAGE_GET_PARENT_DATA,
      .parent_data_id = parent_data_id,
      .parent_data = parent_data,
  };
  return widget->klass->callback(widget, (UIWidgetMessage *)&message);
}

static void ui_widget_layout_default(UIWidget *widget,
                                     UIBoxConstraints constraints) {
  // The default layout just stacks children.
  Vec2 max_child_size = vec2_zero();
  for (UIWidget *child = widget->tree.first; child; child = child->tree.next) {
    ui_widget_layout(child, constraints);

    max_child_size = vec2_max(max_child_size, child->size);
  }

  widget->size = ui_box_constraints_constrain(constraints, max_child_size);
}

static void ui_widget_paint_child(UIWidget *child, UIPaintingContext *context,
                                  Vec2 offset) {
  ui_widget_paint(child, context, vec2_add(offset, child->offset));
}

static void ui_widget_paint_default(UIWidget *widget,
                                    UIPaintingContext *context, Vec2 offset) {
  for (UIWidget *child = widget->tree.first; child; child = child->tree.next) {
    ui_widget_paint_child(child, context, offset);
  }
}

static i32 ui_widget_callback_default(UIWidget *widget,
                                      UIWidgetMessage *message) {
  i32 result = 0;
  switch (message->type) {
    case UI_WIDGET_MESSAGE_LAYOUT: {
      UIWidgetMessageLayout *layout = (UIWidgetMessageLayout *)message;
      ui_widget_layout_default(widget, layout->constraints);
    } break;

    case UI_WIDGET_MESSAGE_PAINT: {
      UIWidgetMessagePaint *paint = (UIWidgetMessagePaint *)message;
      ui_widget_paint_default(widget, paint->context, paint->offset);
    } break;
  }
  return result;
}

void ui_end_frame(void) {
  UIState *state = ui_state_get();
  UIFrame *frame = state->current_frame;
  ASSERTF(!frame->current, "Mismatched begin/end calls, last begin: %s",
          frame->current->klass->name);

  // Layout and paint
  if (frame->root) {
    Vec2 viewport_size = vec2_sub(state->viewport_max, state->viewport_min);
    ui_widget_layout(frame->root, ui_box_constraints_make_tight(
                                      viewport_size.x, viewport_size.y));

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

static UIWidget *ui_widget_get(UIFrame *frame, UIKey key) {
  UIWidget *result = 0;
  UIWidgetHashMap *cache = &frame->cache;
  if (!ui_key_is_zero(key) && cache->slots) {
    UIWidgetHashSlot *slot = cache->slots + (key.hash % cache->slots_count);
    for (UIWidget *widget = slot->first; widget; widget = widget->hash.next) {
      if (ui_key_is_equal(ui_widget_get_key(widget), key)) {
        result = widget;
        break;
      }
    }
  }
  return result;
}

static UIWidget *ui_widget_push(UIFrame *frame, UIWidgetClass *klass,
                                UIKey key) {
  UIWidget *widget = arena_push(
      &frame->arena, sizeof(UIWidget) + klass->props_size, ARENA_PUSH_NO_ZERO);
  memory_zero(widget, sizeof(UIWidget));
  widget->klass = klass;

  UIWidgetHashSlot *slot =
      frame->cache.slots + (key.hash % frame->cache.slots_count);
  DLL_APPEND(slot->first, slot->last, widget, hash.prev, hash.next);
  ++frame->cache.total_count;

  return widget;
}

void ui_widget_begin_(UIWidgetClass *klass, usize props_size, void *props) {
  UIState *state = ui_state_get();
  UIFrame *frame = state->current_frame;
  UIFrame *last_frame = state->last_frame;
  UIWidget *parent = frame->current;

  ASSERTF(klass->props_size >= sizeof(UIKey),
          "The first field of props must be a UIKey");
  ASSERTF(klass->props_size == props_size,
          "props_size (%d) does not equal to klass.props_size (%d)",
          (int)props_size, (int)klass->props_size);
  if (!klass->callback) {
    klass->callback = ui_widget_callback_default;
  }
  UIKey key = *(UIKey *)props;
  if (ui_key_is_zero(key)) {
    UIKey seed = key;
    if (parent) {
      seed = ui_widget_get_key(parent);
    }
    u32 seq = 0;
    if (parent) {
      seq = parent->child_count;
    }
    key = ui_key_make_local(seed, seq, klass->name, str8_zero());
  }

  UIWidget *widget = ui_widget_push(frame, klass, key);
  if (parent) {
    DLL_APPEND(parent->tree.first, parent->tree.last, widget, tree.prev,
               tree.next);
    ++parent->child_count;
    widget->tree.parent = parent;
  } else {
    frame->root = widget;
  }
  void *widget_props = widget + 1;
  memory_copy(widget_props, props, props_size);
  *(UIKey *)widget_props = key;

  UIWidget *last_widget = ui_widget_get(last_frame, key);
  // Copy state from last frame
  if (last_widget) {
    widget->state = last_widget->state;
  }

  frame->current = widget;
}

void ui_widget_end(UIWidgetClass *klass) {
  UIFrame *frame = ui_frame_get();
  UIWidget *widget = frame->current;
  ASSERT(widget);
  ASSERTF(widget->klass == klass,
          "Mismatched begin/end calls. Begin with %s, end with %s",
          widget->klass->name, klass->name);

  frame->current = widget->tree.parent;
}

////////////////////////////////////////////////////////////////////////////////
///
/// UILimitedBox
///
static UIBoxConstraints ui_limited_box_limit_constraints(
    UILimitedBoxProps *limited_box, UIBoxConstraints constraints) {
  return (UIBoxConstraints){
      .min_width = constraints.min_width,
      .max_width = ui_box_constraints_has_bounded_width(constraints)
                       ? constraints.max_width
                       : ui_box_constraints_constrain_width(
                             constraints, limited_box->max_width),
      .min_height = constraints.min_height,
      .max_height = ui_box_constraints_has_bounded_height(constraints)
                        ? constraints.max_height
                        : ui_box_constraints_constrain_height(
                              constraints, limited_box->max_height),
  };
}

static void ui_limited_box_layout(UIWidget *widget,
                                  UILimitedBoxProps *limited_box,
                                  UIBoxConstraints constraints) {
  UIBoxConstraints limited_constraints =
      ui_limited_box_limit_constraints(limited_box, constraints);

  if (widget->tree.first) {
    Vec2 max_child_size = vec2_zero();
    for (UIWidget *child = widget->tree.first; child;
         child = child->tree.next) {
      ui_widget_layout(child, limited_constraints);

      Vec2 size = ui_box_constraints_constrain(constraints, child->size);
      max_child_size = vec2_max(max_child_size, size);
    }
    widget->size = ui_box_constraints_constrain(constraints, max_child_size);
  } else {
    widget->size =
        ui_box_constraints_constrain(limited_constraints, vec2_zero());
  }
}

static i32 ui_limited_box_callback(UIWidget *widget, UIWidgetMessage *message) {
  i32 result = 0;
  switch (message->type) {
    case UI_WIDGET_MESSAGE_LAYOUT: {
      UIWidgetMessageLayout *layout = (UIWidgetMessageLayout *)message;
      ui_limited_box_layout(widget,
                            ui_widget_get_props(widget, UILimitedBoxProps),
                            layout->constraints);
    } break;
    default: {
      result = ui_widget_callback_default(widget, message);
    } break;
  }
  return result;
}

static UIWidgetClass ui_limited_box_class = {
    .name = "LimitedBox",
    .props_size = sizeof(UILimitedBoxProps),
    .callback = &ui_limited_box_callback,
};

void ui_limited_box_begin(UILimitedBoxProps *props) {
  ui_widget_begin(&ui_limited_box_class, props);
}

void ui_limited_box_end(void) { ui_widget_end(&ui_limited_box_class); }

////////////////////////////////////////////////////////////////////////////////
///
/// UIColoredBox
///
static void ui_colored_box_paint(UIWidget *widget,
                                 UIColoredBoxProps *colored_box,
                                 UIPaintingContext *context, Vec2 offset) {
  Vec2 size = widget->size;
  if (size.x > 0 && size.y > 0) {
    fill_rect(offset, vec2_add(offset, size),
              (ColorU32){
                  (u8)(colored_box->color.a * 255.0f),
                  (u8)(colored_box->color.r * 255.0f),
                  (u8)(colored_box->color.g * 255.0f),
                  (u8)(colored_box->color.b * 255.0f),
              });
  }

  for (UIWidget *child = widget->tree.first; child; child = child->tree.next) {
    ui_widget_paint_child(child, context, offset);
  }
}

static i32 ui_colored_box_callback(UIWidget *widget, UIWidgetMessage *message) {
  i32 result = 0;
  switch (message->type) {
    case UI_WIDGET_MESSAGE_PAINT: {
      UIWidgetMessagePaint *paint = (UIWidgetMessagePaint *)message;
      ui_colored_box_paint(widget,
                           ui_widget_get_props(widget, UIColoredBoxProps),
                           paint->context, paint->offset);
    } break;
    default: {
      result = ui_widget_callback_default(widget, message);
    } break;
  }
  return result;
}

static UIWidgetClass ui_colored_box_class = {
    .name = "ColoredBox",
    .props_size = sizeof(UIColoredBoxProps),
    .callback = &ui_colored_box_callback,
};

void ui_colored_box_begin(UIColoredBoxProps *props) {
  ui_widget_begin(&ui_colored_box_class, props);
}

void ui_colored_box_end(void) { ui_widget_end(&ui_colored_box_class); }

////////////////////////////////////////////////////////////////////////////////
///
/// UIConstrainedBox
///
static void ui_constrained_box_layout(UIWidget *widget,
                                      UIConstrainedBoxProps *constrained_box,
                                      UIBoxConstraints constraints) {
  UIBoxConstraints enforced_constraints = ui_box_constraints_enforce(
      constrained_box->additional_constraints, constraints);

  Vec2 max_child_size = vec2_zero();
  for (UIWidget *child = widget->tree.first; child; child = child->tree.next) {
    ui_widget_layout(child, enforced_constraints);
    max_child_size = vec2_max(max_child_size, child->size);
  }

  widget->size =
      ui_box_constraints_constrain(enforced_constraints, max_child_size);
}

static i32 ui_constrained_box_callback(UIWidget *widget,
                                       UIWidgetMessage *message) {
  i32 result = 0;
  switch (message->type) {
    case UI_WIDGET_MESSAGE_LAYOUT: {
      UIWidgetMessageLayout *layout = (UIWidgetMessageLayout *)message;
      ui_constrained_box_layout(
          widget, ui_widget_get_props(widget, UIConstrainedBoxProps),
          layout->constraints);
    } break;
    default: {
      result = ui_widget_callback_default(widget, message);
    } break;
  }
  return result;
}

static UIWidgetClass ui_constrained_box_class = {
    .name = "ConstrainedBox",
    .props_size = sizeof(UIConstrainedBoxProps),
    .callback = &ui_constrained_box_callback,
};

void ui_constrained_box_begin(UIConstrainedBoxProps *props) {
  ui_widget_begin(&ui_constrained_box_class, props);
}

void ui_constrained_box_end(void) { ui_widget_end(&ui_constrained_box_class); }

////////////////////////////////////////////////////////////////////////////////
///
/// UIAlign
///
static void ui_align_layout(UIWidget *widget, UIAlignProps *align,
                            UIBoxConstraints constraints) {
  UISizeFactor factor = align->factor;
  if (factor.width_present) {
    ASSERTF(align->factor.width >= 0, "factor.widget must be positive, got %f",
            align->factor.width);
  }
  if (factor.height_present) {
    ASSERTF(factor.height >= 0, "height_factor must be positive, got %f",
            factor.height);
  }
  bool should_shrink_wrap_width =
      factor.width_present || f32_is_infinity(constraints.max_width);
  bool should_shrink_wrap_height =
      factor.height_present || f32_is_infinity(constraints.max_height);
  UIBoxConstraints child_constraints = ui_box_constraints_loosen(constraints);

  if (widget->tree.first) {
    Vec2 max_child_size = vec2_zero();

    for (UIWidget *child = widget->tree.first; child;
         child = child->tree.next) {
      ui_widget_layout(child, child_constraints);

      Vec2 wrap_size = v2(
          should_shrink_wrap_width
              ? (child->size.x * (factor.width_present ? factor.width : 1.0f))
              : F32_INFINITY,
          should_shrink_wrap_height
              ? (child->size.y * (factor.height_present ? factor.height : 1.0f))
              : F32_INFINITY);

      max_child_size = vec2_max(max_child_size, wrap_size);
    }

    widget->size = ui_box_constraints_constrain(constraints, max_child_size);

    // TODO: UITextDirection
    UIAlignment alignment = align->alignment;
    for (UIWidget *child = widget->tree.first; child;
         child = child->tree.next) {
      child->offset = ui_alignment_align_offset(
          alignment, vec2_sub(widget->size, child->size));
    }
  } else {
    Vec2 size = v2(should_shrink_wrap_width ? 0 : F32_INFINITY,
                   should_shrink_wrap_height ? 0 : F32_INFINITY);
    widget->size = ui_box_constraints_constrain(constraints, size);
  }
}

static i32 ui_align_callback(UIWidget *widget, UIWidgetMessage *message) {
  i32 result = 0;
  switch (message->type) {
    case UI_WIDGET_MESSAGE_LAYOUT: {
      UIWidgetMessageLayout *layout = (UIWidgetMessageLayout *)message;
      ui_align_layout(widget, ui_widget_get_props(widget, UIAlignProps),
                      layout->constraints);
    } break;
    default: {
      result = ui_widget_callback_default(widget, message);
    } break;
  }
  return result;
}

static UIWidgetClass ui_align_class = {
    .name = "Align",
    .props_size = sizeof(UIAlignProps),
    .callback = &ui_align_callback,
};

void ui_align_begin(UIAlignProps *props) {
  ui_widget_begin(&ui_align_class, props);
}

void ui_align_end(void) { ui_widget_end(&ui_align_class); }

////////////////////////////////////////////////////////////////////////////////
///
/// UICenter
///
static i32 ui_center_callback(UIWidget *widget, UIWidgetMessage *message) {
  i32 result = 0;
  switch (message->type) {
    case UI_WIDGET_MESSAGE_LAYOUT: {
      UIWidgetMessageLayout *layout = (UIWidgetMessageLayout *)message;
      UICenterProps *center = ui_widget_get_props(widget, UICenterProps);
      UIAlignProps align = {
          .key = center->key,
          .factor = center->factor,
      };
      ui_align_layout(widget, &align, layout->constraints);
    } break;
    default: {
      result = ui_widget_callback_default(widget, message);
    } break;
  }
  return result;
}

static UIWidgetClass ui_center_class = {
    .name = "Center",
    .props_size = sizeof(UICenterProps),
    .callback = &ui_center_callback,
};

void ui_center_begin(UICenterProps *props) {
  ui_widget_begin(&ui_center_class, props);
}

void ui_center_end(void) { ui_widget_end(&ui_center_class); }

////////////////////////////////////////////////////////////////////////////////
///
/// UIPadding
///

static void ui_padding_layout(UIWidget *widget, UIPaddingProps *padding,
                              UIBoxConstraints constraints) {
  // TODO: UITextDirection
  UIEdgeInsets resolved_padding = {
      .left = padding->padding.start,
      .right = padding->padding.end,
      .top = padding->padding.top,
      .bottom = padding->padding.bottom,
  };
  f32 horizontal = ui_edge_insets_get_horizontal(resolved_padding);
  f32 vertical = ui_edge_insets_get_vertical(resolved_padding);
  if (widget->tree.first) {
    UIBoxConstraints inner_constraints =
        ui_box_constraints_deflate(constraints, resolved_padding);
    Vec2 max_child_size = vec2_zero();

    for (UIWidget *child = widget->tree.first; child;
         child = child->tree.next) {
      ui_widget_layout(child, inner_constraints);
      child->offset = v2(resolved_padding.left, resolved_padding.top);

      max_child_size = vec2_max(max_child_size, child->size);
    }

    widget->size = ui_box_constraints_constrain(
        constraints,
        v2(horizontal + max_child_size.x, vertical + max_child_size.y));
  } else {
    widget->size =
        ui_box_constraints_constrain(constraints, v2(horizontal, vertical));
  }
}

static i32 ui_padding_callback(UIWidget *widget, UIWidgetMessage *message) {
  i32 result = 0;
  switch (message->type) {
    case UI_WIDGET_MESSAGE_LAYOUT: {
      UIWidgetMessageLayout *layout = (UIWidgetMessageLayout *)message;
      ui_padding_layout(widget, ui_widget_get_props(widget, UIPaddingProps),
                        layout->constraints);
    } break;
    default: {
      result = ui_widget_callback_default(widget, message);
    } break;
  }
  return result;
}

static UIWidgetClass ui_padding_class = {
    .name = "Padding",
    .props_size = sizeof(UIPaddingProps),
    .callback = &ui_padding_callback,
};

void ui_padding_begin(UIPaddingProps *props) {
  ui_widget_begin(&ui_padding_class, props);
}

void ui_padding_end(void) { ui_widget_end(&ui_padding_class); }

////////////////////////////////////////////////////////////////////////////////
///
/// UIFlexible
///
static i32 ui_flexible_callback(UIWidget *widget, UIWidgetMessage *message) {
  i32 result = 0;
  switch (message->type) {
    case UI_WIDGET_MESSAGE_GET_PARENT_DATA: {
      UIWidgetMessageGetParentData *get_parent_data =
          (UIWidgetMessageGetParentData *)message;
      if (get_parent_data->parent_data_id == UI_WIDGET_PARENT_DATA_FLEX) {
        UIFlexibleProps *flexible =
            ui_widget_get_props(widget, UIFlexibleProps);
        UIWidgetParentDataFlex *data = get_parent_data->parent_data;
        *data = (UIWidgetParentDataFlex){
            .flex = flexible->flex,
            .fit = flexible->fit,
        };
        result = 1;
      }
    } break;
    default: {
      result = ui_widget_callback_default(widget, message);
    } break;
  }
  return result;
}

UIWidgetClass ui_flexible_vtable = {
    .name = "Flexible",
    .props_size = sizeof(UIFlexibleProps),
    .callback = &ui_flexible_callback,
};

void ui_flexible_begin(UIFlexibleProps *props) {
  ui_widget_begin(&ui_flexible_vtable, props);
}

void ui_flexible_end(void) { ui_widget_end(&ui_flexible_vtable); }

////////////////////////////////////////////////////////////////////////////////
///
/// UIFlex
///
static inline UIBoxConstraints ui_box_constraints_make_for_non_flex_child(
    UIFlexProps *flex, UIBoxConstraints constraints) {
  bool should_fill_cross_axis = false;
  if (flex->cross_axis_alignment == UI_CROSS_AXIS_ALIGNMENT_STRETCH) {
    should_fill_cross_axis = true;
  }

  UIBoxConstraints result;
  switch (flex->direction) {
    case UI_AXIS_HORIZONTAL: {
      if (should_fill_cross_axis) {
        result = ui_box_constraints_make_tight_height(constraints.max_height);
      } else {
        result = (UIBoxConstraints){
            .min_width = 0,
            .max_width = F32_INFINITY,
            .min_height = 0,
            .max_height = constraints.max_height,
        };
      }
    } break;
    case UI_AXIS_VERTICAL: {
      if (should_fill_cross_axis) {
        result = ui_box_constraints_make_tight_width(constraints.max_width);
      } else {
        result = (UIBoxConstraints){
            .min_width = 0,
            .max_width = constraints.max_width,
            .min_height = 0,
            .max_height = F32_INFINITY,
        };
      }
    } break;
    default:
      UNREACHABLE;
  }
  return result;
}

static inline UIBoxConstraints ui_box_constraints_make_for_flex_child(
    UIFlexProps *flex, UIBoxConstraints constraints, f32 max_child_extent,
    UIWidgetParentDataFlex data) {
  DEBUG_ASSERT(data.flex > 0);
  DEBUG_ASSERT(max_child_extent >= 0.0f);
  f32 min_child_extent = 0.0;
  if (data.fit == UI_FLEX_FIT_TIGHT) {
    min_child_extent = max_child_extent;
  }
  bool should_fill_cross_axis = false;
  if (flex->cross_axis_alignment == UI_CROSS_AXIS_ALIGNMENT_STRETCH) {
    should_fill_cross_axis = true;
  }
  UIBoxConstraints result;
  if (flex->direction == UI_AXIS_HORIZONTAL) {
    result = (UIBoxConstraints){
        .min_width = min_child_extent,
        .max_width = max_child_extent,
        .min_height = should_fill_cross_axis ? constraints.max_height : 0,
        .max_height = constraints.max_height,
    };
  } else {
    result = (UIBoxConstraints){
        .min_width = should_fill_cross_axis ? constraints.max_width : 0,
        .max_width = constraints.max_width,
        .min_height = min_child_extent,
        .max_height = max_child_extent,
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

static UIFlexLayoutSize ui_flex_compute_size(UIWidget *widget,
                                             UIFlexProps *flex,
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
      axis_size_make(flex->spacing * (widget->child_count - 1), 0.0f);
  for (UIWidget *child = widget->tree.first; child; child = child->tree.next) {
    i32 child_flex = 0;
    if (can_flex) {
      UIWidgetParentDataFlex data;
      if (ui_widget_get_parent_data(child, UI_WIDGET_PARENT_DATA_FLEX, &data)) {
        child_flex = data.flex;
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
  for (UIWidget *child = widget->tree.first; child && total_flex > 0;
       child = child->tree.next) {
    UIWidgetParentDataFlex data;
    bool has_parent_data =
        ui_widget_get_parent_data(child, UI_WIDGET_PARENT_DATA_FLEX, &data);
    if (!has_parent_data || data.flex <= 0) {
      continue;
    }
    total_flex -= data.flex;
    DEBUG_ASSERT(f32_is_finite(space_per_flex));
    f32 max_child_extent = space_per_flex * data.flex;
    DEBUG_ASSERT(data.fit == UI_FLEX_FIT_LOOSE ||
                 max_child_extent < F32_INFINITY);
    UIBoxConstraints child_constraints = ui_box_constraints_make_for_flex_child(
        flex, constraints, max_child_extent, data);
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

static void ui_flex_layout(UIWidget *widget, UIFlexProps *flex,
                           UIBoxConstraints constraints) {
  UIFlexLayoutSize sizes = ui_flex_compute_size(widget, flex, constraints);
  f32 cross_axis_extent = sizes.axis_size.cross;
  widget->size = convert_size(v2(sizes.axis_size.main, sizes.axis_size.cross),
                              flex->direction);
  // TODO: Handle overflow.

  f32 remaining_space = f32_max(0.0f, sizes.main_axis_free_space);
  // TODO: Handle text direction and vertical direction.
  f32 leading_space;
  f32 between_space;
  ui_flex_distribute_space(flex->main_axis_alignment, remaining_space,
                           widget->child_count, /* flipped= */ false,
                           flex->spacing, &leading_space, &between_space);

  // Position all children in visual order: starting from the top-left child and
  // work towards the child that's farthest away from the origin.
  f32 child_main_position = leading_space;
  for (UIWidget *child = widget->tree.first; child; child = child->tree.next) {
    f32 child_cross_position = ui_flex_get_child_cross_axis_offset(
        flex->cross_axis_alignment,
        cross_axis_extent -
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

static i32 ui_flex_callback(UIWidget *widget, UIWidgetMessage *message) {
  i32 result = 0;
  switch (message->type) {
    case UI_WIDGET_MESSAGE_LAYOUT: {
      UIWidgetMessageLayout *layout = (UIWidgetMessageLayout *)message;
      ui_flex_layout(widget, ui_widget_get_props(widget, UIFlexProps),
                     layout->constraints);
    } break;
    default: {
      result = ui_widget_callback_default(widget, message);
    } break;
  }
  return result;
}

static UIWidgetClass ui_flex_class = {
    .name = "Flex",
    .props_size = sizeof(UIFlexProps),
    .callback = &ui_flex_callback,
};

void ui_flex_begin(UIFlexProps *props) {
  ui_widget_begin(&ui_flex_class, props);
}

void ui_flex_end(void) { ui_widget_end(&ui_flex_class); }

////////////////////////////////////////////////////////////////////////////////
///
/// UIColumn
///
static i32 ui_column_callback(UIWidget *widget, UIWidgetMessage *message) {
  i32 result = 0;
  switch (message->type) {
    case UI_WIDGET_MESSAGE_LAYOUT: {
      UIWidgetMessageLayout *layout = (UIWidgetMessageLayout *)message;
      UIColumnProps *column = ui_widget_get_props(widget, UIColumnProps);
      UIFlexProps flex = {
          .key = column->key,
          .direction = UI_AXIS_VERTICAL,
          .main_axis_alignment = column->main_axis_alignment,
          .main_axis_size = column->main_axis_size,
          .cross_axis_alignment = column->cross_axis_alignment,
          .spacing = column->spacing,
      };
      ui_flex_layout(widget, &flex, layout->constraints);
    } break;
    default: {
      result = ui_widget_callback_default(widget, message);
    } break;
  }
  return result;
}

static UIWidgetClass ui_column_class = {
    .name = "Column",
    .props_size = sizeof(UIColumnProps),
    .callback = &ui_column_callback,
};

void ui_column_begin(UIColumnProps *props) {
  ui_widget_begin(&ui_column_class, props);
}

void ui_column_end(void) { ui_widget_end(&ui_column_class); }

////////////////////////////////////////////////////////////////////////////////
///
/// UIRow
///
static i32 ui_row_callback(UIWidget *widget, UIWidgetMessage *message) {
  i32 result = 0;
  switch (message->type) {
    case UI_WIDGET_MESSAGE_LAYOUT: {
      UIWidgetMessageLayout *layout = (UIWidgetMessageLayout *)message;
      UIRowProps *row = ui_widget_get_props(widget, UIRowProps);
      UIFlexProps flex = {
          .key = row->key,
          .direction = UI_AXIS_HORIZONTAL,
          .main_axis_alignment = row->main_axis_alignment,
          .main_axis_size = row->main_axis_size,
          .cross_axis_alignment = row->cross_axis_alignment,
          .spacing = row->spacing,
      };
      ui_flex_layout(widget, &flex, layout->constraints);
    } break;
    default: {
      result = ui_widget_callback_default(widget, message);
    } break;
  }
  return result;
}

static UIWidgetClass ui_row_vtable = {
    .name = "Row",
    .props_size = sizeof(UIRowProps),
    .callback = &ui_row_callback,
};

void ui_row_begin(UIRowProps *props) { ui_widget_begin(&ui_row_vtable, props); }

void ui_row_end(void) { ui_widget_end(&ui_row_vtable); }
