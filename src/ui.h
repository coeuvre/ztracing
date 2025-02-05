#ifndef ZTRACING_SRC_UI_H_
#define ZTRACING_SRC_UI_H_

#include "src/assert.h"
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

static inline UIBoxConstraints ui_box_constraints_loosen(
    UIBoxConstraints constraints) {
  return (UIBoxConstraints){
      .min_width = 0,
      .max_width = constraints.max_width,
      .min_height = 0,
      .max_height = constraints.max_height,
  };
}

typedef struct UIColor {
  f32 r;
  f32 g;
  f32 b;
  f32 a;
} UIColor;

typedef struct UIPaintingContext {
  int placeholder;
} UIPaintingContext;

enum {
  UI_WIDGET_MESSAGE_LAYOUT = 1,
  UI_WIDGET_MESSAGE_PAINT,
  UI_WIDGET_MESSAGE_GET_PARENT_DATA,
};

typedef struct UIWidgetMessage {
  u32 type;
} UIWidgetMessage;

typedef struct UIWidgetMessageLayout {
  u32 type;  // UI_WIDGET_MESSAGE_LAYOUT
  UIBoxConstraints constraints;
} UIWidgetMessageLayout;

typedef struct UIWidgetMessagePaint {
  u32 type;  // UI_WIDGET_MESSAGE_PAINT
  UIPaintingContext *context;
  Vec2 offset;
} UIWidgetMessagePaint;

enum {
  UI_WIDGET_PARENT_DATA_FLEX = 1,
};

typedef struct UIWidgetMessageGetParentData {
  u32 type;  // UI_WIDGET_MESSAGE_GET_PARENT_DATA
  u32 parent_data_id;
  void *parent_data;
} UIWidgetMessageGetParentData;

typedef i32(UIWidgetCallback)(UIWidget *widget, UIWidgetMessage *message);

typedef struct UIWidgetClass {
  const char *name;
  usize props_size;
  UIWidgetCallback *callback;
} UIWidgetClass;

struct UIWidget {
  UIWidgetClass *klass;
  UIWidgetHashLink hash;
  UIWidgetTreeLink tree;

  u32 child_count;
  u32 seq;

  /// The size of this box computed during layout.
  Vec2 size;
  /// The offset at which to paint the child in the parent's coordinate system.
  Vec2 offset;

  void *state;
};

void ui_widget_begin_(UIWidgetClass *klass, usize props_size, void *props);
#define ui_widget_begin(klass, props) \
  ui_widget_begin_(klass, sizeof(props), &props)
void ui_widget_end(UIWidgetClass *klass);

static inline UIKey ui_widget_get_key(UIWidget *widget) {
  DEBUG_ASSERT(widget->klass->props_size >= sizeof(UIKey));
  UIKey *key_ptr = (UIKey *)(widget + 1);
  return *key_ptr;
}

static inline void *ui_widget_get_props_(UIWidget *widget, usize props_size) {
  DEBUG_ASSERT(widget->klass->props_size == props_size);
  return widget + 1;
}

#define ui_widget_get_props(widget, Props) \
  ((Props *)ui_widget_get_props_(widget, sizeof(Props)))

////////////////////////////////////////////////////////////////////////////////
///
/// UILimitedBox
///
/// A box that limits its size only when it's unconstrained.
///
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
typedef struct UIConstrainedBoxProps {
  UIKey key;
  UIBoxConstraints additional_constraints;
} UIConstrainedBoxProps;

void ui_constrained_box_begin(UIConstrainedBoxProps props);
void ui_constrained_box_end(void);

////////////////////////////////////////////////////////////////////////////////
///
/// UIAlign
///
/// A widget that aligns its child within itself and optionally sizes itself
/// based on the child's size.

