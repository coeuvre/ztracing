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

UIID BeginUIRow(UIRowProps props);
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

UIID BeginUIColumn(UIColumnProps props);
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

UIID BeginUIStack(UIStackProps props);
static inline void EndUIStack(void) { EndUITag("Stack"); }

void UITextF(UIProps props, const char *fmt, ...);
void UIText(UIProps props, Str8 text);

typedef struct UICollapsingHeaderProps {
  Str8 text;
  UIEdgeInsets padding;
} UICollapsingHeaderProps;

typedef struct UICollapsingProps {
  b8 default_open;
  b8 default_background_color;
  b8 disabled;
  UICollapsingHeaderProps header;
} UICollapsingProps;

UIID BeginUICollapsing(UICollapsingProps props, b32 *out_open);
void EndUICollapsing(void);
b32 IsUICollapsingOpen(UIID id);

UIID BeginUIScrollable(void);
void EndUIScrollable(void);
f32 GetUIScrollableScroll(UIID id);
void SetUIScrollableScroll(UIID id, f32 scroll);

#define kUIDebugLayerZIndex 1000
UIID UIDebugLayer(void);
void OpenUIDebugLayer(UIID id);

#endif  // ZTRACING_SRC_UI_WIDGETS_H_
