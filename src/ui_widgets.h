#ifndef ZTRACING_SRC_UI_WIDGETS_H_
#define ZTRACING_SRC_UI_WIDGETS_H_

#include "src/assert.h"
#include "src/string.h"
#include "src/ui.h"

static inline void BeginUIRow() {
  BeginUIBox();
  SetUIMainAxis(kAxis2X);
  SetUIMainAxisSize(kUIMainAxisSizeMax);
  SetUICrossAxisAlign(kUICrossAxisAlignCenter);
}

static inline void EndUIRow() { EndUIBox(); }

static inline void BeginUIColumn() {
  BeginUIBox();
  SetUIMainAxis(kAxis2Y);
  SetUIMainAxisSize(kUIMainAxisSizeMax);
  SetUICrossAxisAlign(kUICrossAxisAlignCenter);
}

static inline void EndUIColumn() { EndUIBox(); }

#endif  // ZTRACING_SRC_UI_WIDGETS_H_
