#ifndef ZTRACING_SRC_UI_H_
#define ZTRACING_SRC_UI_H_

#include <stdbool.h>

#include "src/assert.h"
#include "src/math.h"
#include "src/string.h"
#include "src/types.h"

#define UI_BUTTON_PRIMARY 0x01
#define UI_BUTTON_SECONDARY 0x02
#define UI_BUTTON_TERTIARY 0x04

#define UI_MOUSE_BUTTON_PRIMARY UI_BUTTON_PRIMARY
#define UI_MOUSE_BUTTON_SECONDARY UI_BUTTON_SECONDARY
#define UI_MOUSE_BUTTON_TERTIARY UI_BUTTON_TERTIARY
#define UI_MOUSE_BUTTON_BACK 0x8
#define UI_MOUSE_BUTTON_FORWARD 0x10

void ui_set_viewport(Vec2 min, Vec2 max);

void ui_on_focus_lost(Vec2 pos);
void ui_on_mouse_move(Vec2 pos);
void ui_on_mouse_button_down(Vec2 pos, u32 button);
void ui_on_mouse_button_up(Vec2 pos, u32 button);

void ui_begin_frame(void);
void ui_end_frame(void);

Str8 ui_push_str8(Str8 str);
Str8 ui_push_str8f(const char *format, ...);
Str8 ui_push_str8fv(const char *format, va_list ap);

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
  f32 min_width;
  f32 max_width;
  f32 min_height;
  f32 max_height;
} UIBoxConstraints;

OPTIONAL_TYPE(UIBoxConstraintsO, UIBoxConstraints, ui_box_constraints);

static inline UIBoxConstraints ui_box_constraints(f32 min_width, f32 max_width,
                                                  f32 min_height,
                                                  f32 max_height) {
  UIBoxConstraints result;
  result.min_width = min_width;
  result.max_width = max_width;
  result.min_height = min_height;
  result.max_height = max_height;
  return result;
}

/// Creates box constraints that is respected only by the given size.
static inline UIBoxConstraints ui_box_constraints_tight(f32 width, f32 height) {
  return ui_box_constraints(width, width, height, height);
}

/// Creates box constraints that require the given width.
static inline UIBoxConstraints ui_box_constraints_tight_width(f32 width) {
  return ui_box_constraints(width, width, 0, F32_INFINITY);
}

/// Creates box constraints that require the given height.
static inline UIBoxConstraints ui_box_constraints_tight_height(f32 height) {
  return ui_box_constraints(0, F32_INFINITY, height, height);
}

/// Returns the width that both satisfies the constraints and is as close as
/// possible to the given width.
static inline f32 ui_box_constraints_constrain_width(UIBoxConstraints self,
                                                     f32 width) {
  return f32_clamp(width, self.min_width, self.max_width);
}

/// Returns the height that both satisfies the constraints and is as close as
/// possible to the given height.
static inline f32 ui_box_constraints_constrain_height(UIBoxConstraints self,
                                                      f32 height) {
  return f32_clamp(height, self.min_height, self.max_height);
}

/// Returns the size that both satisfies the constraints and is as close as
/// possible to the given size.
static inline Vec2 ui_box_constraints_constrain(UIBoxConstraints self,
                                                Vec2 size) {
  return vec2(ui_box_constraints_constrain_width(self, size.x),
              ui_box_constraints_constrain_height(self, size.y));
}

/// The biggest size that satisfies the constraints.
static inline Vec2 ui_box_constraints_get_biggest(UIBoxConstraints self) {
  return vec2(ui_box_constraints_constrain_width(self, F32_INFINITY),
              ui_box_constraints_constrain_height(self, F32_INFINITY));
}

static inline UIBoxConstraints ui_box_constraints_flip(UIBoxConstraints self) {
  return ui_box_constraints(self.min_height, self.max_height, self.min_width,
                            self.max_width);
}

static inline bool ui_box_constraints_has_bounded_width(UIBoxConstraints self) {
  return self.max_width < F32_INFINITY;
}

static inline bool ui_box_constraints_has_bounded_height(
    UIBoxConstraints self) {
  return self.max_height < F32_INFINITY;
}

