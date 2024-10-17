#ifndef ZTRACING_SRC_UI_WIDGETS_H_
#define ZTRACING_SRC_UI_WIDGETS_H_

#include "src/assert.h"
#include "src/math.h"
#include "src/string.h"
#include "src/ui.h"

static inline UIKey BeginUIRow(UIProps props) {
  props.main_axis = kAxis2X;
  props.main_axis_size = kUIMainAxisSizeMax;
  props.cross_axis_align = kUICrossAxisAlignCenter;
  return BeginUITag("Row", props);
}

static inline void EndUIRow() { EndUITag("Row"); }

static inline UIKey BeginUIColumn(UIProps props) {
  props.main_axis = kAxis2Y;
  props.main_axis_size = kUIMainAxisSizeMax;
  props.cross_axis_align = kUICrossAxisAlignCenter;
  return BeginUITag("Column", props);
}

static inline void EndUIColumn() { EndUITag("Column"); }

void UITextF(UIProps props, const char *fmt, ...);
void UIText(UIProps props, Str8 text);

UIKey BeginUIScrollable(void);
void EndUIScrollable(void);
f32 GetUIScrollableScroll(UIKey key);
void SetUIScrollableScroll(UIKey key, f32 scroll);

#define kUIDebugLayerZIndex 1000
UIKey UIDebugLayer(void);
void OpenUIDebugLayer(UIKey key);

#endif  // ZTRACING_SRC_UI_WIDGETS_H_
