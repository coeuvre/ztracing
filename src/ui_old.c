#include "src/ui_old.h"

#include <stdarg.h>
#include <string.h>

#include "src/assert.h"
#include "src/draw.h"
#include "src/list.h"
#include "src/math.h"
#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"

THREAD_LOCAL UIState t_ui_state;

UIBox *ui_box_get(UIFrame *frame, UIID id) {
  UIBox *result = 0;
  UIBoxCache *cache = &frame->cache;
  if (!uuid_is_zero(id) && cache->box_hash_slots) {
    UIBoxHashSlot *slot =
        &cache->box_hash_slots[id.hash % cache->box_hash_slots_count];
    for (UIBox *box = slot->first; box; box = box->hash_next) {
      if (uuid_is_equal(box->id, id)) {
        result = box;
        break;
      }
    }
  }
  return result;
}

static UIBox *ui_box_push(UIBoxCache *cache, Arena *arena, UIID id) {
  ASSERT(!uuid_is_zero(id));
  UIBox *result = arena_push_array(arena, UIBox, 1);
  result->id = id;

  UIBoxHashSlot *slot =
      &cache->box_hash_slots[id.hash % cache->box_hash_slots_count];
  DLL_APPEND(slot->first, slot->last, result, hash_prev, hash_next);
  ++cache->total_box_count;

  return result;
}

static void ui_push_build_errorf(UIFrame *frame, const char *fmt, ...) {
  UIBuildError *error = arena_push_array(&frame->arena, UIBuildError, 1);

  va_list ap;
  va_start(ap, fmt);
  error->message = arena_push_str8fv(&frame->arena, fmt, ap);
  va_end(ap);

  DLL_APPEND(frame->first_error, frame->last_error, error, prev, next);
}

static inline b32 is_mouse_button_pressed(UIState *state,
                                          UIMouseButton button) {
  UIMouseButtonState *mouse_button_state = &state->input.mouse.buttons[button];
  b32 result =
      mouse_button_state->is_down && mouse_button_state->transition_count > 0;
  return result;
}

static inline b32 is_mouse_button_clicked(UIState *state,
                                          UIMouseButton button) {
  UIMouseButtonState *mouse_button_state = &state->input.mouse.buttons[button];
  b32 result =
      !mouse_button_state->is_down && mouse_button_state->transition_count > 0;
  return result;
}

void ui_init(void) {
  UIState *state = &t_ui_state;
  ASSERTF(!state->init, "ui_init called more than once");

  state->init = 1;
  state->input.dt = 1.0f / 60.0f;  // Assume 60 FPS by default.
  state->input.mouse.pos = vec2(-1, -1);
}

void ui_quit(void) {
  UIState *state = ui_state_get();
  arena_free(&state->frames[0].arena);
  arena_free(&state->frames[1].arena);
  *state = (UIState){0};
}

void ui_on_mouse_pos(Vec2 pos) {
  UIState *state = ui_state_get();

  state->input.mouse.pos = pos;
}

void ui_on_mouse_button_up(Vec2 pos, UIMouseButton button) {
  UIState *state = ui_state_get();

  state->input.mouse.pos = pos;

  UIMouseButtonState *mouse_button_state = &state->input.mouse.buttons[button];
  if (mouse_button_state->is_down) {
    mouse_button_state->is_down = 0;
    mouse_button_state->transition_count += 1;
  }
}

void ui_on_mouse_button_down(Vec2 pos, UIMouseButton button) {
  UIState *state = ui_state_get();

  state->input.mouse.pos = pos;

  UIMouseButtonState *mouse_button_state = &state->input.mouse.buttons[button];
  if (!mouse_button_state->is_down) {
    mouse_button_state->is_down = 1;
    mouse_button_state->transition_count += 1;
  }
}

void ui_on_mouse_wheel(Vec2 delta) {
  UIState *state = ui_state_get();

  state->input.mouse.wheel = delta;
}

void ui_set_delta_time(f32 dt) {
  UIState *state = ui_state_get();

  state->input.dt = dt;
  state->fast_rate = 1.0f - f32_exp(-50.f * dt);
}

void ui_begin_frame(Vec2 viewport_size) {
  UIState *state = ui_state_get();
  state->frame_index += 1;
  state->current_frame =
      state->frames + (state->frame_index % ARRAY_COUNT(state->frames));
  state->last_frame =
      state->frames + ((state->frame_index - 1) % ARRAY_COUNT(state->frames));

  UIFrame *frame = state->current_frame;

  arena_clear(&frame->arena);

  frame->cache = (UIBoxCache){0};
  frame->cache.box_hash_slots_count = 4096;
  frame->cache.box_hash_slots = arena_push_array(
      &frame->arena, UIBoxHashSlot, frame->cache.box_hash_slots_count);

  frame->frame_index = state->frame_index;
  frame->viewport_size = viewport_size;
  frame->first_error = frame->last_error = 0;
  frame->current_stack = frame->current_build = 0;
  ui_tag_begin("Root", (UIProps){0});
  frame->root = frame->current_build;
}

