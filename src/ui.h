#ifndef ZTRACING_SRC_UI_H_
#define ZTRACING_SRC_UI_H_

#include <stdbool.h>

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

typedef struct UIBoxConstraints {
  bool present;
  f32 min_width;
  f32 max_width;
  f32 min_height;
  f32 max_height;
} UIBoxConstraints;

static inline UIBoxConstraints ui_box_constraints_make(f32 min_width,
                                                       f32 max_width,
                                                       f32 min_height,
                                                       f32 max_height) {
  UIBoxConstraints result;
  result.present = true;
  result.min_width = min_width;
  result.max_width = max_width;
  result.min_height = min_height;
  result.max_height = max_height;
  return result;
}

/// Creates box constraints that is respected only by the given size.
static inline UIBoxConstraints ui_box_constraints_make_tight(f32 width,
                                                             f32 height) {
  return ui_box_constraints_make(width, width, height, height);
}

/// Creates box constraints that require the given width.
static inline UIBoxConstraints ui_box_constraints_make_tight_width(f32 width) {
  return ui_box_constraints_make(width, width, 0, F32_INFINITY);
}

/// Creates box constraints that require the given height.
static inline UIBoxConstraints ui_box_constraints_make_tight_height(
    f32 height) {
  return ui_box_constraints_make(0, F32_INFINITY, height, height);
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
  return ui_box_constraints_make(constraints.min_height, constraints.max_height,
                                 constraints.min_width, constraints.max_width);
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
  return ui_box_constraints_make(
      f32_clamp(self.min_width, constraints.min_width, constraints.max_width),
      f32_clamp(self.max_width, constraints.min_width, constraints.max_width),
      f32_clamp(self.min_height, constraints.min_height,
                constraints.max_height),
      f32_clamp(self.max_height, constraints.min_height,
                constraints.max_height));
}

static inline UIBoxConstraints ui_box_constraints_loosen(
    UIBoxConstraints constraints) {
  return ui_box_constraints_make(0, constraints.max_width, 0,
                                 constraints.max_height);
}

static inline bool ui_box_constraints_is_tight(UIBoxConstraints constraints) {
  return constraints.present &&
         constraints.min_width >= constraints.max_width &&
         constraints.min_height >= constraints.max_height;
}

typedef struct UIColor {
  bool present;
  f32 r;
  f32 g;
  f32 b;
  f32 a;
} UIColor;

static inline UIColor ui_color_make(f32 r, f32 g, f32 b, f32 a) {
  UIColor color;
  color.present = true;
  color.r = r;
  color.g = g;
  color.b = b;
  color.a = a;
  return color;
}

typedef struct UIPaintingContext {
  int placeholder;
} UIPaintingContext;

enum {
  UI_WIDGET_MESSAGE_UNKNOWN,
  UI_WIDGET_MESSAGE_MOUNT,
  UI_WIDGET_MESSAGE_UNMOUNT,
  UI_WIDGET_MESSAGE_LAYOUT,
  UI_WIDGET_MESSAGE_PAINT,
  UI_WIDGET_MESSAGE_GET_PARENT_DATA,
};

typedef struct UIWidgetMessage {
  u32 type;
} UIWidgetMessage;

typedef struct UIWidgetMessageMount {
  u32 type;  // UI_WIDGET_MESSAGE_MOUNT
} UIWidgetMessageMount;

typedef struct UIWidgetMessageUnmount {
  u32 type;  // UI_WIDGET_MESSAGE_UNMOUNT
} UIWidgetMessageUnmount;

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

typedef enum UIWidgetStatus {
  UI_WIDGET_STATUS_UNMOUNTED,
  UI_WIDGET_STATUS_MOUNTED,
} UIWidgetStatus;

struct UIWidget {
  UIWidgetClass *klass;
  /// Previous sibling of this widget.
  UIWidget *prev;
  /// Next sibling of this widget.
  UIWidget *next;
  /// First child of this widget.
  UIWidget *first;
  /// Last child of this widget.
  UIWidget *last;

  UIWidgetStatus status;

  u32 child_count;

  /// The size of this box computed during layout.
  Vec2 size;
  /// The offset at which to paint the child in the parent's coordinate system.
  Vec2 offset;

  void *state;
};

void ui_widget_begin(UIWidgetClass *klass, const void *props);
void ui_widget_end(UIWidgetClass *klass);
UIWidget *ui_widget_get_current(void);
UIWidget *ui_widget_get_root(void);
UIWidget *ui_widget_get_last_child(void);

/// Get an arena for storing temporary data that needs to keep across begin and
/// end.
///
/// Allocations MUST be freed before exiting `end` in a FILO (stack) order.
Arena *ui_get_build_arena(void);

static inline UIKey ui_widget_get_key(UIWidget *widget) {
  UIKey *key_ptr = (UIKey *)(widget + 1);
  return *key_ptr;
}

