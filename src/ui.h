#ifndef ZTRACING_SRC_UI_H_
#define ZTRACING_SRC_UI_H_

#include "src/math.h"
#include "src/string.h"
#include "src/types.h"

#define kUISizeMax F32_MAX
#define kUISizeUndefined 0

typedef struct UIKey {
  u64 hash;
} UIKey;

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

typedef struct UIBuildData {
  Str8 key_str;
  ColorU32 color;
  Vec2 size;
  Str8 text;
  Axis2 main_axis;
  f32 flex;
  UIMainAxisAlign main_axis_align;
  UICrossAxisAlign cross_axis_align;
} UIBuildData;

typedef struct UIComputedData {
  Vec2 min_size;
  Vec2 max_size;
  b32 unbounded;

  Vec2 size;
  Vec2 rel_pos;
  Vec2 text_size;

  Rect2 screen_rect_in_pixel;
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

void BeginUIFrame(Vec2 screen_size_in_pixel, f32 content_scale);
void EndUIFrame(void);
void RenderUI(void);

UIKey UIKeyZero(void);
UIKey UIKeyFromStr8(UIKey seed, Str8 str);
b32 IsEqualUIKey(UIKey a, UIKey b);

void BeginUIBox(Str8 key);
void EndUIBox(void);

UIBox *GetUIRoot(void);
UIBox *GetUIChild(UIBox *box, Str8 key);

void SetUIColor(ColorU32 color);
void SetUISize(Vec2 size);
void SetUIText(Str8 text);

void SetUIMainAxis(Axis2 axis);
void SetUIMainAxisAlign(UIMainAxisAlign main_axis_align);
void SetUICrossAxisAlign(UICrossAxisAlign cross_axis_align);
void SetUIFlex(f32 flex);

#endif  // ZTRACING_SRC_UI_H_