/// Returns new box constraints that respect the given constraints while being
/// as close as possible to the original constraints.
static inline UIBoxConstraints ui_box_constraints_enforce(
    UIBoxConstraints self, UIBoxConstraints constraints) {
  return ui_box_constraints(
      f32_clamp(self.min_width, constraints.min_width, constraints.max_width),
      f32_clamp(self.max_width, constraints.min_width, constraints.max_width),
      f32_clamp(self.min_height, constraints.min_height,
                constraints.max_height),
      f32_clamp(self.max_height, constraints.min_height,
                constraints.max_height));
}

static inline UIBoxConstraints ui_box_constraints_loosen(
    UIBoxConstraints self) {
  return ui_box_constraints(0, self.max_width, 0, self.max_height);
}

static inline bool ui_box_constraints_is_tight(UIBoxConstraints self) {
  return self.min_width >= self.max_width && self.min_height >= self.max_height;
}

/// Creates box constraints that require the given width or height.
static inline UIBoxConstraints ui_box_constraints_tight_for(f32o width,
                                                            f32o height) {
  return ui_box_constraints(width.present ? width.value : 0,
                            width.present ? width.value : F32_INFINITY,
                            height.present ? height.value : 0,
                            height.present ? height.value : F32_INFINITY);
}

/// Returns new box constraints with a tight width and/or height as close to
/// the given width and height as possible while still respecting the original
/// box constraints.
static inline UIBoxConstraints ui_box_constraints_tighten(UIBoxConstraints self,
                                                          f32o width,
                                                          f32o height) {
  return ui_box_constraints(
      width.present ? f32_clamp(width.value, self.min_width, self.max_width)
                    : self.min_width,
      width.present ? f32_clamp(width.value, self.min_width, self.max_width)
                    : self.max_width,
      height.present ? f32_clamp(height.value, self.min_height, self.max_height)
                     : self.min_height,
      height.present ? f32_clamp(height.value, self.min_height, self.max_height)
                     : self.max_height);
}

typedef struct UIColor {
  f32 r;
  f32 g;
  f32 b;
  f32 a;
} UIColor;

OPTIONAL_TYPE(UIColorO, UIColor, ui_color);

static inline UIColor ui_color(f32 r, f32 g, f32 b, f32 a) {
  UIColor color;
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
  UI_MESSAGE_UNKNOWN,
  UI_MESSAGE_MOUNT,
  UI_MESSAGE_UPDATE,
  UI_MESSAGE_UNMOUNT,
  UI_MESSAGE_LAYOUT,
  UI_MESSAGE_PAINT,
  UI_MESSAGE_GET_PARENT_DATA,

  // EVENT HANDLING
  UI_MESSAGE_HIT_TEST,
  UI_MESSAGE_HANDLE_EVENT,
};

typedef struct UIMessageMount {
  u32 type;  // UI_MESSAGE_MOUNT
} UIMessageMount;

typedef struct UIMessageUpdate {
  u32 type;  // UI_MESSAGE_UPDATE
} UIMessageUpdate;

typedef struct UIMessageUnmount {
  u32 type;  // UI_MESSAGE_UNMOUNT
} UIMessageUnmount;

typedef struct UIMessageLayout {
  u32 type;  // UI_MESSAGE_LAYOUT
  UIBoxConstraints constraints;
} UIMessageLayout;

typedef struct UIMessagePaint {
  u32 type;  // UI_MESSAGE_PAINT
  UIPaintingContext *context;
  Vec2 offset;
} UIMessagePaint;

enum {
  UI_WIDGET_PARENT_DATA_FLEX = 1,
};

/// Callback should return true there is parent data.
typedef struct UIMessageGetParentData {
  u32 type;  // UI_MESSAGE_GET_PARENT_DATA
  u32 parent_data_id;
  void *parent_data;
} UIMessageGetParentData;

typedef struct UIHitTestEntry UIHitTestEntry;
struct UIHitTestEntry {
  UIHitTestEntry *prev;
  UIHitTestEntry *next;

  UIWidget *widget;
  Vec2 local_position;
};