static inline void *ui_widget_get_props_(UIWidget *widget, usize props_size) {
  ASSERTF(props_size == widget->klass->props_size,
          "%s: klass.props_size (%d) doesn't match requested props_size (%d)",
          widget->klass->name, (int)props_size, (int)widget->klass->props_size);
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
extern UIWidgetClass ui_limited_box_class;

typedef struct UILimitedBoxProps {
  UIKey key;
  f32 max_width;
  f32 max_height;
} UILimitedBoxProps;

static inline void ui_limited_box_begin(const UILimitedBoxProps *props) {
  ui_widget_begin(&ui_limited_box_class, props);
}

static inline void ui_limited_box_end(void) {
  ui_widget_end(&ui_limited_box_class);
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIColoredBox
///
/// A widget that paints its area with a specified color and then draws its
/// child on top of that color.
///
extern UIWidgetClass ui_colored_box_class;

typedef struct UIColoredBoxProps {
  UIKey key;
  UIColor color;
} UIColoredBoxProps;

static inline void ui_colored_box_begin(const UIColoredBoxProps *props) {
  ui_widget_begin(&ui_colored_box_class, props);
}

static inline void ui_colored_box_end(void) {
  ui_widget_end(&ui_colored_box_class);
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIConstrainedBox
///
/// A widget that imposes additional constraints on its child.
extern UIWidgetClass ui_constrained_box_class;

typedef struct UIConstrainedBoxProps {
  UIKey key;
  UIBoxConstraints constraints;
} UIConstrainedBoxProps;

static inline void ui_constrained_box_begin(
    const UIConstrainedBoxProps *props) {
  ui_widget_begin(&ui_constrained_box_class, props);
}

static inline void ui_constrained_box_end(void) {
  ui_widget_end(&ui_constrained_box_class);
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIAlign
///
/// A widget that aligns its child within itself and optionally sizes itself
/// based on the child's size.
extern UIWidgetClass ui_align_class;

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
  bool present;
  f32 x;
  f32 y;
} UIAlignment;

static inline UIAlignment ui_alignment_make(f32 x, f32 y) {
  UIAlignment alignment;
  alignment.present = true;
  alignment.x = x;
  alignment.y = y;
  return alignment;
}

static inline Vec2 ui_alignment_align_offset(UIAlignment alignment,
                                             Vec2 offset) {
  f32 center_x = offset.x / 2.0f;
  f32 center_y = offset.y / 2.0f;
  return v2(center_x + alignment.x * center_x,
            center_y + alignment.y * center_y);
}

static inline UIAlignment ui_alignment_top_left(void) {
  return ui_alignment_make(-1, -1);
}

static inline UIAlignment ui_alignment_top_center(void) {
  return ui_alignment_make(0, -1);
}

static inline UIAlignment ui_alignment_top_right(void) {
  return ui_alignment_make(1, -1);
}

static inline UIAlignment ui_alignment_center_left(void) {
  return ui_alignment_make(-1, 0);
}

static inline UIAlignment ui_alignment_center(void) {
  return ui_alignment_make(0, 0);
}

static inline UIAlignment ui_alignment_center_right(void) {
  return ui_alignment_make(1, 0);
}

static inline UIAlignment ui_alignment_bottom_left(void) {
  return ui_alignment_make(-1, 1);
}

static inline UIAlignment ui_alignment_bottom_center(void) {
  return ui_alignment_make(0, 1);
}

static inline UIAlignment ui_alignment_bottom_right(void) {
  return ui_alignment_make(1, 1);
}

typedef struct UISizeFactor {
  bool width_present;
  bool height_present;
  f32 width;
  f32 height;
} UISizeFactor;

typedef struct UIAlignProps {
  UIKey key;
  UIAlignment alignment;
  UISizeFactor factor;
} UIAlignProps;

static inline void ui_align_begin(const UIAlignProps *props) {
  ui_widget_begin(&ui_align_class, props);
}

static inline void ui_align_end(void) { ui_widget_end(&ui_align_class); }

////////////////////////////////////////////////////////////////////////////////
///
/// UICenter
///
/// A widget that centers its child within itself.
extern UIWidgetClass ui_center_class;

typedef struct UICenterProps {
  UIKey key;
  UISizeFactor factor;
} UICenterProps;

static inline void ui_center_begin(const UICenterProps *props) {
  ui_widget_begin(&ui_center_class, props);
}

static inline void ui_center_end(void) { ui_widget_end(&ui_center_class); }

////////////////////////////////////////////////////////////////////////////////
///
/// UIPadding
///
/// A widget that insets its child by the given padding.
extern UIWidgetClass ui_padding_class;

typedef struct UIEdgeInsetsDirectional {
  bool present;
  f32 start;
  f32 end;
  f32 top;
  f32 bottom;
} UIEdgeInsetsDirectional;

static inline UIEdgeInsetsDirectional ui_edge_insets_directional_make(
    f32 start, f32 end, f32 top, f32 bottom) {
  UIEdgeInsetsDirectional result;
  result.present = true;
  result.start = start;
  result.end = end;
  result.top = top;
  result.bottom = bottom;
  return result;
}

static inline UIEdgeInsetsDirectional ui_edge_insets_all(f32 val) {
  return ui_edge_insets_directional_make(val, val, val, val);
}

typedef struct UIEdgeInsets {
  f32 left;
  f32 right;
  f32 top;
  f32 bottom;
} UIEdgeInsets;

static inline f32 ui_edge_insets_get_horizontal(UIEdgeInsets edge_insets) {
  return edge_insets.left + edge_insets.right;
}

static inline f32 ui_edge_insets_get_vertical(UIEdgeInsets edge_insets) {
  return edge_insets.top + edge_insets.bottom;
}

static inline UIBoxConstraints ui_box_constraints_deflate(
    UIBoxConstraints constraints, UIEdgeInsets edge_insets) {
  f32 horizontal = ui_edge_insets_get_horizontal(edge_insets);
  f32 vertical = ui_edge_insets_get_vertical(edge_insets);
  f32 deflated_min_width = f32_max(0, constraints.min_width - horizontal);
  f32 deflated_min_height = f32_max(0, constraints.min_height - vertical);
  return ui_box_constraints_make(
      deflated_min_width,
      f32_max(deflated_min_width, constraints.max_width - horizontal),
      deflated_min_height,
      f32_max(deflated_min_height, constraints.max_height - vertical));
}

typedef struct UIPaddingProps {
  UIKey key;
  UIEdgeInsetsDirectional padding;
} UIPaddingProps;

static inline void ui_padding_begin(const UIPaddingProps *props) {
  ui_widget_begin(&ui_padding_class, props);
}

static inline void ui_padding_end(void) { ui_widget_end(&ui_padding_class); }

////////////////////////////////////////////////////////////////////////////////
///
/// UIContainer
///
/// A convenience widget that combines common painting, positioning, and sizing
/// widgets.
///
extern UIWidgetClass ui_container_class;

typedef struct UIContainerProps {
  UIKey key;

  UIAlignment alignment;
  UIEdgeInsetsDirectional padding;
  /// The color to paint behind the children.
  UIColor color;

  // TODO: Decoration

  /// Additional constraints to apply to the children.
  UIBoxConstraints constraints;
  /// Empty space to surround the decoration and children.
  UIEdgeInsetsDirectional margin;
} UIContainerProps;

static inline void ui_container_begin(const UIContainerProps *props) {
  ui_widget_begin(&ui_container_class, props);
}

void ui_container_end(void);

////////////////////////////////////////////////////////////////////////////////
///
/// UIFlexible
///
/// A widget that controls how a child of a UIRow, UIColumn, or UIFlex flexes.
///
extern UIWidgetClass ui_flexible_vtable;

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

static inline void ui_flexible_begin(const UIFlexibleProps *props) {
  ui_widget_begin(&ui_flexible_vtable, props);
}

static inline void ui_flexible_end(void) { ui_widget_end(&ui_flexible_vtable); }

////////////////////////////////////////////////////////////////////////////////
///
/// UIFlex
///
/// A widget that displays its children in a one-dimensional array.
///
extern UIWidgetClass ui_flex_class;

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

static inline void ui_flex_begin(const UIFlexProps *props) {
  ui_widget_begin(&ui_flex_class, props);
}

static inline void ui_flex_end(void) { ui_widget_end(&ui_flex_class); }

////////////////////////////////////////////////////////////////////////////////
///
/// UIColumn
///
/// A widget that displays its children in a vertical array.
///
extern UIWidgetClass ui_column_class;

typedef struct UIColumnProps {
  UIKey key;
  UIMainAxisAlignment main_axis_alignment;
  UIMainAxisSize main_axis_size;
  UICrossAxisAlignment cross_axis_alignment;
  f32 spacing;
} UIColumnProps;

static inline void ui_column_begin(const UIColumnProps *props) {
  ui_widget_begin(&ui_column_class, props);
}

static inline void ui_column_end(void) { ui_widget_end(&ui_column_class); }

////////////////////////////////////////////////////////////////////////////////
///
/// UIRow
///
/// A widget that displays its children in a vertical array.
///
extern UIWidgetClass ui_row_class;

typedef struct UIRowProps {
  UIKey key;
  UIMainAxisAlignment main_axis_alignment;
  UIMainAxisSize main_axis_size;
  UICrossAxisAlignment cross_axis_alignment;
  f32 spacing;
} UIRowProps;

static inline void ui_row_begin(const UIRowProps *props) {
  ui_widget_begin(&ui_row_class, props);
}

static inline void ui_row_end(void) { ui_widget_end(&ui_row_class); }

#endif  // ZTRACING_SRC_UI_H_
