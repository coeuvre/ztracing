#ifndef ZTRACING_SRC_UI_H_
#define ZTRACING_SRC_UI_H_

#include "src/math.h"
#include "src/string.h"
#include "src/types.h"

#define kUISizeUndefined 0
#define kUISizeInfinity F32_INFINITY
#define kUIFontSizeDefault 16.0f

typedef struct UIKey {
  u64 hash;
} UIKey;

typedef enum UILayout {
  kUILayoutFlex,
  kUILayoutStack,
} UILayout;

typedef enum UIMainAxisSize {
  kUIMainAxisSizeMin,
  kUIMainAxisSizeMax,
} UIMainAxisSize;

typedef enum UIMainAxisAlign {
  kUIMainAxisAlignStart,
  kUIMainAxisAlignCenter,
  kUIMainAxisAlignEnd,
} UIMainAxisAlign;

typedef enum UICrossAxisAlign {
  kUICrossAxisAlignStart,
  kUICrossAxisAlignCenter,
  kUICrossAxisAlignEnd,
  kUICrossAxisAlignStretch,
} UICrossAxisAlign;

typedef struct UIEdgeInsets {
  f32 start;
  f32 end;
  f32 top;
  f32 bottom;
} UIEdgeInsets;

static inline UIEdgeInsets UIEdgeInsetsAll(f32 val) {
  UIEdgeInsets result;
  result.start = result.end = result.top = result.bottom = val;
  return result;
}

static inline UIEdgeInsets UIEdgeInsetsSymmetric(f32 x, f32 y) {
  UIEdgeInsets result;
  result.start = result.end = x;
  result.top = result.bottom = y;
  return result;
}

static inline UIEdgeInsets UIEdgeInsetsFromSTEB(f32 start, f32 top, f32 end,
                                                f32 bottom) {
  UIEdgeInsets result;
  result.start = start;
  result.end = end;
  result.top = top;
  result.bottom = bottom;
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
  UILayout layout;
  Axis2 main_axis;
  f32 flex;
  UIMainAxisSize main_axis_size;
  UIMainAxisAlign main_axis_align;
  UICrossAxisAlign cross_axis_align;
  UIEdgeInsets padding;
  UIEdgeInsets margin;
  UIBorder border;

  Str8 text;
  ColorU32 color;
  f32 font_size;

  b8 hoverable;
  b8 clickable[kUIMouseButtonCount];
  b8 scrollable;
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

// General data that is persistent across frames until the box is GCed.
typedef struct UIPersistent {
  b8 enabled;
} UIPersistent;

typedef struct UIBox UIBox;
struct UIBox {
  const char *tag;
  UIKey key;

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

  UIProps props;
  UIComputed computed;
  UIPersistent persistent;
};

typedef struct UILayerProps {
  Vec2 min;
  Vec2 max;
} UILayerProps;

typedef struct UILayer UILayer;
struct UILayer {
  UILayer *prev;
  UILayer *next;
  UILayer *parent;

  UIKey key;
  Str8 debug_key;
  UILayerProps props;

  UIBox *root;
  UIBox *current;
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

  UIKey hovering;
  UIKey pressed[kUIMouseButtonCount];
  Vec2 pressed_pos[kUIMouseButtonCount];
  UIKey holding[kUIMouseButtonCount];
  UIKey clicked[kUIMouseButtonCount];
  UIKey scrolling;
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

  UILayer *first_layer;
  UILayer *last_layer;
  UILayer *current_layer;

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
} UIState;

UIState *GetUIState(void);

void InitUI(void);
void QuitUI(void);

// Mouse pos in points.
void OnUIMousePos(Vec2 pos);
void OnUIMouseButtonUp(Vec2 pos, UIMouseButton button);
void OnUIMouseButtonDown(Vec2 pos, UIMouseButton button);
void OnUIMouseWheel(Vec2 delta);

void SetUIDeltaTime(f32 dt);
f32 GetUIDeltaTime(void);

void BeginUIFrame(void);
void EndUIFrame(void);
void RenderUI(void);

void BeginUILayer(UILayerProps props, const char *fmt, ...);
void EndUILayer(void);

UIBuildError *GetFirstUIBuildError(void);

static inline UIKey UIKeyZero(void) {
  UIKey result = {0};
  return result;
}

b32 IsEqualUIKey(UIKey a, UIKey b);

static inline b32 IsZeroUIKey(UIKey a) {
  b32 result = IsEqualUIKey(a, UIKeyZero());
  return result;
}

Str8 PushUIStr8(Str8 str);
Str8 PushUIStr8F(const char *fmt, ...);
Str8 PushUIStr8FV(const char *fmt, va_list ap);

void BeginUITag(const char *tag, UIProps props);
void EndUITag(const char *tag);

static inline void BeginUIBox(UIProps props) { BeginUITag("Box", props); }
static inline void EndUIBox(void) { EndUITag("Box"); }

UIBox *GetCurrentUIBox(void);

UIComputed GetUIComputed(void);
UIPersistent *GetUIPersistent(void);

Vec2 GetUIMouseRelPos(void);
Vec2 GetUIMousePos(void);

b32 IsUIMouseHovering(void);
b32 IsUIMouseButtonPressed(UIMouseButton button);
b32 IsUIMouseButtonDown(UIMouseButton button);
b32 IsUIMouseButtonClicked(UIMouseButton button);
b32 IsUIMouseButtonDragging(UIMouseButton button, Vec2 *delta);
b32 IsUIMouseScrolling(Vec2 *delta);

#endif  // ZTRACING_SRC_UI_H_