typedef struct UIHitTestResult {
  UIHitTestEntry *first;
  UIHitTestEntry *last;
} UIHitTestResult;

// Add hit test entry to the result in a front-to-back order. i.e. the first
// widget received the hit should be the first, the root should be the last.
void ui_hit_test_result_add(UIHitTestResult *self, Arena *arena,
                            UIWidget *widget, Vec2 local_position);

/// Callback should return true if hit.
typedef struct UIMessageHitTest {
  u32 type;  // UI_MESSAGE_HIT_TEST
  UIHitTestResult *result;
  Arena *arena;
  /// Position relative to UIWidget's local coordinate system. (0, 0) is the
  /// top-left of the box.
  Vec2 local_position;
} UIMessageHitTest;

typedef enum UIPointerEventType {
  UI_POINTER_EVENT_UNKNOWN,
  /// A pointer comes into contact with the screen (for touch pointers), or has
  /// its button pressed (for mouse pointers) at this widget's location.
  UI_POINTER_EVENT_DOWN,
  /// A pointer that triggered an UI_POINTER_EVENT_DOWN changes position.
  UI_POINTER_EVENT_MOVE,
  /// A pointer that triggered an UI_POINTER_EVENT_DOWN is no longer in contact
  /// with the screen.
  UI_POINTER_EVENT_UP,
  /// A pointer that triggered an UI_POINTER_EVENT_DOWN is no longer directed
  /// towards this receiver.
  UI_POINTER_EVENT_CANCEL,
  /// A pointer that has not triggered an UI_POINTER_EVENT_DOWN changes
  /// position.
  UI_POINTER_EVENT_HOVER,
  /// A pointer has entered this widget.
  UI_POINTER_EVENT_ENTER,
  /// A pointer has exited this widget when the widget is still mounted.
  UI_POINTER_EVENT_EXIT,
} UIPointerEventType;

typedef struct UIPointerEvent {
  UIPointerEventType type;
  u32 button;
  /// Coordinate of the position of the pointer, in logical pixels in the global
  /// coordinate space.
  Vec2 position;
  /// The position transformed into the event receiver's local coordinate
  /// system.
  Vec2 local_position;
} UIPointerEvent;

OPTIONAL_TYPE(UIPointerEventO, UIPointerEvent, ui_pointer_event);

typedef struct UIMessageHandleEvent {
  u32 type;  // UI_MESSAGE_HANDLE_EVENT
  UIPointerEvent *event;
} UIMessageHandleEvent;

typedef union UIMessage {
  u32 type;
  UIMessageMount mount;
  UIMessageUpdate update;
  UIMessageUnmount umount;
  UIMessageLayout layout;
  UIMessagePaint paint;
  UIMessageGetParentData get_parent_data;
  UIMessageHitTest hit_test;
  UIMessageHandleEvent handle_event;
} UIMessage;

typedef i32(UIWidgetCallback)(UIWidget *widget, UIMessage *message);

typedef struct UIWidgetClass {
  const char *name;
  usize props_size;
  usize state_size;
  UIWidgetCallback *callback;
} UIWidgetClass;

typedef enum UIWidgetStatus {
  UI_WIDGET_STATUS_UNMOUNTED,
  UI_WIDGET_STATUS_MOUNTED,
} UIWidgetStatus;

struct UIWidget {
  UIWidgetClass *klass;
  UIWidget *parent;
  /// Previous sibling of this widget.
  UIWidget *prev;
  /// Next sibling of this widget.
  UIWidget *next;
  /// First child of this widget.
  UIWidget *first;
  /// Last child of this widget.
  UIWidget *last;
  u32 child_count;

  UIWidgetStatus status;

  /// The size of this box computed during layout.
  ///
  /// The value is initialized to the size of the same widget from last frame
  /// before layout.
  Vec2 size;
  /// The offset at which to paint the child in the parent's coordinate system.
  ///
  /// The value is initialized to the offset of the same widget from last frame
  /// before layout.
  Vec2 offset;

  // The same widget instance from last frame, if any.
  UIWidget *old_widget;
  void *state;
};

UIWidget *ui_widget_begin(UIWidgetClass *klass, const void *props);
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
          widget->klass->name, (int)widget->klass->props_size, (int)props_size);
  return widget + 1;
}

