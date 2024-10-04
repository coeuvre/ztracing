#ifndef ZTRACING_SRC_UI_WIDGETS_H_
#define ZTRACING_SRC_UI_WIDGETS_H_

#include "src/assert.h"
#include "src/string.h"
#include "src/ui.h"

static inline void Text(Str8 text) {
  BeginWidget(text);
  SetWidgetText(text);
  EndWidget();
}

static inline void BeginContainer(Str8 key) { BeginWidget(key); }

static inline void EndContainer(void) { EndWidget(); }

static inline void BeginCenter(Str8 key) {
  BeginWidget(key);
  UISetMainAxisAlignment(kUIAlignCenter);
  UISetCrossAxisAlignment(kUIAlignCenter);
}

static inline void EndCenter(void) { EndWidget(); }

static inline void BeginRow(Str8 key) {
  BeginWidget(key);
  UISetMainAxis(kAxis2X);
}

static inline void EndRow() { EndWidget(); }

static inline void BeginColumn(Str8 key) {
  BeginWidget(key);
  UISetMainAxis(kAxis2Y);
}

static inline void EndColumn() { EndWidget(); }

#endif  // ZTRACING_SRC_UI_WIDGETS_H_
