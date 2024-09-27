#ifndef ZTRACING_SRC_UI_H_
#define ZTRACING_SRC_UI_H_

#include "src/math.h"
#include "src/string.h"
#include "src/types.h"

typedef struct TextMetrics TextMetrics;
struct TextMetrics {
  Vec2 size;
};

TextMetrics GetTextMetricsStr8(Str8 text, f32 height);
void DrawTextStr8(Vec2 pos, Str8 text, f32 height);
void DrawRect(Vec2 min, Vec2 max, u32 color);

typedef struct WidgetKey WidgetKey;
struct WidgetKey {
  u64 hash;
};

typedef enum WidgetConstraintType WidgetConstraintType;
enum WidgetConstraintType {
  kWidgetConstraintNull,
  kWidgetConstraintPixels,
  kWidgetConstraintTextContent,
  kWidgetConstraintPercentOfParent,
  kWidgetConstraintChildrenSum,
};

typedef struct WidgetConstraint WidgetConstraint;
struct WidgetConstraint {
  WidgetConstraintType type;
  f32 value;
  f32 strickness;
};

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
  u32 flags;
  Str8 text;
  WidgetConstraint constraints[kAxis2Count];

  // computed every frame
  f32 computed_rel_position[kAxis2Count];
  f32 computed_size[kAxis2Count];
  Rect2 screen_rect;

  // persistent data
  f32 hot_t;
  f32 active_t;
};

void BeginUI(void);
void EndUI(void);

WidgetKey WidgetKeyZero(void);
WidgetKey WidgetKeyFromStr8(WidgetKey seed, Str8 str);
b32 EqualWidgetKey(WidgetKey a, WidgetKey b);

void BeginWidget(Str8 id);
void EndWidget(void);

void SetNextWidgetConstraint(Axis2 axis, WidgetConstraint constraint);
void SetNextWidgetTextContent(Str8 text);

void TextLine(Str8 text);

#endif  // ZTRACING_SRC_UI_H_
