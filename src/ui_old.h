#ifndef ZTRACING_SRC_UI_H_
#define ZTRACING_SRC_UI_H_

#include "src/math.h"
#include "src/string.h"
#include "src/types.h"

#define kUISizeUndefined 0
#define kUISizeInfinity F32_INFINITY
#define kUIFontSizeDefault 16.0f

typedef struct UIID {
  u64 hash;
} UIID;

UIID uuid_from_u8(UIID seed, u8 ch);

typedef enum UIPosition {
  kUIPositionRelative,
  kUIposition_absolute,
  kUIPositionFixed,
} UIPosition;

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

typedef struct UIEdgeInsets {
  bool set[4];
  f32 left;
  f32 right;
  f32 top;
  f32 bottom;
} UIEdgeInsets;

static inline UIEdgeInsets ui_edge_insets_all(f32 val) {
  UIEdgeInsets result = {
      .set = {true, true, true, true},
      .left = val,
      .right = val,
      .top = val,
      .bottom = val,
  };
  return result;
}

static inline UIEdgeInsets ui_edge_insets_symmetric(f32 x, f32 y) {
  UIEdgeInsets result = {
      .set = {true, true, true, true},
      .left = x,
      .right = x,
      .top = y,
      .bottom = y,
  };
  return result;
}

static inline UIEdgeInsets ui_edge_insets_from_ltrb(f32 left, f32 top,
                                                    f32 right, f32 bottom) {
  UIEdgeInsets result = {
      .set = {true, true, true, true},
      .left = left,
      .right = right,
      .top = top,
      .bottom = bottom,
  };
  return result;
}

static inline UIEdgeInsets ui_edge_insets_from_lt(f32 left, f32 top) {
  UIEdgeInsets result = {
      .set = {true, false, true, false},
      .left = left,
      .right = 0,
      .top = top,
      .bottom = 0,
  };
  return result;
}

static inline UIEdgeInsets ui_edge_insets_from_rb(f32 right, f32 bottom) {
  UIEdgeInsets result = {
      .set = {false, true, false, true},
      .left = 0,
      .right = right,
      .top = 0,
      .bottom = bottom,
  };
  return result;
}

static inline bool ui_edge_insets_is_set(UIEdgeInsets edge_insets) {
  b32 result = edge_insets.set[0] || edge_insets.set[1] || edge_insets.set[2] ||
               edge_insets.set[3];
  return result;
}

static inline bool ui_edge_insets_is_left_set(UIEdgeInsets edge_insets) {
  bool result = edge_insets.set[0];
  return result;
}

static inline bool ui_edge_insets_is_right_set(UIEdgeInsets edge_insets) {
  bool result = edge_insets.set[1];
  return result;
}

static inline bool ui_edge_insets_is_top_set(UIEdgeInsets edge_insets) {
  bool result = edge_insets.set[2];
  return result;
}

static inline bool ui_edge_insets_is_bottom_set(UIEdgeInsets edge_insets) {
  bool result = edge_insets.set[3];
  return result;
}

// A side of a border of a box.
typedef struct UIBorderSide {
  // The color of this side of the border.
  ColorU32 color;
  // The width of this side of the border, in points.
  f32 width;
} UIBorderSide;

// A border of a box, comprised of four sides: left, top, right, bottom.
typedef struct UIBorder {
  UIBorderSide left;
  UIBorderSide top;
  UIBorderSide right;
  UIBorderSide bottom;
} UIBorder;

// A uniform border with all sides the same color and width.
static inline UIBorder ui_border_from_border_side(UIBorderSide border_side) {
  UIBorder result;
  result.left = border_side;
  result.top = border_side;
  result.right = border_side;
  result.bottom = border_side;
  return result;
}

typedef enum UIMouseButton {
  kUIMouseButtonLeft,
  kUIMouseButtonRight,
  kUIMouseButtonMiddle,
  kUIMouseButtonX1,
  kUIMouseButtonX2,
  kUIMouseButtonCount,
} UIMouseButton;

