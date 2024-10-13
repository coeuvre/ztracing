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
  BeginUIBoxWithTag("Row", props);
}

static inline void EndUIRow() { EndUIBoxWithExpectedTag("Row"); }

static inline void BeginUIColumn(UIProps props) {
  props.main_axis = kAxis2Y;
  props.main_axis_size = kUIMainAxisSizeMax;
  props.cross_axis_align = kUICrossAxisAlignCenter;
  BeginUIBoxWithTag("Column", props);
}

static inline void EndUIColumn() { EndUIBoxWithExpectedTag("Column"); }

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

void BeginUIScrollable(UIProps props, UIScrollableState *state);
void EndUIScrollable(UIScrollableState *state);
f32 GetUIScrollableScroll(UIScrollableState *state);
void SetUIScrollableScroll(UIScrollableState *state, f32 scroll);

#endif  // ZTRACING_SRC_UI_WIDGETS_H_
