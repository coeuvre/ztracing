#ifndef ZTRACING_SRC_UI_H_
#define ZTRACING_SRC_UI_H_

#include "src/math.h"
#include "src/string.h"
#include "src/types.h"

#define kUISizeMax F32_MAX
#define kUISizeUndefined 0
#define KUITextSizeDefault 16.0f

typedef struct UIKey {
  u64 hash;
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

typedef struct UIBuildData {
  Str8 key_str;
  ColorU32 color;
  Vec2 size;
  Str8 text;
  Axis2 main_axis;
  f32 flex;
  UIMainAxisSize main_axis_size;
  UIMainAxisAlign main_axis_align;
  UICrossAxisAlign cross_axis_align;
  UIEdgeInsets padding;

  b8 hoverable;
  b8 clickable[kUIMouseButtonCount];
} UIBuildData;

typedef struct UIComputedData {
  Vec2 min_size;
  Vec2 max_size;
  Axis2 unbounded_axis;

  Vec2 size;
  Vec2 rel_pos;

  Rect2 screen_rect;
} UIComputedData;

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
  UIBuildData build;

  // computed every frame
  UIComputedData computed;

  // persistent data
  f32 hot_t;
  f32 active_t;
};

// Mouse pos in points.
void OnUIMousePos(Vec2 pos);
void OnUIMouseButtonUp(Vec2 pos, UIMouseButton button);
void OnUIMouseButtonDown(Vec2 pos, UIMouseButton button);
void OnUIMouseWheel(Vec2 delta);

void SetUIDeltaTime(f32 dt);
f32 GetUIDeltaTime(void);

void BeginUIFrame(Vec2 screen_size, f32 content_scale);
void EndUIFrame(void);
void RenderUI(void);

UIKey UIKeyZero(void);
UIKey UIKeyFromStr8(UIKey seed, Str8 str);
b32 IsEqualUIKey(UIKey a, UIKey b);

void BeginUIBox(void);
void EndUIBox(void);

UIBox *GetUIBoxByKey(UIBox *box, Str8 key);
UIBox *GetUIBox(UIBox *box, u32 index);
UIBox *GetUICurrent(void);

void SetNextUIKey(Str8 key);

void SetUIColor(ColorU32 color);
void SetUISize(Vec2 size);
void SetUIText(Str8 text);

void SetUIMainAxis(Axis2 axis);
void SetUIMainAxisSize(UIMainAxisSize main_axis_size);
void SetUIMainAxisAlign(UIMainAxisAlign main_axis_align);
void SetUICrossAxisAlign(UICrossAxisAlign cross_axis_align);
void SetUIFlex(f32 flex);
void SetUIPadding(UIEdgeInsets padding);

UIComputedData GetUIComputed(void);
Vec2 GetUIMouseRelPos(void);

b32 IsUIMouseHovering(void);
b32 IsUIMouseButtonPressed(UIMouseButton button);
b32 IsUIMouseButtonDown(UIMouseButton button);
b32 IsUIMouseButtonClicked(UIMouseButton button);
b32 IsUIMouseButtonDragging(UIMouseButton button, Vec2 *delta);
b32 IsUIMouseScrolling(Vec2 *delta);

#endif  // ZTRACING_SRC_UI_H_
