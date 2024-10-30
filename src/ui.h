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

typedef enum UIPosition {
  kUIPositionRelative,
  kUIPositionAbsolute,
  kUIPositionFixed,
} UIPosition;

typedef enum UIMainAxisSize {
  kUIMainAxisSizeMin,
  kUIMainAxisSizeMax,
} UIMainAxisSize;

typedef enum UIMainAxisAlign {
  kUIMainAxisAlignUnknown,
  kUIMainAxisAlignStart,
  kUIMainAxisAlignCenter,
  kUIMainAxisAlignEnd,
} UIMainAxisAlign;

typedef enum UICrossAxisAlign {
  kUICrossAxisAlignUnknown,
  kUICrossAxisAlignStart,
  kUICrossAxisAlignCenter,
  kUICrossAxisAlignEnd,
  kUICrossAxisAlignStretch,
} UICrossAxisAlign;

typedef struct UIEdgeInsets {
  bool set[4];
  f32 left;
  f32 right;
  f32 top;
  f32 bottom;
} UIEdgeInsets;

static inline UIEdgeInsets UIEdgeInsetsAll(f32 val) {
  UIEdgeInsets result = {
      .set = {true, true, true, true},
      .left = val,
      .right = val,
      .top = val,
      .bottom = val,
  };
  return result;
}

static inline UIEdgeInsets UIEdgeInsetsSymmetric(f32 x, f32 y) {
  UIEdgeInsets result = {
      .set = {true, true, true, true},
      .left = x,
      .right = x,
      .top = y,
      .bottom = y,
  };
  return result;
}

static inline UIEdgeInsets UIEdgeInsetsFromLTRB(f32 left, f32 top, f32 right,
                                                f32 bottom) {
  UIEdgeInsets result = {
      .set = {true, true, true, true},
      .left = left,
      .right = right,
      .top = top,
      .bottom = bottom,
  };
  return result;
}

static inline UIEdgeInsets UIEdgeInsetsFromLT(f32 left, f32 top) {
  UIEdgeInsets result = {
      .set = {true, false, true, false},
      .left = left,
      .right = 0,
      .top = top,
      .bottom = 0,
  };
  return result;
}

static inline UIEdgeInsets UIEdgeInsetsFromRB(f32 right, f32 bottom) {
  UIEdgeInsets result = {
      .set = {false, true, false, true},
      .left = 0,
      .right = right,
      .top = 0,
      .bottom = bottom,
  };
  return result;
}

static inline bool IsUIEdgeInsetsSet(UIEdgeInsets edge_insets) {
  b32 result = edge_insets.set[0] || edge_insets.set[1] || edge_insets.set[2] ||
               edge_insets.set[3];
  return result;
}

static inline bool IsUIEdgeInsetsLeftSet(UIEdgeInsets edge_insets) {
  bool result = edge_insets.set[0];
  return result;
}

static inline bool IsUIEdgeInsetsRightSet(UIEdgeInsets edge_insets) {
  bool result = edge_insets.set[1];
  return result;
}

static inline bool IsUIEdgeInsetsTopSet(UIEdgeInsets edge_insets) {
  bool result = edge_insets.set[2];
  return result;
}

