#ifndef ZTRACING_SRC_UI_H_
#define ZTRACING_SRC_UI_H_

#include "src/math.h"
#include "src/string.h"
#include "src/types.h"

void ui_set_viewport(Vec2 min, Vec2 max);

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

typedef struct UIBoxConstraints {
  f32 min_width;
  f32 max_width;
  f32 min_height;
  f32 max_height;
} UIBoxConstraints;

/// Creates box constraints that is respected only by the given size.
static inline UIBoxConstraints ui_box_constraints_make_tight(f32 width,
                                                             f32 height) {
  return (UIBoxConstraints){
      .min_width = width,
      .max_width = width,
      .min_height = height,
      .max_height = height,
  };
}

/// Creates box constraints that require the given width.
static inline UIBoxConstraints ui_box_constraints_make_tight_width(f32 width) {
  return (UIBoxConstraints){
      .min_width = width,
      .max_width = width,
      .min_height = 0,
      .max_height = F32_INFINITY,
  };
}

/// Creates box constraints that require the given height.
static inline UIBoxConstraints ui_box_constraints_make_tight_height(
    f32 height) {
  return (UIBoxConstraints){
      .min_width = 0,
      .max_width = F32_INFINITY,
      .min_height = height,
      .max_height = height,
  };
}

/// Returns the width that both satisfies the constraints and is as close as
/// possible to the given width.
static inline f32 ui_box_constraints_constrain_width(
    UIBoxConstraints constraints, f32 width) {
  return f32_clamp(width, constraints.min_width, constraints.max_width);
}

/// Returns the height that both satisfies the constraints and is as close as
/// possible to the given height.
static inline f32 ui_box_constraints_constrain_height(
    UIBoxConstraints constraints, f32 height) {
  return f32_clamp(height, constraints.min_height, constraints.max_height);
}

/// Returns the size that both satisfies the constraints and is as close as
/// possible to the given size.
static inline Vec2 ui_box_constraints_constrain(UIBoxConstraints constraints,
                                                Vec2 size) {
  return v2(ui_box_constraints_constrain_width(constraints, size.x),
            ui_box_constraints_constrain_height(constraints, size.y));
}

/// The biggest size that satisfies the constraints.
static inline Vec2 ui_box_constraints_get_biggest(
    UIBoxConstraints constraints) {
  return v2(ui_box_constraints_constrain_width(constraints, F32_INFINITY),
            ui_box_constraints_constrain_height(constraints, F32_INFINITY));
}

static inline UIBoxConstraints ui_box_constraints_flip(
    UIBoxConstraints constraints) {
  return (UIBoxConstraints){
      .min_width = constraints.min_height,
      .max_width = constraints.max_height,
      .min_height = constraints.min_width,
      .max_height = constraints.max_width,
  };
}

static inline bool ui_box_constraints_has_bounded_width(
    UIBoxConstraints constraints) {
  return constraints.max_width < F32_INFINITY;
}

static inline bool ui_box_constraints_has_bounded_height(
    UIBoxConstraints constraints) {
  return constraints.max_height < F32_INFINITY;
}

/// Returns new box constraints that respect the given constraints while being
/// as close as possible to the original constraints.
static inline UIBoxConstraints ui_box_constraints_enforce(
    UIBoxConstraints self, UIBoxConstraints constraints) {
  return (UIBoxConstraints){
      .min_width = f32_clamp(self.min_width, constraints.min_width,
                             constraints.max_width),
      .max_width = f32_clamp(self.max_width, constraints.min_width,
                             constraints.max_width),
      .min_height = f32_clamp(self.min_height, constraints.min_height,
                              constraints.max_height),
      .max_height = f32_clamp(self.max_height, constraints.min_height,
                              constraints.max_height),
  };
}

typedef struct UIPaintingContext {
  int placeholder;
} UIPaintingContext;

typedef enum UIFlexFit {
  /// The child is forced to fill the available space.
  UI_FLEX_FIT_TIGHT,
  /// The child can be at most as large as the available space (but is allowed
  /// to be smaller).
  UI_FLEX_FIT_LOOSE,
} UIFlexFit;

typedef void(UIWidgetLayoutFn)(void *widget, UIBoxConstraints constraints);
typedef void(UIWidgetPaintFn)(void *widget, UIPaintingContext *context,
                              Vec2 offset);

typedef struct UIWidgetVTable UIWidgetVTable;
struct UIWidgetVTable {
  UIWidgetVTable *parent;
  const char *name;
  usize size;
  UIWidgetLayoutFn *layout;
  UIWidgetPaintFn *paint;
};

