#include "src/ui.h"

#include <stdarg.h>
#include <string.h>

#include "src/assert.h"
#include "src/draw.h"
#include "src/list.h"
#include "src/math.h"
#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"

static UIBox *GetUIBoxFromFrame(UIFrame *frame, UIID id) {
  UIBox *result = 0;
  UIBoxCache *cache = &frame->cache;
  if (!IsZeroUIID(id) && cache->box_hash_slots) {
    UIBoxHashSlot *slot =
        &cache->box_hash_slots[id.hash % cache->box_hash_slots_count];
    for (UIBox *box = slot->first; box; box = box->hash_next) {
      if (IsEqualUIID(box->id, id)) {
        result = box;
        break;
      }
    }
  }
  return result;
}

static UIBox *PushUIBox(UIBoxCache *cache, Arena *arena, UIID id) {
  ASSERT(!IsZeroUIID(id));
  UIBox *result = PushArray(arena, UIBox, 1);
  result->id = id;

  UIBoxHashSlot *slot =
      &cache->box_hash_slots[id.hash % cache->box_hash_slots_count];
  APPEND_DOUBLY_LINKED_LIST(slot->first, slot->last, result, hash_prev,
                            hash_next);
  ++cache->total_box_count;

  return result;
}

thread_local UIState t_ui_state;

UIState *GetUIState(void) {
  UIState *state = &t_ui_state;
  ASSERTF(state->init, "InitUI is not called");
  return state;
}

static UIFrame *GetCurrentUIFrame(UIState *state) {
  UIFrame *result =
      state->frames + (state->frame_index % ARRAY_COUNT(state->frames));
  ASSERT(result->frame_index == state->frame_index);
  return result;
}

static UIFrame *GetLastUIFrame(UIState *state) {
  UIFrame *result = 0;
  if (state->frame_index > 1) {
    result =
        state->frames + ((state->frame_index - 1) % ARRAY_COUNT(state->frames));
    ASSERT(result->frame_index == state->frame_index - 1);
  }
  return result;
}