// per-frame info provided by builders
typedef struct UIProps {
  Str8 key;

  ColorU32 background_color;
  // The size of the box, including border and padding.
  Vec2 size;
  Axis2 main_axis;
  f32 flex;
  UIMainAxisSize main_axis_size;
  UIMainAxisAlignment main_axis_align;
  UICrossAxisAlignment cross_axis_align;
  bool isolate;

  UIPosition position;
  i32 z_index;
  UIEdgeInsets offset;
  UIEdgeInsets margin;
  UIBorder border;
  UIEdgeInsets padding;

  Str8 text;
  // TODO: Add text_align.
  // TODO: Extract into TextStyle which has font_family, font_size, font_weight
  // and etc.
  ColorU32 color;
  f32 font_size;
} UIProps;

// Data that is computed every frame
typedef struct UIComputed {
  Vec2 min_size;
  Vec2 max_size;

  Vec2 size;
  Vec2 rel_pos;

  f32 font_size;

  Rect2 screen_rect;
  Rect2 clip_rect;
} UIComputed;

typedef struct UIBoxState {
  const char *type_name;
  void *ptr;
  usize size;
} UIBoxState;

typedef struct UIBox UIBox;

typedef struct UIBoxTreeLink {
  UIBox *prev;
  UIBox *next;
  UIBox *first;
  UIBox *last;
  UIBox *parent;
} UIBoxTreeLink;

struct UIBox {
  UIID id;
  const char *tag;
  u32 seq;

  // hash links
  UIBox *hash_prev;
  UIBox *hash_next;

  // tree links for building order
  UIBoxTreeLink build;
  // tree links for stacking order
  UIBoxTreeLink stack;
  u32 children_count;

  bool hoverable;
  bool clickable[kUIMouseButtonCount];
  bool scrollable;

  UIProps props;
  UIComputed computed;

  UIBoxState state;
};

typedef struct UIBoxHashSlot UIBoxHashSlot;
struct UIBoxHashSlot {
  UIBoxHashSlot *prev;
  UIBoxHashSlot *next;
  UIBox *first;
  UIBox *last;
};

typedef struct UIBoxCache {
  u32 total_box_count;
  // Hash slots for box hash table
  u32 box_hash_slots_count;
  UIBoxHashSlot *box_hash_slots;
} UIBoxCache;

typedef struct UIMouseButtonState {
  b8 is_down;
  b8 transition_count;
} UIMouseButtonState;

typedef struct UIMouseInput {
  Vec2 pos;
  Vec2 wheel;
  UIMouseButtonState buttons[kUIMouseButtonCount];

  UIID hovering;
  UIID pressed[kUIMouseButtonCount];
  Vec2 pressed_pos[kUIMouseButtonCount];
  UIID holding[kUIMouseButtonCount];
  UIID clicked[kUIMouseButtonCount];
  UIID scrolling;
  Vec2 scroll_delta;
} UIMouseInput;

typedef struct UIBuildError UIBuildError;
struct UIBuildError {
  UIBuildError *prev;
  UIBuildError *next;
  Str8 message;
};

// Per-frame info
typedef struct UIFrame {
  Arena arena;
  UIBoxCache cache;

  u64 frame_index;
  Vec2 viewport_size;

  UIBox *root;
  UIBox *current_build;
  UIBox *current_stack;

  UIBuildError *first_error;
  UIBuildError *last_error;
} UIFrame;

typedef struct UIInput {
  f32 dt;
  UIMouseInput mouse;
} UIInput;

typedef struct UIState {
  b32 init;
  UIInput input;
  u64 frame_index;
  UIFrame frames[2];
  UIFrame *current_frame;
  UIFrame *last_frame;

  f32 fast_rate;
} UIState;

void ui_init(void);
void ui_quit(void);