static inline bool IsUIEdgeInsetsBottomSet(UIEdgeInsets edge_insets) {
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
static inline UIBorder UIBorderFromBorderSide(UIBorderSide border_side) {
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
  UIMainAxisAlign main_axis_align;
  UICrossAxisAlign cross_axis_align;

  UIPosition position;
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
  b8 clip;
} UIComputed;

typedef struct UIBoxState {
  const char *type_name;
  void *ptr;
  usize size;
} UIBoxState;

typedef struct UIBox UIBox;
struct UIBox {
  UIID id;
  const char *tag;
  u32 seq;

  // hash links
  UIBox *hash_prev;
  UIBox *hash_next;

  // tree links
  UIBox *first;
  UIBox *last;
  UIBox *prev;
  UIBox *next;
  UIBox *parent;
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
  UIBox *current_box;

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

void InitUI(void);
void QuitUI(void);

// Mouse pos in points.
void OnUIMousePos(Vec2 pos);
void OnUIMouseButtonUp(Vec2 pos, UIMouseButton button);
void OnUIMouseButtonDown(Vec2 pos, UIMouseButton button);
void OnUIMouseWheel(Vec2 delta);

void SetUIDeltaTime(f32 dt);

extern thread_local UIState t_ui_state;

static inline UIState *GetUIState(void) {
  UIState *state = &t_ui_state;
  DEBUG_ASSERT(state->init);
  return state;
}

static inline UIFrame *GetCurrentUIFrame(void) {
  UIState *state = GetUIState();
  UIFrame *result = state->current_frame;
  return result;
}

static inline UIFrame *GetLastUIFrame(void) {
  UIState *state = GetUIState();
  UIFrame *result = state->last_frame;
  return result;
}

static inline f32 GetUIDeltaTime(void) {
  UIState *state = GetUIState();
  return state->input.dt;
}

static inline f32 AnimateUIFastF32(f32 value, f32 target) {
  UIState *state = GetUIState();
  f32 result;
  f32 diff = (target - value);
  if (AbsF32(diff) < 0.0001f) {
    result = target;
    // TODO: Trigger re-draw.
  } else {
    result = value + diff * state->fast_rate;
  }
  return result;
}

static inline ColorU32 AnimateUIFastColorU32(ColorU32 value, ColorU32 target) {
  ColorU32 result;
  result.r = AnimateUIFastF32(value.r, target.r);
  result.g = AnimateUIFastF32(value.g, target.g);
  result.b = AnimateUIFastF32(value.b, target.b);
  result.a = AnimateUIFastF32(value.a, target.a);
  return result;
}

void BeginUIFrame(Vec2 viewport_size);
void EndUIFrame(void);
void RenderUI(void);

UIBuildError *GetFirstUIBuildError(void);

static inline UIID UIIDZero(void) {
  UIID result = {0};
  return result;
}

bool IsEqualUIID(UIID a, UIID b);

static inline bool IsZeroUIID(UIID a) {
  bool result = IsEqualUIID(a, UIIDZero());
  return result;
}

Str8 PushUIStr8(Str8 str);
Str8 PushUIStr8F(const char *fmt, ...);
Str8 PushUIStr8FV(const char *fmt, va_list ap);

void BeginUITag(const char *tag, UIProps props);
void EndUITag(const char *tag);

static inline void BeginUIBox(UIProps props) { BeginUITag("Box", props); }

static inline void EndUIBox(void) { EndUITag("Box"); }

static inline UIBox *GetCurrentUIBox(void) {
  UIFrame *frame = GetCurrentUIFrame();
  DEBUG_ASSERT(frame->current_box);
  UIBox *box = frame->current_box;
  return box;
}

void *PushUIBoxState(const char *type_name, usize size);
void *GetUIBoxState(const char *type_name, usize size);

#define PushUIBoxStruct(Type) (Type *)PushUIBoxState(#Type, sizeof(Type))
#define GetUIBoxStruct(Type) (Type *)GetUIBoxState(#Type, sizeof(Type))

Vec2 GetUIMouseRelPos(void);
Vec2 GetUIMousePos(void);

void SetUIBoxBlockMouseInput(void);
bool IsUIMouseHovering(void);
bool IsUIMouseButtonPressed(UIMouseButton button);
bool IsUIMouseButtonDown(UIMouseButton button);
bool IsUIMouseButtonClicked(UIMouseButton button);
bool IsUIMouseButtonDragging(UIMouseButton button, Vec2 *delta);
bool IsUIMouseScrolling(Vec2 *delta);

#endif  // ZTRACING_SRC_UI_H_
