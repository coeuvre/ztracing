#ifndef ZTRACING_SRC_UI_H_
#define ZTRACING_SRC_UI_H_

#include "src/math.h"
#include "src/string.h"
#include "src/types.h"

#define UI_SIZE_MAX F32_MAX

typedef struct UIKey {
  u64 hash;
} UIKey;

typedef enum UIAlign {
  kUIAlignStart,
  kUIAlignEnd,
  kUIAlignCenter,
} UIAlign;

typedef struct UIBuildData {
  u32 color;
  Vec2 size;
  Str8 text;
  Axis2 main_axis;
  UIAlign aligns[kAxis2Count];
} UIBuildData;

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
  Vec2 computed_size;
  Vec2 computed_rel_pos;
  Rect2 computed_screen_rect;

  // persistent data
  f32 hot_t;
  f32 active_t;
};

void UIBeginFrame(void);
void UIEndFrame(void);

UIKey UIKeyZero(void);
UIKey UIKeyFromStr8(UIKey seed, Str8 str);
b32 UIKeyEqual(UIKey a, UIKey b);

void UIBeginBox(Str8 key);
void UIEndBox(void);

void UISetColor(u32 color);
void UISetSize(Vec2 size);
void UISetText(Str8 text);

void UISetMainAxis(Axis2 axis);
void UISetMainAxisAlignment(UIAlign align);
void UISetCrossAxisAlignment(UIAlign align);

#endif  // ZTRACING_SRC_UI_H_
