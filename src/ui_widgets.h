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

UIBox *BeginUIRow(UIRowProps props);
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

UIBox *BeginUIColumn(UIColumnProps props);
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

UIBox *BeginUIStack(UIStackProps props);
static inline void EndUIStack(void) { EndUITag("Stack"); }

void DoUITextF(const char *fmt, ...);
void DoUIText(Str8 text);

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

UIBox *BeginUICollapsing(UICollapsingProps props, b32 *out_open);
void EndUICollapsing(void);
b32 IsUICollapsingOpen(UIBox *box);

UIBox *BeginUIScrollable(void);
void EndUIScrollable(void);
f32 GetUIScrollableScroll(UIBox *scrollable);
void SetUIScrollableScroll(UIBox *scrollable, f32 scroll);

#define kUIDebugLayerZIndex 1000
UIBox *UIDebugLayer(void);
void OpenUIDebugLayer(UIBox *box);

#endif  // ZTRACING_SRC_UI_WIDGETS_H_