static inline f32 ui_edge_insets_get_size(UIEdgeInsets edge_insets,
                                          Axis2 axis) {
  // TODO: Handle text direction.
  f32 result;
  if (axis == kAxis2X) {
    result = edge_insets.left + edge_insets.right;
  } else {
    result = edge_insets.top + edge_insets.bottom;
  }
  return result;
}

static inline f32 ui_edge_insets_get_start(UIEdgeInsets edge_insets,
                                           Axis2 axis) {
  // TODO: Handle text direction.
  f32 result;
  if (axis == kAxis2X) {
    result = edge_insets.left;
  } else {
    result = edge_insets.top;
  }
  return result;
}

static inline f32 ui_edge_insets_get_end(UIEdgeInsets edge_insets, Axis2 axis) {
  // TODO: Handle text direction.
  f32 result;
  if (axis == kAxis2X) {
    result = edge_insets.right;
  } else {
    result = edge_insets.bottom;
  }
  return result;
}

static inline UIBorderSide ui_border_get_start(UIBorder border, Axis2 axis) {
  UIBorderSide result;
  if (axis == kAxis2X) {
    result = border.left;
  } else {
    result = border.top;
  }
  return result;
}

static inline UIBorderSide ui_border_get_end(UIBorder border, Axis2 axis) {
  UIBorderSide result;
  if (axis == kAxis2X) {
    result = border.right;
  } else {
    result = border.bottom;
  }
  return result;
}

static void align_main_axis(UIBox *box, Axis2 axis, UIMainAxisAlignment align,
                            f32 children_size) {
  f32 border_start = ui_border_get_start(box->props.border, axis).width;
  f32 border_end = ui_border_get_end(box->props.border, axis).width;
  f32 padding_start = ui_edge_insets_get_start(box->props.padding, axis);
  f32 padding_end = ui_edge_insets_get_end(box->props.padding, axis);
  f32 size_axis = vec2_get(box->computed.size, axis);
  f32 free = size_axis - children_size - border_start - border_end -
             padding_start - padding_end;
  f32 pos = border_start + padding_start;
  switch (align) {
    case UI_MAIN_AXIS_ALIGNMENT_START: {
    } break;

    case UI_MAIN_AXIS_ALIGNMENT_CENTER: {
      pos += free / 2.0;
    } break;

    case UI_MAIN_AXIS_ALIGNMENT_END: {
      pos += free;
    } break;

    default: {
      UNREACHABLE;
    } break;
  }

  for (UIBox *child = box->build.first; child; child = child->build.next) {
    pos += ui_edge_insets_get_start(child->props.margin, axis);
    vec2_set(&child->computed.rel_pos, axis, pos);
    pos += vec2_get(child->computed.size, axis) +
           ui_edge_insets_get_end(child->props.margin, axis);
  }
}

static void align_cross_axis(UIBox *box, Axis2 axis,
                             UICrossAxisAlignment align) {
  f32 border_start = ui_border_get_start(box->props.border, axis).width;
  f32 border_end = ui_border_get_end(box->props.border, axis).width;
  f32 padding_start = ui_edge_insets_get_start(box->props.padding, axis);
  f32 padding_end = ui_edge_insets_get_end(box->props.padding, axis);

  f32 self_size = vec2_get(box->computed.size, axis);
  for (UIBox *child = box->build.first; child; child = child->build.next) {
    f32 child_size = vec2_get(child->computed.size, axis);
    f32 free = self_size - child_size - border_start - border_end -
               padding_start - padding_end -
               ui_edge_insets_get_size(child->props.margin, axis);
    f32 margin_start = ui_edge_insets_get_start(child->props.margin, axis);
    f32 pos = 0;
    switch (align) {
      case UI_CROSS_AXIS_ALIGNMENT_START:
      case UI_CROSS_AXIS_ALIGNMENT_STRETCH: {
        pos = border_start + padding_start + margin_start;
      } break;

      case UI_CROSS_AXIS_ALIGNMENT_CENTER: {
        pos = border_start + padding_start + margin_start + free / 2.0f;
      } break;

      case UI_CROSS_AXIS_ALIGNMENT_END: {
        pos = border_start + padding_start + margin_start + free;
      } break;

      default: {
        UNREACHABLE;
      } break;
    }

    vec2_set(&child->computed.rel_pos, axis, pos);
  }
}

static inline b32 should_max_axis(UIBox *box, int axis, Axis2 main_axis,
                                  f32 max_size_axis) {
  // cross axis is always as small as possible
  b32 result = box->props.main_axis_size == UI_MAIN_AXIS_SIZE_MAX &&
               axis == (int)main_axis && max_size_axis != F32_INFINITY;
  return result;
}

static f32 get_first_non_zero_font_size(UIBox *box) {
  f32 font_size = box->props.font_size;
  if (font_size <= 0 && box->build.parent) {
    font_size = get_first_non_zero_font_size(box->build.parent);
  }
  return font_size;
}

