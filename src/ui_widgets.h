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
  UIMainAxisAlignment main_axis_align;
  UICrossAxisAlignment cross_axis_align;
} UIRowProps;

void ui_row_begin(UIRowProps props);
static inline void ui_row_end(void) { ui_tag_end("Row"); }

typedef struct UIColumnProps {
  Str8 key;
  Vec2 size;
  UIEdgeInsets padding;
  UIEdgeInsets margin;
  UIBorder border;
  ColorU32 color;
  ColorU32 background_color;
  UIMainAxisAlignment main_axis_align;
  UICrossAxisAlignment cross_axis_align;
} UIColumnProps;

void ui_column_begin(UIColumnProps props);
static inline void ui_column_end(void) { ui_tag_end("Column"); }

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

void ui_text(UITextProps props);

typedef struct UIButtonProps {
  Vec2 size;
  UIPosition position;
  UIEdgeInsets offset;
  UIEdgeInsets padding;
  Str8 text;

  bool default_background_color;
  bool *hoverred;
} UIButtonProps;

bool ui_button_begin(UIButtonProps props);
void ui_button_end(void);

static inline bool DoUIButton(UIButtonProps props) {
  bool clicked = ui_button_begin(props);
  ui_button_end();
  return clicked;
}

typedef struct UICollapsingHeaderProps {
  Str8 text;
  UIEdgeInsets padding;

  bool default_background_color;
  bool *hoverred;
} UICollapsingHeaderProps;

typedef struct UICollapsingProps {
  bool *open;

  UICollapsingHeaderProps header;
} UICollapsingProps;

bool ui_collapsing_begin(UICollapsingProps props);
void ui_collapsing_end(void);

typedef struct UIScrollableProps {
  // Scroll position, optional
  f32 *scroll;
} UIScrollableProps;

void ui_scrollable_begin(UIScrollableProps props);
void ui_scrollable_end(void);

#define kUIDebugLayerZIndex 1000
typedef struct UIDebugLayerProps {
  Arena *arena;
  bool *open;
} UIDebugLayerProps;
void ui_debug_layer(UIDebugLayerProps props);

#endif  // ZTRACING_SRC_UI_WIDGETS_H_