static void PushUIBuildErrorF(UIFrame *frame, const char *fmt, ...) {
  UIBuildError *error = PushArray(&frame->arena, UIBuildError, 1);

  va_list ap;
  va_start(ap, fmt);
  error->message = PushStr8FV(&frame->arena, fmt, ap);
  va_end(ap);

  APPEND_DOUBLY_LINKED_LIST(frame->first_error, frame->last_error, error, prev,
                            next);
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

void InitUI(void) {
  UIState *state = &t_ui_state;
  ASSERTF(!state->init, "InitUI called more than once");

  state->init = 1;
  state->input.dt = 1.0f / 60.0f;  // Assume 60 FPS by default.
  state->input.mouse.pos = V2(-1, -1);
}

void QuitUI(void) {
  UIState *state = GetUIState();
  FreeArena(&state->frames[0].arena);
  FreeArena(&state->frames[1].arena);
  *state = (UIState){0};
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

void OnUIMouseWheel(Vec2 delta) {
  UIState *state = GetUIState();

  state->input.mouse.wheel = delta;
}

void SetUIDeltaTime(f32 dt) {
  UIState *state = GetUIState();

  state->input.dt = dt;
  state->fast_rate = 1.0f - ExpF32(-50.f * dt);
}

void SetUICanvasSize(Vec2 size) {
  UIState *state = GetUIState();
  state->input.canvas_size = size;
}

void BeginUIFrame(void) {
  UIState *state = GetUIState();
  state->frame_index += 1;

  UIFrame *frame =
      state->frames + (state->frame_index % ARRAY_COUNT(state->frames));

  ResetArena(&frame->arena);

  frame->cache = (UIBoxCache){0};
  frame->cache.box_hash_slots_count = 4096;
  frame->cache.box_hash_slots = PushArray(&frame->arena, UIBoxHashSlot,
                                          frame->cache.box_hash_slots_count);

  frame->frame_index = state->frame_index;
  frame->first_layer = frame->last_layer = frame->current_layer = 0;
  frame->first_error = frame->last_error = 0;
}

static inline f32 GetEdgeInsetsSize(UIEdgeInsets edge_insets, Axis2 axis) {
  // TODO: Handle text direction.
  f32 result;
  if (axis == kAxis2X) {
    result = edge_insets.left + edge_insets.right;
  } else {
    result = edge_insets.top + edge_insets.bottom;
  }
  return result;
}

static inline f32 GetEdgeInsetsStart(UIEdgeInsets edge_insets, Axis2 axis) {
  // TODO: Handle text direction.
  f32 result;
  if (axis == kAxis2X) {
    result = edge_insets.left;
  } else {
    result = edge_insets.top;
  }
  return result;
}

static inline f32 GetEdgeInsetsEnd(UIEdgeInsets edge_insets, Axis2 axis) {
  // TODO: Handle text direction.
  f32 result;
  if (axis == kAxis2X) {
    result = edge_insets.right;
  } else {
    result = edge_insets.bottom;
  }
  return result;
}

static inline UIBorderSide GetBorderSideStart(UIBorder border, Axis2 axis) {
  UIBorderSide result;
  if (axis == kAxis2X) {
    result = border.left;
  } else {
    result = border.top;
  }
  return result;
}

static inline UIBorderSide GetBorderSideEnd(UIBorder border, Axis2 axis) {
  UIBorderSide result;
  if (axis == kAxis2X) {
    result = border.right;
  } else {
    result = border.bottom;
  }
  return result;
}

static void AlignMainAxisFlex(UIBox *box, Axis2 axis, f32 border_start,
                              f32 border_end, f32 padding_start,
                              f32 padding_end, UIMainAxisAlign align,
                              f32 children_size) {
  f32 size_axis = GetItemVec2(box->computed.size, axis);
  f32 free = size_axis - children_size - border_start - border_end -
             padding_start - padding_end;
  f32 pos = border_start + padding_start;
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
    pos += GetEdgeInsetsStart(child->props.margin, axis);
    box->computed.clip = box->computed.clip || (pos < 0 || pos > size_axis);
    SetItemVec2(&child->computed.rel_pos, axis, pos);
    pos += GetItemVec2(child->computed.size, axis) +
           GetEdgeInsetsEnd(child->props.margin, axis);
    box->computed.clip = box->computed.clip || (pos < 0 || pos > size_axis);
  }
}

static void AlignMainAxisStack(UIBox *box, Axis2 axis, f32 border_start,
                               f32 border_end, f32 padding_start,
                               f32 padding_end, UIMainAxisAlign align) {
  f32 self_size = GetItemVec2(box->computed.size, axis);
  for (UIBox *child = box->first; child; child = child->next) {
    f32 pos = 0;
    f32 child_size = GetItemVec2(child->computed.size, axis);
    f32 free = self_size - child_size - border_start - border_end -
               padding_start - padding_end -
               GetEdgeInsetsSize(child->props.margin, axis);
    f32 margin_start = GetEdgeInsetsStart(child->props.margin, axis);
    switch (align) {
      case kUIMainAxisAlignStart: {
        pos = border_start + padding_start + margin_start;
      } break;

      case kUIMainAxisAlignCenter: {
        pos = border_start + padding_start + margin_start + free / 2.0f;
      } break;

      case kUIMainAxisAlignEnd: {
        pos = border_start + padding_start + margin_start + free;
      } break;

      default: {
        UNREACHABLE;
      } break;
    }

    SetItemVec2(&child->computed.rel_pos, axis, pos);

    box->computed.clip =
        box->computed.clip || (pos < 0 || pos + child_size > self_size);
  }
}

static void AlignMainAxis(UIBox *box, Axis2 axis, UIMainAxisAlign align,
                          f32 children_size) {
  f32 border_start = GetBorderSideStart(box->props.border, axis).width;
  f32 border_end = GetBorderSideEnd(box->props.border, axis).width;
  f32 padding_start = GetEdgeInsetsStart(box->props.padding, axis);
  f32 padding_end = GetEdgeInsetsEnd(box->props.padding, axis);

  switch (box->props.layout) {
    case kUILayoutFlex: {
      AlignMainAxisFlex(box, axis, border_start, border_end, padding_start,
                        padding_end, align, children_size);
    } break;

    case kUILayoutStack: {
      AlignMainAxisStack(box, axis, border_start, border_end, padding_start,
                         padding_end, align);
    } break;

    default: {
      UNREACHABLE;
    } break;
  }
}

static void AlignCrossAxis(UIBox *box, Axis2 axis, UICrossAxisAlign align) {
  f32 border_start = GetBorderSideStart(box->props.border, axis).width;
  f32 border_end = GetBorderSideEnd(box->props.border, axis).width;
  f32 padding_start = GetEdgeInsetsStart(box->props.padding, axis);
  f32 padding_end = GetEdgeInsetsEnd(box->props.padding, axis);

  f32 self_size = GetItemVec2(box->computed.size, axis);
  for (UIBox *child = box->first; child; child = child->next) {
    f32 child_size = GetItemVec2(child->computed.size, axis);
    f32 free = self_size - child_size - border_start - border_end -
               padding_start - padding_end -
               GetEdgeInsetsSize(child->props.margin, axis);
    f32 margin_start = GetEdgeInsetsStart(child->props.margin, axis);
    f32 pos = 0;
    switch (align) {
      case kUICrossAxisAlignStart:
      case kUICrossAxisAlignStretch: {
        pos = border_start + padding_start + margin_start;
      } break;

      case kUICrossAxisAlignCenter: {
        pos = border_start + padding_start + margin_start + free / 2.0f;
      } break;

      case kUICrossAxisAlignEnd: {
        pos = border_start + padding_start + margin_start + free;
      } break;

      default: {
        UNREACHABLE;
      } break;
    }

    SetItemVec2(&child->computed.rel_pos, axis, pos);
    box->computed.clip =
        box->computed.clip || (pos < 0 || pos + child_size > self_size);
  }
}

static inline b32 ShouldMaxAxis(UIBox *box, int axis, Axis2 main_axis,
                                f32 max_size_axis) {
  // cross axis is always as small as possible
  b32 result = box->props.main_axis_size == kUIMainAxisSizeMax &&
               axis == (int)main_axis && max_size_axis != F32_INFINITY;
  return result;
}

static f32 GetFirstNonZeroFontSize(UIBox *box) {
  f32 font_size = box->props.font_size;
  if (font_size <= 0 && box->parent) {
    font_size = GetFirstNonZeroFontSize(box->parent);
  }
  return font_size;
}

static ColorU32 GetFirstNonZeroColor(UIBox *box) {
  ColorU32 color = box->props.color;
  if (color.a == 0 && box->parent) {
    color = GetFirstNonZeroColor(box->parent);
  }
  return color;
}

static Vec2 LayoutText(UIBox *box, Vec2 max_size, Axis2 main_axis,
                       Axis2 cross_axis) {
  ASSERT(!IsEmptyStr8(box->props.text));

  // TODO: constraint text size within [(0, 0), max_size]

  f32 font_size = GetFirstNonZeroFontSize(box);
  if (font_size <= 0) {
    font_size = kUIFontSizeDefault;
  }
  box->computed.font_size = font_size;
  TextMetrics metrics = GetTextMetricsStr8(box->props.text, font_size);
  Vec2 text_size = metrics.size;
  text_size = MinVec2(text_size, max_size);

  Vec2 children_size;
  SetItemVec2(&children_size, main_axis, GetItemVec2(text_size, main_axis));
  SetItemVec2(&children_size, cross_axis, GetItemVec2(text_size, cross_axis));
  return children_size;
}

static void LayoutBox(UIFrame *frame, UIBox *box, Vec2 min_size, Vec2 max_size);

static Vec2 LayoutChild(UIFrame *frame, UIBox *child, Vec2 min_size,
                        Vec2 max_size, Axis2 main_axis) {
  // Leave space for margin
  f32 margin_x = child->props.margin.left + child->props.margin.right;
  f32 margin_y = child->props.margin.top + child->props.margin.bottom;
  max_size.x = MaxF32(max_size.x - margin_x, 0);
  max_size.y = MaxF32(max_size.y - margin_y, 0);

  LayoutBox(frame, child, min_size, max_size);

  // Add margin back
  Vec2 child_size;
  child_size.x = MinF32(child->computed.size.x + margin_x, max_size.x);
  child_size.y = MinF32(child->computed.size.y + margin_y, max_size.y);

  if (GetItemVec2(child_size, main_axis) == kUISizeInfinity &&
      GetItemVec2(max_size, main_axis) == kUISizeInfinity) {
    PushUIBuildErrorF(
        frame, "Cannot have unbounded content within unbounded constraint");
  }

  return child_size;
}

static Vec2 LayoutChildrenFlex(UIFrame *frame, UIBox *box, Vec2 max_size,
                               Axis2 main_axis, Axis2 cross_axis) {
  f32 max_main_axis_size = GetItemVec2(max_size, main_axis);
  f32 max_cross_axis_size = GetItemVec2(max_size, cross_axis);

  f32 child_main_axis_size = 0.0f;
  f32 child_cross_axis_size = 0.0f;

  f32 total_flex = 0;
  UIBox *last_flex = 0;

  // First pass: layout non-flex children
  for (UIBox *child = box->first; child; child = child->next) {
    total_flex += child->props.flex;
    if (!child->props.flex) {
      Vec2 this_child_max_size;
      SetItemVec2(&this_child_max_size, main_axis,
                  max_main_axis_size - child_main_axis_size);
      SetItemVec2(&this_child_max_size, cross_axis, max_cross_axis_size);
      Vec2 this_child_min_size = {0};
      if (box->props.cross_axis_align == kUICrossAxisAlignStretch) {
        SetItemVec2(&this_child_min_size, cross_axis,
                    GetItemVec2(this_child_max_size, cross_axis));
      }

      Vec2 this_child_size = LayoutChild(frame, child, this_child_min_size,
                                         this_child_max_size, main_axis);

      child_main_axis_size += GetItemVec2(this_child_size, main_axis);
      child_cross_axis_size = MaxF32(child_cross_axis_size,
                                     GetItemVec2(this_child_size, cross_axis));
    } else {
      last_flex = child;
    }
  }

  // Second pass: layout flex children
  f32 child_main_axis_flex = max_main_axis_size - child_main_axis_size;
  for (UIBox *child = box->first; child; child = child->next) {
    if (child->props.flex) {
      if (max_main_axis_size == kUISizeInfinity) {
        PushUIBuildErrorF(frame, "Unbounded constraint doesn't work with flex");
      }

      f32 this_child_max_main_axis_size;
      if (child == last_flex) {
        this_child_max_main_axis_size =
            max_main_axis_size - child_main_axis_size;
      } else {
        this_child_max_main_axis_size =
            ClampF32(child->props.flex / total_flex * child_main_axis_flex, 0,
                     max_main_axis_size - child_main_axis_size);
      }

      // Tight constraint for child
      Vec2 this_child_max_size;
      SetItemVec2(&this_child_max_size, main_axis,
                  this_child_max_main_axis_size);
      SetItemVec2(&this_child_max_size, cross_axis, max_cross_axis_size);
      Vec2 this_child_min_size;
      SetItemVec2(&this_child_min_size, main_axis,
                  this_child_max_main_axis_size);
      if (box->props.cross_axis_align == kUICrossAxisAlignStretch) {
        SetItemVec2(&this_child_min_size, cross_axis, max_cross_axis_size);
      } else {
        SetItemVec2(&this_child_min_size, cross_axis, 0.0f);
      }

      Vec2 this_child_size = LayoutChild(frame, child, this_child_min_size,
                                         this_child_max_size, main_axis);

      child_main_axis_size += GetItemVec2(this_child_size, main_axis);
      child_cross_axis_size = MaxF32(child_cross_axis_size,
                                     GetItemVec2(this_child_size, cross_axis));
    }
  }

  Vec2 children_size = V2(0, 0);
  SetItemVec2(&children_size, main_axis, child_main_axis_size);
  SetItemVec2(&children_size, cross_axis, child_cross_axis_size);
  return children_size;
}

static Vec2 LayoutChildrenStack(UIFrame *frame, UIBox *box, Vec2 max_size,
                                Axis2 main_axis, Axis2 cross_axis) {
  f32 max_main_axis_size = GetItemVec2(max_size, main_axis);
  f32 max_cross_axis_size = GetItemVec2(max_size, cross_axis);

  f32 child_main_axis_size = 0.0f;
  f32 child_cross_axis_size = 0.0f;

  for (UIBox *child = box->first; child; child = child->next) {
    Vec2 this_child_max_size;
    SetItemVec2(&this_child_max_size, main_axis, max_main_axis_size);
    SetItemVec2(&this_child_max_size, cross_axis, max_cross_axis_size);
    Vec2 this_child_min_size = {0};
    if (box->props.cross_axis_align == kUICrossAxisAlignStretch) {
      SetItemVec2(&this_child_min_size, cross_axis,
                  GetItemVec2(this_child_max_size, cross_axis));
    }

    Vec2 this_child_size = LayoutChild(frame, child, this_child_min_size,
                                       this_child_max_size, main_axis);

    child_main_axis_size =
        MaxF32(child_main_axis_size, GetItemVec2(this_child_size, main_axis));
    child_cross_axis_size =
        MaxF32(child_cross_axis_size, GetItemVec2(this_child_size, cross_axis));
  }

  Vec2 children_size = V2(0, 0);
  SetItemVec2(&children_size, main_axis, child_main_axis_size);
  SetItemVec2(&children_size, cross_axis, child_cross_axis_size);
  return children_size;
}

static Vec2 LayoutChildren(UIFrame *frame, UIBox *box, Vec2 max_size,
                           Axis2 main_axis, Axis2 cross_axis) {
  ASSERT(box->first);

  Vec2 result;
  switch (box->props.layout) {
    case kUILayoutFlex: {
      result = LayoutChildrenFlex(frame, box, max_size, main_axis, cross_axis);
    } break;
    case kUILayoutStack: {
      result = LayoutChildrenStack(frame, box, max_size, main_axis, cross_axis);
    } break;
    default: {
      UNREACHABLE;
    } break;
  }
  return result;
}

static void LayoutBox(UIFrame *frame, UIBox *box, Vec2 min_size,
                      Vec2 max_size) {
  ASSERTF(ContainsVec2IncludingEnd(min_size, V2(0, 0), max_size),
          "min_size=(%.2f, %.2f), max_size=(%.2f, %.2f)", min_size.x,
          min_size.y, max_size.x, max_size.y);

  box->computed.min_size = min_size;
  box->computed.max_size = max_size;
  box->computed.clip = 0;

  Vec2 children_max_size = max_size;
  for (int axis = 0; axis < kAxis2Count; ++axis) {
    f32 min_size_axis = GetItemVec2(min_size, axis);
    f32 max_size_axis = GetItemVec2(max_size, axis);
    f32 build_size_axis = GetItemVec2(box->props.size, axis);
    if (build_size_axis == kUISizeInfinity) {
      // If it's infinity, let children be infinity.
      SetItemVec2(&children_max_size, axis, kUISizeInfinity);
    } else if (build_size_axis != kUISizeUndefined) {
      // If box has specific size, and is not infinity, use that (but also
      // respect the constraint) as constraint for children.
      SetItemVec2(&children_max_size, axis,
                  ClampF32(build_size_axis, min_size_axis, max_size_axis));
    } else {
      // Otherwise, pass down the constraint to children to make them as large
      // as possible
      SetItemVec2(&children_max_size, axis, max_size_axis);
    }
  }
  // Leave space for padding and border
  children_max_size.x = MaxF32(
      children_max_size.x -
          (box->props.border.left.width + box->props.border.right.width) -
          (box->props.padding.left + box->props.padding.right),
      0);
  children_max_size.y = MaxF32(
      children_max_size.y -
          (box->props.border.top.width + box->props.border.bottom.width) -
          (box->props.padding.top + box->props.padding.bottom),
      0);

  Axis2 main_axis = box->props.main_axis;
  Axis2 cross_axis = (main_axis + 1) % kAxis2Count;
  Vec2 children_size = V2(0, 0);
  if (box->first) {
    if (!IsEmptyStr8(box->props.text)) {
      PushUIBuildErrorF(frame,
                        "text content is ignored because box has children");
    }
    children_size =
        LayoutChildren(frame, box, children_max_size, main_axis, cross_axis);
  } else if (!IsEmptyStr8(box->props.text)) {
    children_size = LayoutText(box, children_max_size, main_axis, cross_axis);
  }

  // Size box itself
  for (int axis = 0; axis < kAxis2Count; ++axis) {
    f32 min_size_axis = GetItemVec2(min_size, axis);
    f32 max_size_axis = GetItemVec2(max_size, axis);

    f32 build_size_axis = GetItemVec2(box->props.size, axis);
    if (build_size_axis != kUISizeUndefined) {
      // If box has specific size, use that size but also respect the
      // constraint.
      SetItemVec2(&box->computed.size, axis,
                  ClampF32(build_size_axis, min_size_axis, max_size_axis));
    } else if (ShouldMaxAxis(box, axis, main_axis, max_size_axis)) {
      // If box should maximize this axis, regardless of it's children, do it.
      SetItemVec2(&box->computed.size, axis, max_size_axis);
    } else {
      // Size itself around children
      f32 border_start_axis = GetBorderSideStart(box->props.border, axis).width;
      f32 border_end_axis = GetBorderSideEnd(box->props.border, axis).width;
      f32 padding_start_axis = GetEdgeInsetsStart(box->props.padding, axis);
      f32 padding_end_axis = GetEdgeInsetsEnd(box->props.padding, axis);
      f32 children_size_axis = GetItemVec2(children_size, axis);
      f32 content_size_axis = children_size_axis + border_start_axis +
                              border_end_axis + padding_start_axis +
                              padding_end_axis;
      SetItemVec2(&box->computed.size, axis,
                  ClampF32(content_size_axis, min_size_axis, max_size_axis));
    }
  }

  ASSERTF(ContainsVec2IncludingEnd(box->computed.size, min_size, max_size),
          "computed_size=(%.2f, %.2f), min_size=(%.2f, %.2f), max_size=(%.2f, "
          "%.2f)",
          box->computed.size.x, box->computed.size.y, min_size.x, min_size.y,
          max_size.x, max_size.y);

  UIMainAxisAlign main_axis_align = box->props.main_axis_align;
  if (main_axis_align == kUIMainAxisAlignUnknown) {
    main_axis_align = kUIMainAxisAlignStart;
  }
  AlignMainAxis(box, main_axis, main_axis_align,
                GetItemVec2(children_size, main_axis));
  UICrossAxisAlign cross_axis_align = box->props.cross_axis_align;
  if (cross_axis_align == kUICrossAxisAlignUnknown) {
    cross_axis_align = kUICrossAxisAlignStart;
  }
  AlignCrossAxis(box, cross_axis, cross_axis_align);
  // Clip if content size exceeds self size.
  box->computed.clip =
      box->computed.clip ||
      children_size.x + box->props.padding.left + box->props.padding.right >
          box->computed.size.x ||
      children_size.y + box->props.padding.top + box->props.padding.bottom >
          box->computed.size.y;
}

static void RenderBox(UIState *state, UIBox *box) {
  Vec2 min = box->computed.screen_rect.min;
  Vec2 max = box->computed.screen_rect.max;

  Rect2 clip_rect = box->computed.clip_rect;
  f32 clip_area = GetRect2Area(clip_rect);
  if (clip_area > 0) {
    b32 need_clip = box->computed.clip;
    if (need_clip) {
      PushClipRect(clip_rect.min, clip_rect.max);
    }

    if (box->props.background_color.a) {
      DrawRect(min, max, box->props.background_color);
    }

    if (box->props.border.left.width > 0) {
      DrawRect(min, V2(min.x + box->props.border.left.width, max.y),
               box->props.border.left.color);
    }

    if (box->props.border.top.width > 0) {
      DrawRect(min, V2(max.x, min.y + box->props.border.top.width),
               box->props.border.left.color);
    }

    if (box->props.border.right.width > 0) {
      DrawRect(V2(max.x - box->props.border.right.width, min.y), max,
               box->props.border.left.color);
    }

    if (box->props.border.bottom.width > 0) {
      DrawRect(V2(min.x, max.y - box->props.border.bottom.width), max,
               box->props.border.left.color);
    }

    // Debug outline
    // DrawRectLine(min_in_pixel, max_in_pixel,
    // ColorU32FromHex(0xFF00FF), 1.0f);

    if (box->first) {
      for (UIBox *child = box->first; child; child = child->next) {
        RenderBox(state, child);
      }
    } else if (!IsEmptyStr8(box->props.text)) {
      DrawTextStr8(
          V2(min.x + box->props.border.left.width + box->props.padding.left,
             min.y + box->props.border.top.width + box->props.padding.top),
          box->props.text, box->computed.font_size, GetFirstNonZeroColor(box));
    }

    if (need_clip) {
      PopClipRect();
    }
  }
}

#if 0
#include <stdlib.h>

#include "src/log.h"
static void DebugPrintUIR(UIBox *box, u32 level) {
  INFO(
      "%*s%s[seq=%u, key=%s, min_size=(%.2f, %.2f), max_size=(%.2f, %.2f), "
      "build_size=(%.2f, %.2f), size=(%.2f, %.2f), rel_pos=(%.2f, %.2f)]",
      level * 4, "", box->tag, box->seq, box->props.key.ptr,
      box->computed.min_size.x, box->computed.min_size.y,
      box->computed.max_size.x, box->computed.max_size.y, box->props.size.x,
      box->props.size.y, box->computed.size.x, box->computed.size.y,
      box->computed.rel_pos.x, box->computed.rel_pos.y);
  for (UIBox *child = box->first; child; child = child->next) {
    DebugPrintUIR(child, level + 1);
  }
}

static void DebugPrintUI(UIState *state) {
  if (state->frame_index > 1) {
    UIFrame *frame = GetCurrentUIFrame(state);
    for (UILayer *layer = frame->last_layer; layer; layer = layer->prev) {
      INFO("Layer - %s", layer->props.key.ptr);
      if (layer->root) {
        DebugPrintUIR(layer->root, 0);
      }
    }
    exit(0);
  }
}
#else
static void DebugPrintUI(UIState *state) { (void)state; }
#endif

static void ProcessInputR(UIState *state, UIBox *box) {
  for (UIBox *child = box->last; child; child = child->prev) {
    ProcessInputR(state, child);
  }

  // Mouse input
  if (IsZeroUIID(state->input.mouse.hovering) && box->hoverable &&
      ContainsVec2(state->input.mouse.pos, box->computed.clip_rect.min,
                   box->computed.clip_rect.max)) {
    state->input.mouse.hovering = box->id;
  }

  for (i32 button = 0; button < kUIMouseButtonCount; ++button) {
    if (IsZeroUIID(state->input.mouse.pressed[button]) &&
        box->clickable[button] &&
        ContainsVec2(state->input.mouse.pos, box->computed.clip_rect.min,
                     box->computed.clip_rect.max) &&
        IsMouseButtonPressed(state, button)) {
      state->input.mouse.pressed[button] = box->id;
      state->input.mouse.pressed_pos[button] = state->input.mouse.pos;
    }
  }

  if (IsZeroUIID(state->input.mouse.scrolling) && box->scrollable &&
      !IsZeroVec2(state->input.mouse.wheel) &&
      ContainsVec2(state->input.mouse.pos, box->computed.clip_rect.min,
                   box->computed.clip_rect.max)) {
    state->input.mouse.scrolling = box->id;
    state->input.mouse.scroll_delta = state->input.mouse.wheel;
  }
}

static void ProcessInput(UIState *state, UIFrame *frame) {
  state->input.mouse.hovering = UIIDZero();
  state->input.mouse.scrolling = UIIDZero();
  for (i32 button = 0; button < kUIMouseButtonCount; ++button) {
    state->input.mouse.pressed[button] = UIIDZero();
    state->input.mouse.clicked[button] = UIIDZero();
  }

  for (UILayer *layer = frame->last_layer; layer; layer = layer->prev) {
    if (layer->root) {
      ProcessInputR(state, layer->root);
    }
  }

  for (i32 button = 0; button < kUIMouseButtonCount; ++button) {
    if (!IsZeroUIID(state->input.mouse.pressed[button])) {
      state->input.mouse.holding[button] = state->input.mouse.pressed[button];
    }

    if (IsMouseButtonClicked(state, button)) {
      UIID id = state->input.mouse.holding[button];
      UIBox *box = GetUIBoxFromFrame(frame, id);
      if (box &&
          ContainsVec2(state->input.mouse.pos, box->computed.clip_rect.min,
                       box->computed.clip_rect.max)) {
        state->input.mouse.clicked[button] = id;
      }
      state->input.mouse.holding[button] = UIIDZero();
    }

    state->input.mouse.buttons[button].transition_count = 0;
  }
  state->input.mouse.wheel = V2(0, 0);
}

static void PositionBox(UIBox *box, Vec2 parent_pos, Rect2 parent_clip_rect) {
  Vec2 min = AddVec2(parent_pos, box->computed.rel_pos);
  Vec2 max = AddVec2(min, box->computed.size);
  box->computed.screen_rect = R2(min, max);
  box->computed.clip_rect =
      Rect2FromIntersection(parent_clip_rect, box->computed.screen_rect);
  for (UIBox *child = box->first; child; child = child->next) {
    PositionBox(child, min, box->computed.clip_rect);
  }
}

void EndUIFrame(void) {
  UIState *state = GetUIState();
  UIFrame *frame = GetCurrentUIFrame(state);
  ASSERTF(!frame->current_layer, "Mismatched BeginLayer/EndLayer calls");

  for (UILayer *layer = frame->first_layer; layer; layer = layer->next) {
    if (layer->root) {
      Vec2 size = state->input.canvas_size;
      LayoutBox(frame, layer->root, size, size);
      layer->root->computed.rel_pos = V2(0, 0);

      PositionBox(layer->root, V2(0, 0), R2(V2(0, 0), size));
    }
  }

  ProcessInput(state, frame);
  DebugPrintUI(state);
}

void RenderUI(void) {
  UIState *state = GetUIState();
  UIFrame *frame = GetCurrentUIFrame(state);

  ASSERTF(!frame->first_error, "%s", frame->first_error->message.ptr);

  for (UILayer *layer = frame->first_layer; layer; layer = layer->next) {
    if (layer->root) {
      RenderBox(state, layer->root);
    }
  }
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

static UIID UIIDFromU8(UIID seed, u8 ch) {
  u8 str[2] = {ch, 0};
  UIID result = UIIDFromStr8(seed, (Str8){.ptr = str, .len = 1});
  return result;
}

void BeginUILayer(UILayerProps props) {
  ASSERTF(!IsEmptyStr8(props.key), "key of a UILayer cannot be empty");

  UIState *state = GetUIState();
  UIFrame *frame = GetCurrentUIFrame(state);

  UILayer *layer = PushArray(&frame->arena, UILayer, 1);

  {
    UILayer *after;
    for (after = frame->last_layer; after; after = after->prev) {
      if (after->props.z_index <= props.z_index) {
        break;
      }
    }
    if (after) {
      INSERT_DOUBLY_LINKED_LIST(frame->first_layer, frame->last_layer, after,
                                layer, prev, next);
    } else {
      PREPEND_DOUBLY_LINKED_LIST(frame->first_layer, frame->last_layer, layer,
                                 prev, next);
    }
  }

  layer->parent = frame->current_layer;
  frame->current_layer = layer;

  layer->id = UIIDFromStr8(UIIDZero(), props.key);
  layer->props = props;
}

void EndUILayer(void) {
  UIState *state = GetUIState();
  UIFrame *frame = GetCurrentUIFrame(state);
  ASSERTF(frame->current_layer, "Mismatched BeginLayer/EndLayer calls");
  ASSERTF(!frame->current_layer->current,
          "Mismatched BeginUITag/EndUITag calls");

  frame->current_layer = frame->current_layer->parent;
}

UIBuildError *GetFirstUIBuildError(void) {
  UIState *state = GetUIState();
  UIFrame *frame = GetCurrentUIFrame(state);
  UIBuildError *result = frame->first_error;
  return result;
}

b32 IsEqualUIID(UIID a, UIID b) {
  b32 result = a.hash == b.hash;
  return result;
}

Str8 PushUIStr8(Str8 str) {
  UIState *state = GetUIState();
  UIFrame *frame = GetCurrentUIFrame(state);
  Str8 result = PushStr8(&frame->arena, str);
  return result;
}

Str8 PushUIStr8F(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  Str8 result = PushUIStr8FV(fmt, ap);
  va_end(ap);
  return result;
}

Str8 PushUIStr8FV(const char *fmt, va_list ap) {
  UIState *state = GetUIState();
  UIFrame *frame = GetCurrentUIFrame(state);
  Str8 result = PushStr8FV(&frame->arena, fmt, ap);
  return result;
}

static UIID UIIDForBox(UIID seed, u32 seq, const char *tag, Str8 key) {
  // id = seed + tag + (key || seq)
  UIID id = seed;
  id = UIIDFromStr8(id, Str8FromCStr(tag));
  if (!IsEmptyStr8(key)) {
    id = UIIDFromStr8(id, key);
  } else {
    u32 s = seq;
    while (s > 0) {
      id = UIIDFromU8(id, (u8)(s & 0xFF));
      s = s >> 8;
    }
  }
  return id;
}

static UIBox *GetUIBoxFromLastFrame(UIState *state, UIID id) {
  UIBox *result = 0;
  UIFrame *last_frame = GetLastUIFrame(state);
  if (last_frame) {
    result = GetUIBoxFromFrame(last_frame, id);
  }
  return result;
}

UIBox *BeginUITag(const char *tag, UIProps props) {
  UIState *state = GetUIState();
  UIFrame *frame = GetCurrentUIFrame(state);

  UILayer *layer = frame->current_layer;
  ASSERTF(layer, "No active UILayer");

  UIBox *parent = layer->current;
  UIID seed;
  if (parent) {
    seed = parent->id;
  } else {
    seed = layer->id;
  }
  u32 seq = 0;
  if (parent) {
    seq = parent->children_count;
  }

  UIID id = UIIDForBox(seed, seq, tag, props.key);
  UIBox *box = PushUIBox(&frame->cache, &frame->arena, id);
  box->tag = tag;
  box->seq = seq;

  if (parent) {
    APPEND_DOUBLY_LINKED_LIST(parent->first, parent->last, box, prev, next);
    ++parent->children_count;
  } else {
    ASSERTF(!layer->root, "More than one root provided");
    layer->root = box;
  }
  box->parent = parent;
  box->props = props;

  // Copy state from last frame, if any
  UIBox *last_box = GetUIBoxFromLastFrame(state, id);
  if (last_box) {
    box->computed = last_box->computed;
  }

  layer->current = box;

  return box;
}

void EndUITag(const char *tag) {
  UIState *state = GetUIState();
  UIFrame *frame = GetCurrentUIFrame(state);
  UILayer *layer = frame->current_layer;
  ASSERTF(layer, "No active UILayer");

  ASSERT(layer->current);
  ASSERTF(strcmp(layer->current->tag, tag) == 0,
          "Mismatched Begin/End calls. Begin with %s, end with %s",
          layer->current->tag, tag);

  layer->current = layer->current->parent;
}

UIBox *GetCurrentUIBox(void) {
  UIState *state = GetUIState();
  UIFrame *frame = GetCurrentUIFrame(state);
  ASSERT(frame->current_layer && frame->current_layer->current);
  UIBox *box = frame->current_layer->current;
  return box;
}

void *PushUIBoxState(UIBox *box, const char *type_name, usize size) {
  UIState *state = GetUIState();
  UIFrame *frame = GetCurrentUIFrame(state);
  ASSERTF(!box->state.ptr, "Can only push once to the UIBox");

  DEBUG_ASSERT(GetUIBoxFromFrame(frame, box->id) == box);
  box->state.type_name = type_name;
  box->state.size = size;

  UIBox *last_box = GetUIBoxFromLastFrame(state, box->id);
  if (last_box && last_box->state.ptr) {
    ASSERTF(last_box->state.size == box->state.size &&
                strcmp(last_box->state.type_name, type_name) == 0,
            "The type pushed to this box (%s) is not the same as the last "
            "frame (%s)",
            type_name, last_box->state.type_name);
    box->state.ptr = PushArena(&frame->arena, size, kPushArenaNoZero);
    // Copy state from last frame
    memcpy(box->state.ptr, last_box->state.ptr, size);
  } else {
    box->state.ptr = PushArena(&frame->arena, size, 0);
  }

  return box->state.ptr;
}

void *GetUIBoxState(UIBox *box, const char *type_name, usize size) {
  ASSERTF(box->state.ptr, "UIBox doesn't have state");
  ASSERTF(
      box->state.size == size && strcmp(box->state.type_name, type_name) == 0,
      "The type currently requested (%s) is not the same as the one pushed "
      "(%s)",
      type_name, box->state.type_name);
  void *result = box->state.ptr;
  return result;
}

Vec2 GetUIMouseRelPos(UIBox *box) {
  UIState *state = GetUIState();
  Vec2 result = V2(0, 0);
  if (box) {
    result = SubVec2(state->input.mouse.pos, box->computed.screen_rect.min);
  }
  return result;
}

Vec2 GetUIMousePos(void) {
  UIState *state = GetUIState();
  Vec2 result = state->input.mouse.pos;
  return result;
}

void SetUIBoxBlockMouseInput(UIBox *box) {
  box->hoverable = 1;
  for (i32 button = 0; button < kUIMouseButtonCount; ++button) {
    box->clickable[button] = 1;
  }
  box->scrollable = 1;
}

b32 IsUIMouseHovering(UIBox *box) {
  UIState *state = GetUIState();
  b32 result = 0;
  if (box) {
    box->hoverable = 1;
    result = IsEqualUIID(state->input.mouse.hovering, box->id);
  }
  return result;
}

b32 IsUIMouseButtonPressed(UIBox *box, UIMouseButton button) {
  UIState *state = GetUIState();
  b32 result = 0;
  if (box) {
    box->clickable[button] = 1;
    result = IsEqualUIID(state->input.mouse.pressed[button], box->id);
  }
  return result;
}

b32 IsUIMouseButtonDown(UIBox *box, UIMouseButton button) {
  UIState *state = GetUIState();
  b32 result = 0;
  if (box) {
    box->clickable[button] = 1;
    result = IsEqualUIID(state->input.mouse.holding[button], box->id);
  }
  return result;
}

b32 IsUIMouseButtonClicked(UIBox *box, UIMouseButton button) {
  UIState *state = GetUIState();
  b32 result = 0;
  if (box) {
    box->clickable[button] = 1;
    result = IsEqualUIID(state->input.mouse.clicked[button], box->id);
  }
  return result;
}

b32 IsUIMouseButtonDragging(UIBox *box, UIMouseButton button, Vec2 *delta) {
  UIState *state = GetUIState();
  b32 result = 0;
  if (box) {
    box->clickable[button] = 1;
    result = IsEqualUIID(state->input.mouse.holding[button], box->id);
    if (result && delta) {
      *delta = SubVec2(state->input.mouse.pos,
                       state->input.mouse.pressed_pos[button]);
    }
  }
  return result;
}

b32 IsUIMouseScrolling(UIBox *box, Vec2 *delta) {
  UIState *state = GetUIState();
  b32 result = 0;
  if (box) {
    box->scrollable = 1;
    result = IsEqualUIID(state->input.mouse.scrolling, box->id);
    if (result && delta) {
      *delta = state->input.mouse.scroll_delta;
    }
  }
  return result;
}
