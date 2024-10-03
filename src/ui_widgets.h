#ifndef ZTRACING_SRC_UI_WIDGETS_H_
#define ZTRACING_SRC_UI_WIDGETS_H_

#include "src/assert.h"
#include "src/string.h"
#include "src/ui.h"

static inline void Text(Str8 text) {
  BeginWidget(text, kWidgetText);
  SetWidgetText(text);
  EndWidget();
}

static inline void BeginContainer(Str8 key) {
  BeginWidget(key, kWidgetContainer);
}

static inline void EndContainer(void) {
  ASSERT(GetWidgetType() == kWidgetContainer);
  EndWidget();
}

static inline void BeginCenter(Str8 key) { BeginWidget(key, kWidgetCenter); }

static inline void EndCenter(void) {
  ASSERT(GetWidgetType() == kWidgetCenter);
  EndWidget();
}

#endif  // ZTRACING_SRC_UI_WIDGETS_H_
