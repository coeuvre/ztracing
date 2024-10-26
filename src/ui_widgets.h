#ifndef ZTRACING_SRC_UI_WIDGETS_H_
#define ZTRACING_SRC_UI_WIDGETS_H_

#include <stdbool.h>

#include "src/assert.h"
#include "src/math.h"
#include "src/string.h"
#include "src/ui.h"

typedef struct UIRowProps {
  Str8 key;
  Vec2 size;
  UIEdgeInsets padding;
  UIEdgeInsets margin;
  UIBorder border;
  ColorU32 color;
  ColorU32 background_color;
  UIMainAxisAlign main_axis_align;
  UICrossAxisAlign cross_axis_align;
} UIRowProps;

void BeginUIRow(UIRowProps props);
static inline void EndUIRow(void) { EndUITag("Row"); }

typedef struct UIColumnProps {
  Str8 key;
  Vec2 size;
  UIEdgeInsets padding;
  UIEdgeInsets margin;
  UIBorder border;
  ColorU32 color;
  ColorU32 background_color;
  UIMainAxisAlign main_axis_align;
  UICrossAxisAlign cross_axis_align;
} UIColumnProps;

void BeginUIColumn(UIColumnProps props);
static inline void EndUIColumn(void) { EndUITag("Column"); }

typedef struct UIStackProps {
  Str8 key;
  Vec2 size;
  UIEdgeInsets padding;
  UIEdgeInsets margin;
  UIBorder border;
  ColorU32 color;
  ColorU32 background_color;
  UIMainAxisAlign main_axis_align;
  UICrossAxisAlign cross_axis_align;
} UIStackProps;
void BeginUIStack(UIStackProps props);
static inline void EndUIStack(void) { EndUITag("Stack"); }

typedef struct UITextProps {
  Str8 key;
  Vec2 size;
  Str8 text;
  UIEdgeInsets padding;
  UIEdgeInsets margin;
  UIBorder border;
  ColorU32 color;
  ColorU32 background_color;
} UITextProps;

void DoUIText(UITextProps props);

typedef struct UIButtonProps {
  Vec2 size;
  Str8 text;
  UIEdgeInsets padding;

  bool default_background_color;
  bool *hoverred;
} UIButtonProps;

bool BeginUIButton(UIButtonProps props);
void EndUIButton(void);

static inline bool DoUIButton(UIButtonProps props) {
  bool clicked = BeginUIButton(props);
  EndUIButton();
  return clicked;
}

typedef struct UICollapsingHeaderProps {
  Str8 text;
  UIEdgeInsets padding;

  bool *hoverred;
} UICollapsingHeaderProps;

typedef struct UICollapsingProps {
  bool default_open;
  bool default_background_color;
  bool disabled;
  UICollapsingHeaderProps header;
} UICollapsingProps;

bool BeginUICollapsing(UICollapsingProps props);
void EndUICollapsing(void);

typedef struct UIScrollableProps {
  // Scroll position, optional
  f32 *scroll;
} UIScrollableProps;

void BeginUIScrollable(UIScrollableProps props);
void EndUIScrollable(void);

#define kUIDebugLayerZIndex 1000
typedef struct UIDebugLayerProps {
  bool *open;
} UIDebugLayerProps;
void DoUIDebugLayer(UIDebugLayerProps props);

#endif  // ZTRACING_SRC_UI_WIDGETS_H_
