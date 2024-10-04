#ifndef ZTRACING_SRC_UI_WIDGETS_H_
#define ZTRACING_SRC_UI_WIDGETS_H_

#include "src/assert.h"
#include "src/string.h"
#include "src/ui.h"

static inline void UIText(Str8 text) {
  UIBeginBox(text);
  UISetText(text);
  UIEndBox();
}

static inline void UIBeginCenter(Str8 key) {
  UIBeginBox(key);
  UISetMainAxisAlignment(kUIMainAxisAlignCenter);
  UISetCrossAxisAlignment(kUICrossAxisAlignCenter);
}

static inline void UIEndCenter(void) { UIEndBox(); }

static inline void UIBeginRow(Str8 key) {
  UIBeginBox(key);
  UISetMainAxis(kAxis2X);
}

static inline void UIEndRow() { UIEndBox(); }

static inline void UIBeginColumn(Str8 key) {
  UIBeginBox(key);
  UISetMainAxis(kAxis2Y);
}

static inline void UIEndColumn() { UIEndBox(); }

#endif  // ZTRACING_SRC_UI_WIDGETS_H_
