#ifndef ZTRACING_SRC_UI_WIDGETS_H_
#define ZTRACING_SRC_UI_WIDGETS_H_

#include "src/assert.h"
#include "src/string.h"
#include "src/ui.h"

static inline void BeginUICenter(Str8 key) {
  BeginUIBox(key);
  SetUIMainAxisAlign(kUIMainAxisAlignCenter);
  SetUICrossAxisAlign(kUICrossAxisAlignCenter);
}

static inline void EndUICenter(void) { EndUIBox(); }

static inline void BeginUIRow(Str8 key) {
  BeginUIBox(key);
  SetUIMainAxis(kAxis2X);
}

static inline void EndUIRow() { EndUIBox(); }

static inline void BeginUIColumn(Str8 key) {
  BeginUIBox(key);
  SetUIMainAxis(kAxis2Y);
}

static inline void EndUIColumn() { EndUIBox(); }

#endif  // ZTRACING_SRC_UI_WIDGETS_H_