// Mouse pos in points.
void ui_on_mouse_pos(Vec2 pos);
void ui_on_mouse_button_up(Vec2 pos, UIMouseButton button);
void ui_on_mouse_button_down(Vec2 pos, UIMouseButton button);
void ui_on_mouse_wheel(Vec2 delta);

void ui_set_delta_time(f32 dt);

extern THREAD_LOCAL UIState t_ui_state;

static inline UIState *ui_state_get(void) {
  UIState *state = &t_ui_state;
  DEBUG_ASSERT(state->init);
  return state;
}

// Get current UIFrame
static inline UIFrame *ui_frame_get(void) {
  UIState *state = ui_state_get();
  UIFrame *result = state->current_frame;
  return result;
}

// Get last UIFrame
static inline UIFrame *ui_frame_get_last(void) {
  UIState *state = ui_state_get();
  UIFrame *result = state->last_frame;
  return result;
}

static inline f32 ui_get_delta_time(void) {
  UIState *state = ui_state_get();
  return state->input.dt;
}

static inline f32 ui_animate_fast_f32(f32 value, f32 target) {
  UIState *state = ui_state_get();
  f32 result;
  f32 diff = (target - value);
  if (f32_abs(diff) < 0.0001f) {
    result = target;
    // TODO: Trigger re-draw.
  } else {
    result = value + diff * state->fast_rate;
  }
  return result;
}

static inline ColorU32 ui_animate_fast_color_u32(ColorU32 value,
                                                 ColorU32 target) {
  ColorU32 result;
  result.r = ui_animate_fast_f32(value.r, target.r);
  result.g = ui_animate_fast_f32(value.g, target.g);
  result.b = ui_animate_fast_f32(value.b, target.b);
  result.a = ui_animate_fast_f32(value.a, target.a);
  return result;
}

void ui_begin_frame(Vec2 viewport_size);
void ui_end_frame(void);
void ui_render(void);

UIBox *ui_box_get(UIFrame *frame, UIID id);

UIBuildError *ui_get_first_build_error(void);

static inline UIID uuid_zero(void) {
  UIID result = {0};
  return result;
}

bool uuid_is_equal(UIID a, UIID b);

static inline bool uuid_is_zero(UIID a) {
  bool result = uuid_is_equal(a, uuid_zero());
  return result;
}

Str8 ui_push_str8(Str8 str);
Str8 ui_push_str8f(const char *fmt, ...);
Str8 ui_push_str8fv(const char *fmt, va_list ap);

void ui_tag_begin(const char *tag, UIProps props);
void ui_tag_end(const char *tag);

static inline void ui_box_begin(UIProps props) { ui_tag_begin("Box", props); }

static inline void ui_box_end(void) { ui_tag_end("Box"); }

static inline UIBox *ui_box_get_current(void) {
  UIFrame *frame = ui_frame_get();
  DEBUG_ASSERT(frame->current_build);
  UIBox *box = frame->current_build;
  return box;
}

void *ui_box_push_state(const char *type_name, usize size);
void *ui_box_get_state(const char *type_name, usize size);

#define ui_box_push_struct(Type) (Type *)ui_box_push_state(#Type, sizeof(Type))
#define ui_box_get_struct(Type) (Type *)ui_box_get_state(#Type, sizeof(Type))

Vec2 ui_get_mouse_rel_pos(void);
Vec2 ui_get_mouse_pos(void);

void ui_set_block_mouse_input(void);
bool ui_is_mouse_hovering(void);
bool ui_is_mouse_button_pressed(UIMouseButton button);
bool ui_is_mouse_button_down(UIMouseButton button);
bool ui_is_mouse_button_clicked(UIMouseButton button);
bool ui_is_mouse_button_dragging(UIMouseButton button, Vec2 *delta);
bool ui_is_mouse_button_scrolling(Vec2 *delta);

#endif  // ZTRACING_SRC_UI_H_