static ColorU32 get_first_non_zero_color(UIBox *box) {
  ColorU32 color = box->props.color;
  if (color.a == 0 && box->build.parent) {
    color = get_first_non_zero_color(box->build.parent);
  }
  return color;
}

static Vec2 layout_text(UIBox *box, Vec2 max_size, Axis2 main_axis,
                        Axis2 cross_axis) {
  ASSERT(!str8_is_empty(box->props.text));

  // TODO: constraint text size within [(0, 0), max_size]

  f32 font_size = get_first_non_zero_font_size(box);
  if (font_size <= 0) {
    font_size = kUIFontSizeDefault;
  }
  box->computed.font_size = font_size;
  TextMetrics metrics = get_text_metrics_str8(box->props.text, font_size);
  Vec2 text_size = metrics.size;
  text_size = vec2_min(text_size, max_size);

  Vec2 children_size;
  vec2_set(&children_size, main_axis, vec2_get(text_size, main_axis));
  vec2_set(&children_size, cross_axis, vec2_get(text_size, cross_axis));
  return children_size;
}

static void layout_box(UIFrame *frame, UIBox *box, Vec2 min_size,
                       Vec2 max_size);

static Vec2 layout_child(UIFrame *frame, UIBox *child, Vec2 min_size,
                         Vec2 max_size, Axis2 main_axis) {
  // Leave space for margin
  f32 margin_x = child->props.margin.left + child->props.margin.right;
  f32 margin_y = child->props.margin.top + child->props.margin.bottom;
  max_size.x = f32_max(max_size.x - margin_x, 0);
  max_size.y = f32_max(max_size.y - margin_y, 0);

  layout_box(frame, child, min_size, max_size);

  // Add margin back
  Vec2 child_size;
  child_size.x = f32_min(child->computed.size.x + margin_x, max_size.x);
  child_size.y = f32_min(child->computed.size.y + margin_y, max_size.y);

  if (vec2_get(child_size, main_axis) == kUISizeInfinity &&
      vec2_get(max_size, main_axis) == kUISizeInfinity) {
    ui_push_build_errorf(
        frame, "Cannot have unbounded content within unbounded constraint");
  }

  return child_size;
}

static inline bool should_layout(UIPosition pos) {
  bool result = pos == kUIPositionRelative;
  return result;
}

static Vec2 should_children_flex(UIFrame *frame, UIBox *box, Vec2 max_size,
                                 Axis2 main_axis, Axis2 cross_axis) {
  f32 max_main_axis_size = vec2_get(max_size, main_axis);
  f32 max_cross_axis_size = vec2_get(max_size, cross_axis);

  f32 child_main_axis_size = 0.0f;
  f32 child_cross_axis_size = 0.0f;

  f32 total_flex = 0;
  UIBox *last_flex = 0;

  // First pass: layout non-flex children
  for (UIBox *child = box->build.first; child; child = child->build.next) {
    if (!should_layout(child->props.position)) {
      continue;
    }

    total_flex += child->props.flex;
    if (!child->props.flex) {
      Vec2 this_child_max_size;
      vec2_set(&this_child_max_size, main_axis,
               max_main_axis_size - child_main_axis_size);
      vec2_set(&this_child_max_size, cross_axis, max_cross_axis_size);
      Vec2 this_child_min_size = {0};
      if (box->props.cross_axis_align == UI_CROSS_AXIS_ALIGNMENT_STRETCH) {
        vec2_set(&this_child_min_size, cross_axis,
                 vec2_get(this_child_max_size, cross_axis));
      }

      Vec2 this_child_size = layout_child(frame, child, this_child_min_size,
                                          this_child_max_size, main_axis);

      child_main_axis_size += vec2_get(this_child_size, main_axis);
      child_cross_axis_size =
          f32_max(child_cross_axis_size, vec2_get(this_child_size, cross_axis));
    } else {
      last_flex = child;
    }
  }

  // Second pass: layout flex children
  f32 child_main_axis_flex = max_main_axis_size - child_main_axis_size;
  for (UIBox *child = box->build.first; child; child = child->build.next) {
    if (!should_layout(child->props.position)) {
      continue;
    }

    if (child->props.flex) {
      if (max_main_axis_size == kUISizeInfinity) {
        ui_push_build_errorf(frame,
                             "Unbounded constraint doesn't work with flex");
      }

      f32 this_child_max_main_axis_size;
      if (child == last_flex) {
        this_child_max_main_axis_size =
            max_main_axis_size - child_main_axis_size;
      } else {
        this_child_max_main_axis_size =
            f32_clamp(child->props.flex / total_flex * child_main_axis_flex, 0,
                      max_main_axis_size - child_main_axis_size);
      }

      // Tight constraint for child
      Vec2 this_child_max_size;
      vec2_set(&this_child_max_size, main_axis, this_child_max_main_axis_size);
      vec2_set(&this_child_max_size, cross_axis, max_cross_axis_size);
      Vec2 this_child_min_size;
      vec2_set(&this_child_min_size, main_axis, this_child_max_main_axis_size);
      if (box->props.cross_axis_align == UI_CROSS_AXIS_ALIGNMENT_STRETCH) {
        vec2_set(&this_child_min_size, cross_axis, max_cross_axis_size);
      } else {
        vec2_set(&this_child_min_size, cross_axis, 0.0f);
      }

      Vec2 this_child_size = layout_child(frame, child, this_child_min_size,
                                          this_child_max_size, main_axis);

      child_main_axis_size += vec2_get(this_child_size, main_axis);
      child_cross_axis_size =
          f32_max(child_cross_axis_size, vec2_get(this_child_size, cross_axis));
    }
  }

  Vec2 children_size = vec2(0, 0);
  vec2_set(&children_size, main_axis, child_main_axis_size);
  vec2_set(&children_size, cross_axis, child_cross_axis_size);
  return children_size;
}

