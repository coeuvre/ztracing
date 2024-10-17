#ifndef ZTRACING_SRC_UI_WIDGETS_H_
#define ZTRACING_SRC_UI_WIDGETS_H_

#include "src/assert.h"
#include "src/math.h"
#include "src/string.h"
#include "src/ui.h"

typedef struct UIRowProps {
  Str8 key;
  Vec2 size;
  UIEdgeInsets padding;
  UIEdgeInsets margin;
  ColorU32 background_color;
  UIMainAxisAlign main_axis_align;
  UICrossAxisAlign cross_axis_align;
} UIRowProps;

UIKey BeginUIRow(UIRowProps props);
static inline void EndUIRow(void) { EndUITag("Row"); }

typedef struct UIColumnProps {
  Str8 key;
  Vec2 size;
  UIEdgeInsets padding;
  UIEdgeInsets margin;
  ColorU32 background_color;
  UIMainAxisAlign main_axis_align;
  UICrossAxisAlign cross_axis_align;
} UIColumnProps;

UIKey BeginUIColumn(UIColumnProps props);
static inline void EndUIColumn(void) { EndUITag("Column"); }

typedef struct UIStackProps {
  Str8 key;
  Vec2 size;
  UIEdgeInsets padding;
  UIEdgeInsets margin;
  ColorU32 background_color;
  UIMainAxisAlign main_axis_align;
  UICrossAxisAlign cross_axis_align;
} UIStackProps;

UIKey BeginUIStack(UIStackProps props);
static inline void EndUIStack(void) { EndUITag("Stack"); }

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