#define ui_widget_get_props(widget, Props) \
  ((Props *)ui_widget_get_props_(widget, sizeof(Props)))

static inline void *ui_widget_get_state_(UIWidget *widget, usize state_size) {
  ASSERTF(state_size == widget->klass->state_size,
          "%s: klass.state_size (%d) doesn't match requested state_size (%d)",
          widget->klass->name, (int)widget->klass->state_size, (int)state_size);
  ASSERTF(widget->klass->state_size > 0, "%s doesn't have state",
          widget->klass->name);
  ASSERT(widget->state);
  return widget->state;
}

#define ui_widget_get_state(widget, State) \
  ((State *)ui_widget_get_state_(widget, sizeof(State)))

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
  f32 x;
  f32 y;
} UIAlignment;

OPTIONAL_TYPE(UIAlignmentO, UIAlignment, ui_alignment);

static inline UIAlignment ui_alignment(f32 x, f32 y) {
  UIAlignment alignment;
  alignment.x = x;
  alignment.y = y;
  return alignment;
}

static inline Vec2 ui_alignment_align_offset(UIAlignment alignment,
                                             Vec2 offset) {
  f32 center_x = offset.x / 2.0f;
  f32 center_y = offset.y / 2.0f;
  return vec2(center_x + alignment.x * center_x,
              center_y + alignment.y * center_y);
}

static inline UIAlignment ui_alignment_top_left(void) {
  return ui_alignment(-1, -1);
}

static inline UIAlignment ui_alignment_top_center(void) {
  return ui_alignment(0, -1);
}

static inline UIAlignment ui_alignment_top_right(void) {
  return ui_alignment(1, -1);
}

static inline UIAlignment ui_alignment_center_left(void) {
  return ui_alignment(-1, 0);
}

static inline UIAlignment ui_alignment_center(void) {
  return ui_alignment(0, 0);
}

static inline UIAlignment ui_alignment_center_right(void) {
  return ui_alignment(1, 0);
}

static inline UIAlignment ui_alignment_bottom_left(void) {
  return ui_alignment(-1, 1);
}

static inline UIAlignment ui_alignment_bottom_center(void) {
  return ui_alignment(0, 1);
}

static inline UIAlignment ui_alignment_bottom_right(void) {
  return ui_alignment(1, 1);
}

typedef struct UIAlignProps {
  UIKey key;
  UIAlignment alignment;
  f32o width;
  f32o height;
} UIAlignProps;

static inline void ui_align_begin(const UIAlignProps *props) {
  ui_widget_begin(&ui_align_class, props);
}

static inline void ui_align_end(void) { ui_widget_end(&ui_align_class); }

////////////////////////////////////////////////////////////////////////////////
///
/// UIUnconstrainedBox
///
/// A widget that imposes no constraints on its child, allowing it to render
/// at its "natural" size.
extern UIWidgetClass ui_unconstrained_box_class;

typedef struct UIUnconstrainedBoxProps {
  UIKey key;
  UIAlignment alignment;
} UIUnconstrainedBoxProps;

static inline void ui_unconstrained_box_begin(
    const UIUnconstrainedBoxProps *props) {
  ui_widget_begin(&ui_unconstrained_box_class, props);
}

static inline void ui_unconstrained_box_end(void) {
  ui_widget_end(&ui_unconstrained_box_class);
}

////////////////////////////////////////////////////////////////////////////////
///
/// UICenter
///
/// A widget that centers its child within itself.
extern UIWidgetClass ui_center_class;