static Vec2 layout_children(UIFrame *frame, UIBox *box, Vec2 max_size,
                            Axis2 main_axis, Axis2 cross_axis) {
  ASSERT(box->build.first);

  Vec2 result =
      should_children_flex(frame, box, max_size, main_axis, cross_axis);

  for (UIBox *child = box->build.first; child; child = child->build.next) {
    if (should_layout(child->props.position)) {
      continue;
    }

    layout_box(frame, child, vec2(0, 0), frame->viewport_size);
  }

  return result;
}

static void begin_stacking_context(UIFrame *frame, UIBox *box,
                                   bool create_new_stacking_context) {
  UIBox *parent = frame->current_stack;
  box->stack.parent = parent;
  if (parent) {
    UIBox *after = parent->stack.last;
    for (after = parent->stack.last; after; after = after->stack.prev) {
      if (after->props.z_index <= box->props.z_index) {
        break;
      }
    }
    if (after) {
      DLL_INSERT(parent->stack.first, parent->stack.last, after, box,
                 stack.prev, stack.next);
    } else {
      DLL_PREPEND(parent->stack.first, parent->stack.last, box, stack.prev,
                  stack.next);
    }
  }

  if (create_new_stacking_context) {
    frame->current_stack = box;
  }
}

static void end_stacking_context(UIFrame *frame, UIBox *box,
                                 bool create_new_stacking_context) {
  if (create_new_stacking_context) {
    frame->current_stack = box->stack.parent;
  }
}

static void build_stacking_context(UIFrame *frame, UIBox *box) {
  bool create_new_stacking_context =
      frame->root == box || box->props.position == kUIPositionFixed ||
      box->props.z_index != 0 || box->props.isolate;
  begin_stacking_context(frame, box, create_new_stacking_context);
  for (UIBox *child = box->build.first; child; child = child->build.next) {
    build_stacking_context(frame, child);
  }
  end_stacking_context(frame, box, create_new_stacking_context);
}