extern UIWidgetVTable ui_widget_vtable;
struct UIWidget {
  UIWidgetVTable *vtable;
  UIWidgetHashLink hash;
  UIWidgetTreeLink tree;

  UIKey key;
  u32 child_count;
  u32 seq;

  // TODO: SliverWidget?

  /// The size of this box computed during layout.
  Vec2 size;
  /// The offset at which to paint the child in the parent's coordinate system.
  Vec2 offset;
};

////////////////////////////////////////////////////////////////////////////////
///
/// UILimitedBox
///
/// A box that limits its size only when it's unconstrained.
///
extern UIWidgetVTable ui_limited_box_vtable;
typedef struct UILimitedBox {
  UIWidget widget;
  f32 max_width;
  f32 max_height;
} UILimitedBox;

typedef struct UILimitedBoxProps {
  UIKey key;
  f32 max_width;
  f32 max_height;
} UILimitedBoxProps;

void ui_limited_box_begin(UILimitedBoxProps props);
void ui_limited_box_end(void);

////////////////////////////////////////////////////////////////////////////////
///
/// UIColoredBox
///
/// A widget that paints its area with a specified color and then draws its
/// child on top of that color.
///
typedef struct UIColor {
  f32 r;
  f32 g;
  f32 b;
  f32 a;
} UIColor;

extern UIWidgetVTable ui_colored_box_vtable;
typedef struct UIColoredBox {
  UIWidget widget;
  UIColor color;
} UIColoredBox;

typedef struct UIColoredBoxProps {
  UIKey key;
  UIColor color;
} UIColoredBoxProps;

void ui_colored_box_begin(UIColoredBoxProps props);
void ui_colored_box_end(void);

////////////////////////////////////////////////////////////////////////////////
///
/// UIConstrainedBox
///
/// A widget that imposes additional constraints on its child.
extern UIWidgetVTable ui_constrained_box_vtable;
typedef struct UIConstrainedBox {
  UIWidget widget;
  UIBoxConstraints additional_constraints;
} UIConstrainedBox;

typedef struct UIConstrainedBoxProps {
  UIKey key;
  UIBoxConstraints additional_constraints;
} UIConstrainedBoxProps;

void ui_constrained_box_begin(UIConstrainedBoxProps props);
void ui_constrained_box_end(void);

////////////////////////////////////////////////////////////////////////////////
///
/// UIFlexible
///
/// A widget that controls how a child of a UIRow, UIColumn, or UIFlex flexes.
///
extern UIWidgetVTable ui_flexible_vtable;
typedef struct UIFlexible {
  UIWidget widget;
  i32 flex;
  UIFlexFit fit;
} UIFlexible;

typedef struct UIFlexibleProps {
  UIKey key;
  i32 flex;
  UIFlexFit fit;
} UIFlexibleProps;

void ui_flexible_begin(UIFlexibleProps props);
void ui_flexible_end(void);

////////////////////////////////////////////////////////////////////////////////
///
/// UIFlex
///
/// A widget that displays its children in a one-dimensional array.
///
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

extern UIWidgetVTable ui_flex_vtable;
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

////////////////////////////////////////////////////////////////////////////////
///
/// UIColumn
///
/// A widget that displays its children in a vertical array.
///
extern UIWidgetVTable ui_column_vtable;
typedef struct UIColumn {
  UIFlex flex;
} UIColumn;

typedef struct UIColumnProps {
  UIKey key;
  UIMainAxisAlignment main_axis_alignment;
  UIMainAxisSize main_axis_size;
  UICrossAxisAlignment cross_axis_alignment;
  f32 spacing;
} UIColumnProps;

void ui_column_begin(UIColumnProps props);
void ui_column_end(void);

////////////////////////////////////////////////////////////////////////////////
///
/// UIRow
///
/// A widget that displays its children in a vertical array.
///
extern UIWidgetVTable ui_row_vtable;
typedef struct UIRow {
  UIFlex flex;
} UIRow;

typedef struct UIRowProps {
  UIKey key;
  UIMainAxisAlignment main_axis_alignment;
  UIMainAxisSize main_axis_size;
  UICrossAxisAlignment cross_axis_alignment;
  f32 spacing;
} UIRowProps;

void ui_row_begin(UIRowProps props);
void ui_row_end(void);

#endif  // ZTRACING_SRC_UI_H_
