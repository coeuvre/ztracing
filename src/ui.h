#ifndef ZTRACING_SRC_UI_H_
#define ZTRACING_SRC_UI_H_

#include "src/math.h"
#include "src/string.h"
#include "src/types.h"

void ui_begin_frame(void);
void ui_end_frame(void);

typedef struct UIKey {
  u64 hash;
} UIKey;

static inline UIKey ui_key_zero(void) { return (UIKey){0}; }
static inline bool ui_key_is_zero(UIKey key) { return key.hash == 0; }
static inline bool ui_key_is_equal(UIKey a, UIKey b) {
  return a.hash == b.hash;
}

typedef struct UIWidget UIWidget;

typedef struct UIWidgetTreeLink {
  UIWidget *prev;
  UIWidget *next;
  UIWidget *first;
  UIWidget *last;
  UIWidget *parent;
} UIWidgetTreeLink;

typedef struct UIWidgetHashLink {
  UIWidget *prev;
  UIWidget *next;
} UIWidgetHashLink;

typedef void(UIWidgetRenderFn)(void *widget);

typedef struct UIWidgetVTable {
  UIWidgetRenderFn *render;
} UIWidgetVTable;

struct UIWidget {
  UIWidgetVTable *vtable;
  UIWidgetHashLink hash;
  UIWidgetTreeLink tree;

  UIKey key;
  u32 children_count;
  u32 seq;
  const char *tag;
};

typedef enum UIAxis {
  UI_AXIS_HORIZONTAL,
  UI_AXIS_VERTICAL,
} UIAxis;

typedef enum UIMainAxisSize {
  UI_MAIN_AXIS_SIZE_MAX,
  UI_MAIN_AXIS_SIZE_MIN,
} UIMainAxisSize;

typedef enum UIMainAxisAlignment {
  UI_MAIN_AXIS_ALIGNMENT_START,
  UI_MAIN_AXIS_ALIGNMENT_END,
  UI_MAIN_AXIS_ALIGNMENT_CENTER,
  UI_MAIN_AXIS_ALIGNMENT_SPACE_BETWEEN,
  UI_MAIN_AXIS_ALIGNMENT_SPACE_AROUND,
  UI_MAIN_AXIS_ALIGNMENT_SPACE_EVENLY,
} UIMainAxisAlignment;

typedef enum UICrossAxisAlignment {
  UI_CROSS_AXIS_ALIGNMENT_CENTER,
  UI_CROSS_AXIS_ALIGNMENT_START,
  UI_CROSS_AXIS_ALIGNMENT_END,
  UI_CROSS_AXIS_ALIGNMENT_STRETCH,
  UI_CROSS_AXIS_ALIGNMENT_BASELINE,
} UICrossAxisAlignment;

/// A widget that displays its children in a one-dimensional array.
typedef struct UIFlex {
  UIWidget widget;
  UIAxis direction;
  UIMainAxisAlignment main_axis_alignment;
  UIMainAxisSize main_axis_size;
  UICrossAxisAlignment cross_axis_alignment;
  // TODO: UITextDirection
  // TODO: UIVerticalDirection
  // TODO: UITextBaseline
  f32 spacing;
} UIFlex;

typedef struct UIFlexProps {
  UIKey key;
  UIAxis direction;
  UIMainAxisAlignment main_axis_alignment;
  UIMainAxisSize main_axis_size;
  UICrossAxisAlignment cross_axis_alignment;
  f32 spacing;
} UIFlexProps;

void ui_flex_begin(UIFlexProps props);
void ui_flex_end(void);

/// A widget that displays its children in a vertical array.
typedef struct UIColumn {
  UIFlex flex;
} UIColumn;

typedef struct UIColumnProps {
  UIMainAxisAlignment main_axis_alignment;
  UIMainAxisSize main_axis_size;
  UICrossAxisAlignment cross_axis_alignment;
  f32 spacing;
} UIColumnProps;

void ui_column_begin(UIColumnProps props);
void ui_column_end(void);

#endif  // ZTRACING_SRC_UI_H_