static void layout_box(UIFrame *frame, UIBox *box, Vec2 min_size,
                       Vec2 max_size) {
  ASSERTF(vec2_contains_including_end(min_size, vec2(0, 0), max_size),
          "min_size=(%.2f, %.2f), max_size=(%.2f, %.2f)", min_size.x,
          min_size.y, max_size.x, max_size.y);

  box->computed.min_size = min_size;
  box->computed.max_size = max_size;

  if (!should_layout(box->props.position)) {
    if (ui_edge_insets_is_right_set(box->props.offset) &&
        ui_edge_insets_is_left_set(box->props.offset)) {
      box->props.size.x =
          f32_max(box->props.offset.right - box->props.offset.left, 0);
    }

    if (ui_edge_insets_is_bottom_set(box->props.offset) &&
        ui_edge_insets_is_top_set(box->props.offset)) {
      box->props.size.y =
          f32_max(box->props.offset.bottom - box->props.offset.top, 0);
    }
  }

  Vec2 children_max_size = max_size;
  for (int axis = 0; axis < kAxis2Count; ++axis) {
    f32 min_size_axis = vec2_get(min_size, axis);
    f32 max_size_axis = vec2_get(max_size, axis);
    f32 build_size_axis = vec2_get(box->props.size, axis);
    if (build_size_axis == kUISizeInfinity) {
      // If it's infinity, let children be infinity.
      vec2_set(&children_max_size, axis, kUISizeInfinity);
    } else if (build_size_axis != kUISizeUndefined) {
      // If box has specific size, and is not infinity, use that (but also
      // respect the constraint) as constraint for children.
      vec2_set(&children_max_size, axis,
               f32_clamp(build_size_axis, min_size_axis, max_size_axis));
    } else {
      // Otherwise, pass down the constraint to children to make them as large
      // as possible
      vec2_set(&children_max_size, axis, max_size_axis);
    }
  }
  // Leave space for padding and border
  children_max_size.x = f32_max(
      children_max_size.x -
          (box->props.border.left.width + box->props.border.right.width) -
          (box->props.padding.left + box->props.padding.right),
      0);
  children_max_size.y = f32_max(
      children_max_size.y -
          (box->props.border.top.width + box->props.border.bottom.width) -
          (box->props.padding.top + box->props.padding.bottom),
      0);

  Axis2 main_axis = box->props.main_axis;
  Axis2 cross_axis = (main_axis + 1) % kAxis2Count;
  Vec2 children_size = vec2(0, 0);
  if (box->build.first) {
    if (!str8_is_empty(box->props.text)) {
      ui_push_build_errorf(frame,
                           "text content is ignored because box has children");
    }
    children_size =
        layout_children(frame, box, children_max_size, main_axis, cross_axis);
  } else if (!str8_is_empty(box->props.text)) {
    children_size = layout_text(box, children_max_size, main_axis, cross_axis);
  }

  // Size box itself
  for (int axis = 0; axis < kAxis2Count; ++axis) {
    f32 min_size_axis = vec2_get(min_size, axis);
    f32 max_size_axis = vec2_get(max_size, axis);

    f32 build_size_axis = vec2_get(box->props.size, axis);
    if (build_size_axis != kUISizeUndefined) {
      // If box has specific size, use that size but also respect the
      // constraint.
      vec2_set(&box->computed.size, axis,
               f32_clamp(build_size_axis, min_size_axis, max_size_axis));
    } else if (should_max_axis(box, axis, main_axis, max_size_axis)) {
      // If box should maximize this axis, regardless of it's children, do it.
      vec2_set(&box->computed.size, axis, max_size_axis);
    } else {
      // Size itself around children
      f32 border_start_axis =
          ui_border_get_start(box->props.border, axis).width;
      f32 border_end_axis = ui_border_get_end(box->props.border, axis).width;
      f32 padding_start_axis =
          ui_edge_insets_get_start(box->props.padding, axis);
      f32 padding_end_axis = ui_edge_insets_get_end(box->props.padding, axis);
      f32 children_size_axis = vec2_get(children_size, axis);
      f32 content_size_axis = children_size_axis + border_start_axis +
                              border_end_axis + padding_start_axis +
                              padding_end_axis;
      vec2_set(&box->computed.size, axis,
               f32_clamp(content_size_axis, min_size_axis, max_size_axis));
    }
  }

  ASSERTF(vec2_contains_including_end(box->computed.size, min_size, max_size),
          "computed_size=(%.2f, %.2f), min_size=(%.2f, %.2f), max_size=(%.2f, "
          "%.2f)",
          box->computed.size.x, box->computed.size.y, min_size.x, min_size.y,
          max_size.x, max_size.y);

  UIMainAxisAlignment main_axis_align = box->props.main_axis_align;
  align_main_axis(box, main_axis, main_axis_align,
                  vec2_get(children_size, main_axis));
  UICrossAxisAlignment cross_axis_align = box->props.cross_axis_align;
  align_cross_axis(box, cross_axis, cross_axis_align);
}

static void render_box(UIState *state, UIBox *box) {
  Vec2 min = box->computed.screen_rect.min;
  Vec2 max = box->computed.screen_rect.max;

  Rect2 clip_rect = box->computed.clip_rect;
  b32 need_clip = box->props.position != kUIPositionRelative;
  if (rect2_get_area(clip_rect) > 0) {
    if (need_clip) {
      push_clip_rect(clip_rect.min, clip_rect.max);
    }

    if (box->props.background_color.a) {
      fill_rect(min, max, box->props.background_color);
    }

    if (box->props.border.left.width > 0) {
      fill_rect(min, vec2(min.x + box->props.border.left.width, max.y),
                box->props.border.left.color);
    }

    if (box->props.border.top.width > 0) {
      fill_rect(min, vec2(max.x, min.y + box->props.border.top.width),
                box->props.border.top.color);
    }

    if (box->props.border.right.width > 0) {
      fill_rect(vec2(max.x - box->props.border.right.width, min.y), max,
                box->props.border.right.color);
    }

    if (box->props.border.bottom.width > 0) {
      fill_rect(vec2(min.x, max.y - box->props.border.bottom.width), max,
                box->props.border.bottom.color);
    }

    if (!str8_is_empty(box->props.text)) {
      draw_text_str8(
          vec2(min.x + box->props.border.left.width + box->props.padding.left,
             min.y + box->props.border.top.width + box->props.padding.top),
          box->props.text, box->computed.font_size,
          get_first_non_zero_color(box));
    }

    // Debug outline
    // stroke_rect(min, max, color_u32_from_hex(0xFF00FF), 1.0f);
  } else {
    need_clip = 0;
  }

  if (!need_clip) {
    push_clip_rect(clip_rect.min, clip_rect.max);
  }

  for (UIBox *child = box->stack.first; child; child = child->stack.next) {
    render_box(state, child);
  }

  pop_clip_rect();
}

#if 0
#include <stdlib.h>