/// A point within a rectangle.
///
/// `Alignment(0.0, 0.0)` represents the center of the rectangle. The distance
/// from -1.0 to +1.0 is the distance from one side of the rectangle to the
/// other side of the rectangle. Therefore, 2.0 units horizontally (or
/// vertically) is equivalent to the width (or height) of the rectangle.
///
/// `Alignment(-1.0, -1.0)` represents the top left of the rectangle.
///
/// `Alignment(1.0, 1.0)` represents the bottom right of the rectangle.
///
/// `Alignment(0.0, 3.0)` represents a point that is horizontally centered with
/// respect to the rectangle and vertically below the bottom of the rectangle by
/// the height of the rectangle.
///
/// `Alignment(0.0, -0.5)` represents a point that is horizontally centered with
/// respect to the rectangle and vertically half way between the top edge and
/// the center.
///
/// `Alignment(x, y)` in a rectangle with height h and width w describes
/// the point (x * w/2 + w/2, y * h/2 + h/2) in the coordinate system of the
/// rectangle.
typedef struct UIAlignment {
  f32 x;
  f32 y;
} UIAlignment;

static inline Vec2 ui_alignment_align_offset(UIAlignment alignment,
                                             Vec2 offset) {
  f32 center_x = offset.x / 2.0f;
  f32 center_y = offset.y / 2.0f;
  return v2(center_x + alignment.x * center_x,
            center_y + alignment.y * center_y);
}

static inline UIAlignment ui_alignment_top_left(void) {
  return (UIAlignment){
      .x = -1.0f,
      .y = -1.0f,
  };
}

static inline UIAlignment ui_alignment_top_center(void) {
  return (UIAlignment){
      .x = 0.0f,
      .y = -1.0f,
  };
}

static inline UIAlignment ui_alignment_top_right(void) {
  return (UIAlignment){
      .x = 0.0f,
      .y = 1.0f,
  };
}

static inline UIAlignment ui_alignment_center_left(void) {
  return (UIAlignment){
      .x = -1.0f,
      .y = 0.0f,
  };
}

static inline UIAlignment ui_alignment_center(void) {
  return (UIAlignment){
      .x = 0.0f,
      .y = 0.0f,
  };
}

static inline UIAlignment ui_alignment_center_right(void) {
  return (UIAlignment){
      .x = 1.0f,
      .y = 0.0f,
  };
}

static inline UIAlignment ui_alignment_bottom_left(void) {
  return (UIAlignment){
      .x = -1.0f,
      .y = 1.0f,
  };
}

static inline UIAlignment ui_alignment_bottom_center(void) {
  return (UIAlignment){
      .x = 0.0f,
      .y = 1.0f,
  };
}

static inline UIAlignment ui_alignment_bottom_right(void) {
  return (UIAlignment){
      .x = 1.0f,
      .y = 1.0f,
  };
}

typedef struct UIAlignProps {
  UIKey key;
  UIAlignment alignment;
  bool has_width_factor;
  bool has_height_factor;
  f32 width_factor;
  f32 height_factor;
} UIAlignProps;

void ui_align_begin(UIAlignProps props);
void ui_align_end(void);

////////////////////////////////////////////////////////////////////////////////
///
/// UICenter
///
/// A widget that centers its child within itself.
typedef struct UICenterProps {
  UIKey key;
  bool has_width_factor;
  bool has_height_factor;
  f32 width_factor;
  f32 height_factor;
} UICenterProps;

void ui_center_begin(UICenterProps props);
void ui_center_end(void);

////////////////////////////////////////////////////////////////////////////////
///
/// UIFlexible
///
/// A widget that controls how a child of a UIRow, UIColumn, or UIFlex flexes.
///
typedef enum UIFlexFit {
  /// The child is forced to fill the available space.
  UI_FLEX_FIT_TIGHT,
  /// The child can be at most as large as the available space (but is allowed
  /// to be smaller).
  UI_FLEX_FIT_LOOSE,
} UIFlexFit;

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
typedef struct UIWidgetParentDataFlex {
  i32 flex;
  UIFlexFit fit;
} UIWidgetParentDataFlex;

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

typedef struct UIFlexProps {
  UIKey key;
  UIAxis direction;
  UIMainAxisAlignment main_axis_alignment;
  UIMainAxisSize main_axis_size;
  UICrossAxisAlignment cross_axis_alignment;
  // TODO: UITextDirection
  // TODO: UIVerticalDirection
  // TODO: UITextBaseline
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
