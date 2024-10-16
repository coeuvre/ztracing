#ifndef ZTRACING_SRC_UI_WIDGETS_H_
#define ZTRACING_SRC_UI_WIDGETS_H_

#include "src/assert.h"
#include "src/math.h"
#include "src/string.h"
#include "src/ui.h"

static inline void BeginUIRow(UIProps props) {
  props.main_axis = kAxis2X;
  props.main_axis_size = kUIMainAxisSizeMax;
  props.cross_axis_align = kUICrossAxisAlignCenter;
  BeginUITag("Row", props);
}

static inline void EndUIRow() { EndUITag("Row"); }

static inline void BeginUIColumn(UIProps props) {
  props.main_axis = kAxis2Y;
  props.main_axis_size = kUIMainAxisSizeMax;
  props.cross_axis_align = kUICrossAxisAlignCenter;
  BeginUITag("Column", props);
}

static inline void EndUIColumn() { EndUITag("Column"); }

void UITextF(UIProps props, const char *fmt, ...);
void UIText(UIProps props, Str8 text);

typedef struct UIScrollableState {
  // persistent info
  f32 scroll;
  f32 control_offset_drag_start;

  // per-frame info
  f32 scroll_area_size;
  f32 scroll_max;
  Vec2 head_size;
  f32 scroll_step;
  f32 control_max;
  f32 control_offset;
  f32 control_size;
} UIScrollableState;

void BeginUIScrollable(UIScrollableState *state);
void EndUIScrollable(UIScrollableState *state);
f32 GetUIScrollableScroll(UIScrollableState *state);
void SetUIScrollableScroll(UIScrollableState *state, f32 scroll);

typedef struct UIDebugLayerState {
  UIScrollableState scrollable;

  b8 open;
  Vec2 min;
  Vec2 max;
  Vec2 pressed_min;
  Vec2 pressed_max;

  Rect2 hovered_clip_rect;
} UIDebugLayerState;

void UIDebugLayer(UIDebugLayerState *state);

#endif  // ZTRACING_SRC_UI_WIDGETS_H_