#include "src/log.h"
static void ui_debug_print_r(UIBox *box, u32 level) {
  INFO(
      "%*s%s[seq=%u, key=%s, min_size=(%.2f, %.2f), max_size=(%.2f, %.2f), "
      "build_size=(%.2f, %.2f), size=(%.2f, %.2f), rel_pos=(%.2f, %.2f), "
      "screen_rect=((%.2f, %.2f), (%.2f, %.2f))], clip_rect=((%.2f, %.2f), "
      "(%.2f, %.2f))]",
      level * 4, "", box->tag, box->seq, box->props.key.ptr,
      box->computed.min_size.x, box->computed.min_size.y,
      box->computed.max_size.x, box->computed.max_size.y, box->props.size.x,
      box->props.size.y, box->computed.size.x, box->computed.size.y,
      box->computed.rel_pos.x, box->computed.rel_pos.y,
      box->computed.screen_rect.min.x, box->computed.screen_rect.min.y,
      box->computed.screen_rect.max.x, box->computed.screen_rect.max.y,
      box->computed.clip_rect.min.x, box->computed.clip_rect.min.y,
      box->computed.clip_rect.max.x, box->computed.clip_rect.max.y);
  for (UIBox *child = box->first; child; child = child->next) {
    ui_debug_print_r(child, level + 1);
  }
}

static void ui_debug_print(UIState *state) {
  if (state->frame_index > 1) {
    UIFrame *frame = ui_frame_get();
    for (UIBox *box = frame->first_box; box; box = box->next) {
      ui_debug_print_r(box, 0);
    }
    exit(0);
  }
}
#else
static void ui_debug_print(UIState *state) { (void)state; }
#endif

static void process_input_r(UIState *state, UIBox *box) {
  for (UIBox *child = box->stack.last; child; child = child->stack.prev) {
    process_input_r(state, child);
  }

  // Mouse input
  if (uuid_is_zero(state->input.mouse.hovering) && box->hoverable &&
      vec2_contains(state->input.mouse.pos, box->computed.clip_rect.min,
                    box->computed.clip_rect.max)) {
    state->input.mouse.hovering = box->id;
  }

  for (i32 button = 0; button < kUIMouseButtonCount; ++button) {
    if (uuid_is_zero(state->input.mouse.pressed[button]) &&
        box->clickable[button] &&
        vec2_contains(state->input.mouse.pos, box->computed.clip_rect.min,
                      box->computed.clip_rect.max) &&
        is_mouse_button_pressed(state, button)) {
      state->input.mouse.pressed[button] = box->id;
      state->input.mouse.pressed_pos[button] = state->input.mouse.pos;
    }
  }

  if (uuid_is_zero(state->input.mouse.scrolling) && box->scrollable &&
      !vec2_is_zero(state->input.mouse.wheel) &&
      vec2_contains(state->input.mouse.pos, box->computed.clip_rect.min,
                    box->computed.clip_rect.max)) {
    state->input.mouse.scrolling = box->id;
    state->input.mouse.scroll_delta = state->input.mouse.wheel;
  }
}

static void process_input(UIState *state, UIFrame *frame) {
  state->input.mouse.hovering = uuid_zero();
  state->input.mouse.scrolling = uuid_zero();
  for (i32 button = 0; button < kUIMouseButtonCount; ++button) {
    state->input.mouse.pressed[button] = uuid_zero();
    state->input.mouse.clicked[button] = uuid_zero();
    if (!uuid_is_zero(state->input.mouse.holding[button])) {
      state->input.mouse.hovering = state->input.mouse.holding[button];
    }
  }

  process_input_r(state, frame->root);

  for (i32 button = 0; button < kUIMouseButtonCount; ++button) {
    if (!uuid_is_zero(state->input.mouse.pressed[button])) {
      state->input.mouse.holding[button] = state->input.mouse.pressed[button];
    }

    if (is_mouse_button_clicked(state, button)) {
      UIID id = state->input.mouse.holding[button];
      UIBox *box = ui_box_get(frame, id);
      if (box &&
          vec2_contains(state->input.mouse.pos, box->computed.clip_rect.min,
                        box->computed.clip_rect.max)) {
        state->input.mouse.clicked[button] = id;
      }
      state->input.mouse.holding[button] = uuid_zero();
    }

    state->input.mouse.buttons[button].transition_count = 0;
  }
  state->input.mouse.wheel = vec2(0, 0);
}

static Vec2 position_absolute(Vec2 min, Vec2 max, UIEdgeInsets offset,
                              Vec2 size, UIEdgeInsets margin) {
  Vec2 result;
  if (ui_edge_insets_is_left_set(offset)) {
    result.x = min.x + offset.left;
  } else if (ui_edge_insets_is_right_set(offset)) {
    result.x = max.x - offset.right - size.x;
  } else {
    result.x = min.x;
  }

  if (ui_edge_insets_is_top_set(offset)) {
    result.y = min.y + offset.top;
  } else if (ui_edge_insets_is_bottom_set(offset)) {
    result.y = max.y - offset.bottom - size.y;
  } else {
    result.y = min.y;
  }

  result.x += margin.left;
  result.y += margin.top;

  return result;
}

