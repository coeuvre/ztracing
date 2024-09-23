#pragma once

typedef struct WidgetKey WidgetKey;
struct WidgetKey {
    u64 hash;
};

typedef enum WidgetFlags WidgetFlags;
enum WidgetFlags {};

typedef enum WidgetConstraintType WidgetConstraintType;
enum WidgetConstraintType {
    WidgetConstraint_Null,
    WidgetConstraint_Pixels,
    WidgetConstraint_TextContent,
    WidgetConstraint_PercentOfParent,
    WidgetConstraint_ChildrenSum,
};

typedef struct WidgetConstraint WidgetConstraint;
struct WidgetConstraint {
    WidgetConstraintType type;
    f32 value;
    f32 strickness;
};

typedef struct Widget Widget;
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
    WidgetConstraint constraints[Axis2_COUNT];

    // computed every frame
    f32 computed_rel_position[Axis2_COUNT];
    f32 computed_size[Axis2_COUNT];
    Rect2 rect;

    // persistent data
    f32 hot_t;
    f32 active_t;
};

static void begin_ui();
static void end_ui();

static WidgetKey widget_key_zero(void);
static WidgetKey widget_key_from_str8(WidgetKey seed, Str8 str);
static b32 equal_widget_key(WidgetKey a, WidgetKey b);

static void begin_widget_str8(Str8 id);
static void end_widget(void);
