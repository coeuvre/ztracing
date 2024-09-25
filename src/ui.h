#ifndef ZTRACING_SRC_UI_H_
#define ZTRACING_SRC_UI_H_

#include "src/math.h"
#include "src/string.h"
#include "src/types.h"

struct WidgetKey {
  u64 hash;
};

enum WidgetFlags {};

enum WidgetConstraintType {
  kWidgetConstraintNull,
  kWidgetConstraintPixels,
  kWidgetConstraintTextContent,
  kWidgetConstraintPercentOfParent,
  kWidgetConstraintChildrenSum,
};

struct WidgetConstraint {
  WidgetConstraintType type;
  f32 value;
  f32 strickness;
};

struct Widget {
  // tree links
  Widget *first;
  Widget *last;
  Widget *prev;
  Widget *next;
  Widget *parent;

  // hash links
  Widget *hash_prev;
  Widget *hash_next;

  // key + generation
  WidgetKey key;
  u64 last_touched_frame_index;

  // per-frame info provided by builders
  WidgetFlags flags;
  Str8 string;
  WidgetConstraint constraints[kAxis2Count];

  // computed every frame
  f32 computed_rel_position[kAxis2Count];
  f32 computed_size[kAxis2Count];
  Rect2 rect;

  // persistent data
  f32 hot_t;
  f32 active_t;
};

void BeginUI();
void EndUI();

WidgetKey WidgetKeyZero(void);
WidgetKey WidgetKeyFromStr8(WidgetKey seed, Str8 str);
bool EqualWidgetKey(WidgetKey a, WidgetKey b);

void BeginWidget(Str8 id);
void EndWidget(void);

#endif  // ZTRACING_SRC_UI_H_