static void position_box(UIFrame *frame, UIBox *box, Vec2 parent_min,
                         Vec2 parent_max, Rect2 parent_clip_rect) {
  Vec2 min, max;
  bool ignore_parent_clip_rect = false;
  switch (box->props.position) {
    case kUIPositionRelative: {
      Vec2 offset = vec2(box->props.offset.left - box->props.offset.right,
                       box->props.offset.top - box->props.offset.bottom);
      min = vec2_add(vec2_add(parent_min, box->computed.rel_pos), offset);
      max = vec2_add(min, box->computed.size);
    } break;

    case kUIposition_absolute: {
      Vec2 size = box->computed.size;
      min = position_absolute(parent_min, parent_max, box->props.offset, size,
                              box->props.margin);
      max = vec2_add(min, size);
      ignore_parent_clip_rect = true;
    } break;

    case kUIPositionFixed: {
      Vec2 size = box->computed.size;
      min = position_absolute(vec2(0, 0), frame->viewport_size, box->props.offset,
                              size, box->props.margin);
      max = vec2_add(min, size);
      ignore_parent_clip_rect = true;
    } break;

    default: {
      UNREACHABLE;
    } break;
  }

  Rect2 screen_rect = r2(min, max);
  Rect2 clip_rect;
  if (ignore_parent_clip_rect) {
    clip_rect = screen_rect;
  } else {
    clip_rect = rect2_from_intersection(parent_clip_rect, screen_rect);
  }

  box->computed.screen_rect = screen_rect;
  box->computed.clip_rect = clip_rect;

  for (UIBox *child = box->build.first; child; child = child->build.next) {
    position_box(frame, child, min, max, box->computed.clip_rect);
  }
}

void ui_end_frame(void) {
  UIState *state = ui_state_get();
  UIFrame *frame = ui_frame_get();
  ui_tag_end("Root");
  ASSERTF(!frame->current_build, "Mismatched BeginBox/EndBox calls");

  Vec2 size = frame->viewport_size;
  layout_box(frame, frame->root, vec2(0, 0), size);
  frame->root->computed.rel_pos = vec2(0, 0);

  build_stacking_context(frame, frame->root);
  ASSERT(!frame->current_stack);

  position_box(frame, frame->root, vec2(0, 0), size, r2(vec2(0, 0), size));

  process_input(state, frame);
  ui_debug_print(state);
}

void ui_render(void) {
  UIState *state = ui_state_get();
  UIFrame *frame = ui_frame_get();

  ASSERTF(!frame->first_error, "%s", frame->first_error->message.ptr);

  render_box(state, frame->root);
}

