#ifndef ZTRACING_SRC_UI_H_
#define ZTRACING_SRC_UI_H_

#include "src/math.h"
#include "src/string.h"
#include "src/types.h"

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

typedef enum WidgetType WidgetType;
enum WidgetType {
  kWidgetUnknown,

  kWidgetContainer,
  kWidgetCenter,
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
  WidgetType type;
  u32 color;
  Vec2 size;
  Str8 text;

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

b32 MissNextWidgetKey(void);
Str8 GetNextWidgetKey(void);
void SetNextWidgetKey(Str8 key);

void BeginWidget(Str8 key, WidgetType type);
void EndWidget(void);

WidgetType GetWidgetType(void);
void SetWidgetColor(u32 color);
void SetWidgetSize(Vec2 size);

#endif  // ZTRACING_SRC_UI_H_
