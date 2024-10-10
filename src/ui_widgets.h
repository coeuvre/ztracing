#ifndef ZTRACING_SRC_UI_WIDGETS_H_
#define ZTRACING_SRC_UI_WIDGETS_H_

#include "src/assert.h"
#include "src/string.h"
#include "src/ui.h"

static inline void BeginUIRow() {
  SetNextUITag("Row");
  SetNextUIMainAxis(kAxis2X);
  SetNextUIMainAxisSize(kUIMainAxisSizeMax);
  SetNextUICrossAxisAlign(kUICrossAxisAlignCenter);
  BeginUIBox();
}

static inline void EndUIRow() { EndUIBoxWithExpectedTag("Row"); }

static inline void BeginUIColumn() {
  SetNextUITag("Column");
  SetNextUIMainAxis(kAxis2Y);
  SetNextUIMainAxisSize(kUIMainAxisSizeMax);
  SetNextUICrossAxisAlign(kUICrossAxisAlignCenter);
  BeginUIBox();
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

void BeginUIScrollable(UIScrollableState *state);
void EndUIScrollable(UIScrollableState *state);

#endif  // ZTRACING_SRC_UI_WIDGETS_H_