static UIID UIIDFromStr8(UIID seed, Str8 str) {
  UIID result = seed;
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

UIID uuid_from_u8(UIID seed, u8 ch) {
  u8 str[2] = {ch, 0};
  UIID result = UIIDFromStr8(seed, (Str8){.ptr = str, .len = 1});
  return result;
}

UIBuildError *ui_get_first_build_error(void) {
  UIFrame *frame = ui_frame_get();
  UIBuildError *result = frame->first_error;
  return result;
}

bool uuid_is_equal(UIID a, UIID b) {
  bool result = a.hash == b.hash;
  return result;
}

Str8 ui_push_str8(Str8 str) {
  UIFrame *frame = ui_frame_get();
  Str8 result = arena_push_str8(&frame->arena, str);
  return result;
}

Str8 ui_push_str8f(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  Str8 result = ui_push_str8fv(fmt, ap);
  va_end(ap);
  return result;
}

Str8 ui_push_str8fv(const char *fmt, va_list ap) {
  UIFrame *frame = ui_frame_get();
  Str8 result = arena_push_str8fv(&frame->arena, fmt, ap);
  return result;
}

static UIID uuid_from_u32(UIID seed, u32 num) {
  UIID id = seed;
  u32 s = num;
  while (s > 0) {
    id = uuid_from_u8(id, (u8)(s & 0xFF));
    s = s >> 8;
  }
  return id;
}

static UIID uuid_for_box(UIID seed, u32 seq, const char *tag, Str8 key) {
  // id = seed + tag + (key || seq)
  UIID id = seed;
  id = UIIDFromStr8(id, str8_from_cstr(tag));
  if (!str8_is_empty(key)) {
    id = UIIDFromStr8(id, key);
  } else {
    id = uuid_from_u32(id, seq);
  }
  return id;
}

static UIBox *ui_box_get_from_last_frame(UIID id) {
  UIBox *result = 0;
  UIFrame *last_frame = ui_frame_get_last();
  if (last_frame) {
    result = ui_box_get(last_frame, id);
  }
  return result;
}

void ui_tag_begin(const char *tag, UIProps props) {
  UIFrame *frame = ui_frame_get();

  UIBox *parent = frame->current_build;
  UIID seed;
  if (parent) {
    seed = parent->id;
  } else {
    seed = uuid_zero();
  }
  u32 seq = 0;
  if (parent) {
    seq = parent->children_count;
  }

  UIID id = uuid_for_box(seed, seq, tag, props.key);
  UIBox *box = ui_box_push(&frame->cache, &frame->arena, id);
  box->tag = tag;
  box->seq = seq;

  if (parent) {
    DLL_APPEND(parent->build.first, parent->build.last, box, build.prev,
               build.next);
    ++parent->children_count;
  }
  box->build.parent = parent;
  box->props = props;

  // Copy computed state from last frame, if any
  UIBox *last_box = ui_box_get_from_last_frame(id);
  if (last_box) {
    box->computed = last_box->computed;
  }

  frame->current_build = box;
}

void ui_tag_end(const char *tag) {
  UIFrame *frame = ui_frame_get();
  UIBox *box = frame->current_build;
  ASSERT(box);
  ASSERTF(strcmp(box->tag, tag) == 0,
          "Mismatched Begin/End calls. Begin with %s, end with %s", box->tag,
          tag);

  frame->current_build = box->build.parent;
}

void *ui_box_push_state(const char *type_name, usize size) {
  UIFrame *frame = ui_frame_get();
  UIBox *box = ui_box_get_current();
  ASSERTF(!box->state.ptr, "Can only push once to the UIBox");

  DEBUG_ASSERT(ui_box_get(frame, box->id) == box);
  box->state.type_name = type_name;
  box->state.size = size;

  UIBox *last_box = ui_box_get_from_last_frame(box->id);
  if (last_box && last_box->state.ptr) {
    ASSERTF(last_box->state.size == box->state.size &&
                strcmp(last_box->state.type_name, type_name) == 0,
            "The type pushed to this box (%s) is not the same as the last "
            "frame (%s)",
            type_name, last_box->state.type_name);
    box->state.ptr = arena_push(&frame->arena, size, ARENA_PUSH_NO_ZERO);
    // Copy state from last frame
    memcpy(box->state.ptr, last_box->state.ptr, size);
  } else {
    box->state.ptr = arena_push(&frame->arena, size, 0);
  }

  return box->state.ptr;
}

void *ui_box_get_state(const char *type_name, usize size) {
  UIBox *box = ui_box_get_current();
  ASSERTF(box->state.ptr, "UIBox doesn't have state");
  ASSERTF(
      box->state.size == size && strcmp(box->state.type_name, type_name) == 0,
      "The type currently requested (%s) is not the same as the one pushed "
      "(%s)",
      type_name, box->state.type_name);
  void *result = box->state.ptr;
  return result;
}

Vec2 ui_get_mouse_rel_pos(void) {
  UIState *state = ui_state_get();
  UIBox *box = ui_box_get_current();
  Vec2 result = vec2(0, 0);
  if (box) {
    result = vec2_sub(state->input.mouse.pos, box->computed.screen_rect.min);
  }
  return result;
}

Vec2 ui_get_mouse_pos(void) {
  UIState *state = ui_state_get();
  Vec2 result = state->input.mouse.pos;
  return result;
}

void ui_set_block_mouse_input(void) {
  UIBox *box = ui_box_get_current();
  box->hoverable = true;
  for (i32 button = 0; button < kUIMouseButtonCount; ++button) {
    box->clickable[button] = true;
  }
  box->scrollable = true;
}

bool ui_is_mouse_hovering(void) {
  UIState *state = ui_state_get();
  UIBox *box = ui_box_get_current();
  bool result = false;
  if (box) {
    box->hoverable = true;
    result = uuid_is_equal(state->input.mouse.hovering, box->id);
  }
  return result;
}

bool ui_is_mouse_button_pressed(UIMouseButton button) {
  UIState *state = ui_state_get();
  UIBox *box = ui_box_get_current();
  bool result = false;
  if (box) {
    box->clickable[button] = true;
    result = uuid_is_equal(state->input.mouse.pressed[button], box->id);
  }
  return result;
}

bool ui_is_mouse_button_down(UIMouseButton button) {
  UIState *state = ui_state_get();
  UIBox *box = ui_box_get_current();
  bool result = false;
  if (box) {
    box->clickable[button] = true;
    result = uuid_is_equal(state->input.mouse.holding[button], box->id);
  }
  return result;
}

bool ui_is_mouse_button_clicked(UIMouseButton button) {
  UIState *state = ui_state_get();
  UIBox *box = ui_box_get_current();
  bool result = false;
  if (box) {
    box->clickable[button] = true;
    result = uuid_is_equal(state->input.mouse.clicked[button], box->id);
  }
  return result;
}

bool ui_is_mouse_button_dragging(UIMouseButton button, Vec2 *delta) {
  UIState *state = ui_state_get();
  UIBox *box = ui_box_get_current();
  bool result = false;
  if (box) {
    box->clickable[button] = true;
    result = uuid_is_equal(state->input.mouse.holding[button], box->id);
    if (result && delta) {
      *delta = vec2_sub(state->input.mouse.pos,
                        state->input.mouse.pressed_pos[button]);
    }
  }
  return result;
}

bool ui_is_mouse_button_scrolling(Vec2 *delta) {
  UIState *state = ui_state_get();
  UIBox *box = ui_box_get_current();
  bool result = false;
  if (box) {
    box->scrollable = true;
    result = uuid_is_equal(state->input.mouse.scrolling, box->id);
    if (result && delta) {
      *delta = state->input.mouse.scroll_delta;
    }
  }
  return result;
}

