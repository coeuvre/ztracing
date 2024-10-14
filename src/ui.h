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
  Vec2 size;
  Axis2 main_axis;
  f32 flex;
  UIMainAxisSize main_axis_size;
  UIMainAxisAlign main_axis_align;
  UICrossAxisAlign cross_axis_align;
  UIEdgeInsets padding;
  UIEdgeInsets margin;

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
  b8 clip;
} UIComputed;

typedef struct UIBuildError UIBuildError;
struct UIBuildError {
  UIBuildError *prev;
  UIBuildError *next;
  Str8 message;
};

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

  // key + generation
  UIKey key;
  u64 last_touched_build_index;

  // per-frame info provided by builders
  UIProps props;

  // computed every frame
  UIComputed computed;

  // persistent data
  f32 hot_t;
  f32 active_t;
};

typedef struct UIKeyNode UIKeyNode;
struct UIKeyNode {
  UIKeyNode *prev;
  UIKeyNode *next;
  UIKey key;
};

typedef struct UILayer UILayer;
struct UILayer {
  UILayer *prev;
  UILayer *next;
  UILayer *parent;
  UIKey key;

  UIKeyNode *first_key;
  UIKeyNode *last_key;
  UIKeyNode *first_free_key;

  UIBox *root;
  UIBox *current;
};

void InitUI(void);
void QuitUI(void);

// Mouse pos in points.
void OnUIMousePos(Vec2 pos);
void OnUIMouseButtonUp(Vec2 pos, UIMouseButton button);
void OnUIMouseButtonDown(Vec2 pos, UIMouseButton button);
void OnUIMouseWheel(Vec2 delta);

void SetUIDeltaTime(f32 dt);
f32 GetUIDeltaTime(void);

void BeginUIFrame(Vec2 screen_size);
void EndUIFrame(void);
void RenderUI(void);

void BeginUILayer(const char *fmt, ...);
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
Vec2 GetUIMouseRelPos(UIKey key);

b32 IsUIMouseHovering(UIKey key);
b32 IsUIMouseButtonPressed(UIKey key, UIMouseButton button);
b32 IsUIMouseButtonDown(UIKey key, UIMouseButton button);
b32 IsUIMouseButtonClicked(UIKey key, UIMouseButton button);
b32 IsUIMouseButtonDragging(UIKey key, UIMouseButton button, Vec2 *delta);
b32 IsUIMouseScrolling(UIKey key, Vec2 *delta);

#endif  // ZTRACING_SRC_UI_H_
