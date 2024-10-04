#ifndef ZTRACING_SRC_UI_H_
#define ZTRACING_SRC_UI_H_

#include "src/math.h"
#include "src/string.h"
#include "src/types.h"

typedef struct WidgetKey {
  u64 hash;
} WidgetKey;

typedef enum UIAlign {
  kUIAlignStart,
  kUIAlignEnd,
  kUIAlignCenter,
} UIAlign;

typedef struct UIWidgetBuildData {
  u32 color;
  Vec2 size;
  Str8 text;
  Axis2 main_axis;
  UIAlign aligns[kAxis2Count];
} UIWidgetBuildData;

typedef struct Widget Widget;
struct Widget {
  // hash links
  Widget *hash_prev;
  Widget *hash_next;

  // tree links
  Widget *first;
  Widget *last;
  Widget *prev;
  Widget *next;
  Widget *parent;

  // key + generation
  WidgetKey key;
  u64 last_touched_build_index;

  // per-frame info provided by builders
  UIWidgetBuildData build;

  // computed every frame
  Vec2 computed_size;
  Vec2 computed_rel_pos;
  Rect2 computed_screen_rect;

  // persistent data
  f32 hot_t;
  f32 active_t;
};

void BeginUI(void);
void EndUI(void);

WidgetKey WidgetKeyZero(void);
WidgetKey WidgetKeyFromStr8(WidgetKey seed, Str8 str);
b32 EqualWidgetKey(WidgetKey a, WidgetKey b);

void BeginWidget(Str8 key);
void EndWidget(void);

void SetWidgetColor(u32 color);
void SetWidgetSize(Vec2 size);
void SetWidgetText(Str8 text);

void UISetMainAxis(Axis2 axis);
void UISetMainAxisAlignment(UIAlign align);
void UISetCrossAxisAlignment(UIAlign align);

#endif  // ZTRACING_SRC_UI_H_