typedef struct UICenterProps {
  UIKey key;
  f32o width;
  f32o height;
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

typedef struct UIEdgeInsets {
  f32 start;
  f32 end;
  f32 top;
  f32 bottom;
} UIEdgeInsets;

OPTIONAL_TYPE(UIEdgeInsetsO, UIEdgeInsets, ui_edge_insets);

static inline UIEdgeInsets ui_edge_insets(f32 start, f32 end, f32 top,
                                          f32 bottom) {
  UIEdgeInsets result;
  result.start = start;
  result.end = end;
  result.top = top;
  result.bottom = bottom;
  return result;
}

static inline UIEdgeInsets ui_edge_insets_all(f32 val) {
  return ui_edge_insets(val, val, val, val);
}

static inline UIEdgeInsets ui_edge_insets_symmetric(f32 x, f32 y) {
  return ui_edge_insets(x, x, y, y);
}

typedef struct UIPaddingProps {
  UIKey key;
  UIEdgeInsets padding;
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

  f32o width;
  f32o height;

  UIAlignmentO alignment;
  UIEdgeInsetsO padding;
  /// The color to paint behind the children.
  UIColorO color;

  // TODO: Decoration

  /// Additional constraints to apply to the children.
  UIBoxConstraintsO constraints;
  /// Empty space to surround the decoration and children.
  UIEdgeInsetsO margin;
} UIContainerProps;

void ui_container_begin(const UIContainerProps *props);
void ui_container_end(void);

////////////////////////////////////////////////////////////////////////////////
///
/// UIFlexible
///
/// A widget that controls how a child of a UIRow, UIColumn, or UIFlex flexes.
///
extern UIWidgetClass ui_flexible_class;

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
  ui_widget_begin(&ui_flexible_class, props);
}

static inline void ui_flexible_end(void) { ui_widget_end(&ui_flexible_class); }

////////////////////////////////////////////////////////////////////////////////
///
/// UIExpanded
///
/// A widget that expands a child of a UIRow, UIColumn, or UIFlex so that the
/// child fills the available space.
///
extern UIWidgetClass ui_expanded_class;

typedef struct UIExpandedProps {
  UIKey key;
  i32 flex;
} UIExpandedProps;

void ui_expanded_begin(const UIExpandedProps *props);

static inline void ui_expanded_end(void) { ui_widget_end(&ui_expanded_class); }

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

void ui_column_begin(const UIColumnProps *props);
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

void ui_row_begin(const UIRowProps *props);
static inline void ui_row_end(void) { ui_widget_end(&ui_row_class); }

////////////////////////////////////////////////////////////////////////////////
///
/// UIPointerListener
///
/// A widget that listens to common pointer events.
///
extern UIWidgetClass ui_pointer_listener_class;

typedef struct UIPointerListenerProps {
  UIKey key;
  UIPointerEventO *down;
  UIPointerEventO *move;
  UIPointerEventO *up;
  UIPointerEventO *cancel;
} UIPointerListenerProps;

void ui_pointer_listener_begin(const UIPointerListenerProps *props);
static inline void ui_pointer_listener_end(void) {
  ui_widget_end(&ui_pointer_listener_class);
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIMouseRegion
///
/// A widget that tracks the movement of mice.
///
extern UIWidgetClass ui_mouse_region_class;

typedef struct UIMouseRegionProps {
  UIKey key;
  UIPointerEventO *enter;
  UIPointerEventO *hover;
  UIPointerEventO *exit;
} UIMouseRegionProps;

void ui_mouse_region_begin(const UIMouseRegionProps *props);
static inline void ui_mouse_region_end(void) {
  ui_widget_end(&ui_mouse_region_class);
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIGestureDetector
///
/// A widget that detects gestures.
///
extern UIWidgetClass ui_gesture_detector_class;

typedef struct UIGestureDetectorProps {
  UIKey key;
  bool *tap;
} UIGestureDetectorProps;

void ui_gesture_detector_begin(const UIGestureDetectorProps *props);
void ui_gesture_detector_end(void);

////////////////////////////////////////////////////////////////////////////////
///
/// UIText
///
/// A run of text with a single style.
extern UIWidgetClass ui_text_class;

typedef struct UITextStyle {
  UIColorO color;
  f32o font_size;
} UITextStyle;

OPTIONAL_TYPE(UITextStyleO, UITextStyle, ui_text_style);

typedef struct UITextProps {
  UIKey key;
  // String must be valid for the whole frame. Consider using
  // `ui_push_str8[fv]` to allocate strings.
  Str8 text;
  UITextStyleO style;
} UITextProps;

void ui_text(const UITextProps *props);

#endif  // ZTRACING_SRC_UI_H_
