#ifndef ZTRACING_SRC_UI_WIDGETS_H_
#define ZTRACING_SRC_UI_WIDGETS_H_

#include "src/assert.h"
#include "src/string.h"
#include "src/ui.h"

static inline void BeginUIRow() {
  BeginUIBoxWithTag("Row");
  SetUIMainAxis(kAxis2X);
  SetUIMainAxisSize(kUIMainAxisSizeMax);
  SetUICrossAxisAlign(kUICrossAxisAlignCenter);
}

static inline void EndUIRow() { EndUIBoxWithTag("Row"); }

static inline void BeginUIColumn() {
  BeginUIBoxWithTag("Column");
  SetUIMainAxis(kAxis2Y);
  SetUIMainAxisSize(kUIMainAxisSizeMax);
  SetUICrossAxisAlign(kUICrossAxisAlignCenter);
}

static inline void EndUIColumn() { EndUIBoxWithTag("Column"); }

#endif  // ZTRACING_SRC_UI_WIDGETS_H_
