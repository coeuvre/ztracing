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
  Str8 str;
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

typedef struct UIProps {
  UIKey key;

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

typedef struct UIComputed {
  const char *tag;

  Vec2 min_size;
  Vec2 max_size;

  Vec2 size;
  Vec2 rel_pos;

  f32 font_size;

  Rect2 screen_rect;
  Rect2 clip_rect;
  b8 clip;
} UIComputed;

typedef struct UIBuildError UIBuildError;
struct UIBuildError {
  UIBuildError *prev;
  UIBuildError *next;
  Str8 message;
};

// General data that is persistent across frames until the box is GCed.
typedef struct UIPersistent {
  b8 enabled;
} UIPersistent;

typedef struct UIBox UIBox;
struct UIBox {
  // hash links
  UIBox *hash_prev;
  UIBox *hash_next;

  // tree links
  UIBox *first;
  UIBox *last;
  UIBox *prev;
  UIBox *next;
  UIBox *parent;

  // generation
  u64 last_touched_build_index;

  // per-frame info provided by builders
  UIProps props;

  // computed every frame
  UIComputed computed;

  // persistent data for the life time of the box
  UIPersistent persistent;
};

typedef struct UIKeyNode UIKeyNode;
struct UIKeyNode {
  UIKeyNode *prev;
  UIKeyNode *next;
  UIKey key;
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
  UILayerProps props;

  UIKeyNode *first_key;
  UIKeyNode *last_key;
  UIKeyNode *first_free_key;

  UIBox *root;
  UIBox *current;
};

typedef struct BoxHashSlot BoxHashSlot;
struct BoxHashSlot {
  BoxHashSlot *prev;
  BoxHashSlot *next;
  UIBox *first;
  UIBox *last;
};

typedef struct UIBoxCache {
  u32 total_box_count;
  // Free list for boxes
  UIBox *first_free_box;
  // Hash slots for box hash table
  u32 box_hash_slots_count;
  BoxHashSlot *box_hash_slots;
  // Linked list for non-empty box hash slots
  BoxHashSlot *first_hash_slot;
  BoxHashSlot *last_hash_slot;
} UIBoxCache;

typedef struct UIMouseButtonState {
  b8 is_down;
  b8 transition_count;
} UIMouseButtonState;

typedef struct UIMouseInput {
  Vec2 pos;
  Vec2 wheel;
  UIMouseButtonState buttons[kUIMouseButtonCount];

  UIBox *hovering;
  UIBox *pressed[kUIMouseButtonCount];
  Vec2 pressed_pos[kUIMouseButtonCount];
  UIBox *holding[kUIMouseButtonCount];
  UIBox *clicked[kUIMouseButtonCount];
  UIBox *scrolling;
  Vec2 scroll_delta;
} UIMouseInput;

typedef struct UIInput {
  f32 dt;
  UIMouseInput mouse;
} UIInput;

typedef struct UIState {
  Arena arena;

  UIBoxCache cache;
  UIInput input;

  Arena build_arena[2];
  u64 build_index;

  // per-frame info
  UILayer *first_layer;
  UILayer *last_layer;
  UILayer *current_layer;

  UIBuildError *first_error;
  UIBuildError *last_error;
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

UIKey PushUIKey(Str8 key_str);
UIKey PushUIKeyF(const char *fmt, ...);
UIKey PushUIKeyFV(const char *fmt, va_list ap);

Str8 PushUIText(Str8 key_str);
Str8 PushUITextF(const char *fmt, ...);
Str8 PushUITextFV(const char *fmt, va_list ap);

void BeginUIBoxWithTag(const char *tag, UIProps props);
void EndUIBoxWithExpectedTag(const char *tag);

static inline void BeginUIBox(UIProps props) {
  BeginUIBoxWithTag("Box", props);
}
static inline void EndUIBox(void) { EndUIBoxWithExpectedTag("Box"); }

UIBox *GetUIBox(UIKey key);

UIComputed GetUIComputed(UIKey key);
UIPersistent *GetUIPersistent(UIKey key);

Vec2 GetUIMouseRelPos(UIKey key);
Vec2 GetUIMousePos(void);

b32 IsUIMouseHovering(UIKey key);
b32 IsUIMouseButtonPressed(UIKey key, UIMouseButton button);
b32 IsUIMouseButtonDown(UIKey key, UIMouseButton button);
b32 IsUIMouseButtonClicked(UIKey key, UIMouseButton button);
b32 IsUIMouseButtonDragging(UIKey key, UIMouseButton button, Vec2 *delta);
b32 IsUIMouseScrolling(UIKey key, Vec2 *delta);

#endif  // ZTRACING_SRC_UI_H_
