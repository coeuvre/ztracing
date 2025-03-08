#include "src/ui.h"

#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>

#include "src/assert.h"
#include "src/draw.h"
#include "src/hash_trie.h"
#include "src/list.h"
#include "src/math.h"
#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"

typedef struct UIWidgetListNode UIWidgetListNode;
struct UIWidgetListNode {
  UIWidgetListNode *prev;
  UIWidgetListNode *next;
  UIWidget *widget;
};

typedef struct UIWidgetList {
  UIWidgetListNode *first;
  UIWidgetListNode *last;
} UIWidgetList;

static void ui_widget_list_add_last(UIWidgetList *self, Arena *arena,
                                    UIWidget *widget) {
  UIWidgetListNode *node = arena_push_struct(arena, UIWidgetListNode);
  node->widget = widget;
  DLL_APPEND(self->first, self->last, node, prev, next);
}

static UIWidgetListNode *ui_widget_list_remove_first(UIWidgetList *self) {
  if (!self->first) {
    return 0;
  }

  UIWidgetListNode *node = self->first;
  DLL_REMOVE(self->first, self->last, node, prev, next);
  return node;
}

typedef struct UIWidgetHashNode UIWidgetHashNode;
struct UIWidgetHashNode {
  UIWidgetHashNode *prev;
  UIWidgetHashNode *next;
};

typedef struct UIWidgetHashSlot UIWidgetHashSlot;
struct UIWidgetHashSlot {
  UIWidgetHashNode *first;
  UIWidgetHashNode *last;
};

typedef struct UIWidgetHashMap {
  u32 total_count;
  u32 slots_count;
  UIWidgetHashSlot *slots;
} UIWidgetHashMap;

static void ui_widget_hash_map_put(UIWidgetHashMap *map, Arena *arena,
                                   UIWidget *widget) {
  UIKey key = widget->key;
  UIWidgetHashNode *node = arena_push(arena, sizeof(UIWidgetHashNode), 0);
  UIWidgetHashSlot *slot = map->slots + (key.hash % map->slots_count);
  DLL_APPEND(slot->first, slot->last, node, prev, next);
  map->total_count += 1;
}

static UIWidget *ui_widget_hash_map_get(UIWidgetHashMap *map, UIKey key) {
  UIWidget *result = 0;
  if (!ui_key_is_zero(key) && map->slots) {
    UIWidgetHashSlot *slot = map->slots + (key.hash % map->slots_count);
    for (UIWidgetHashNode *node = slot->first; node; node = node->next) {
      UIWidget *widget = (UIWidget *)(node + 1);
      if (ui_key_eq(widget->key, key)) {
        result = widget;
        break;
      }
    }
  }
  return result;
}

typedef struct UIWidgetStackEntry UIWidgetStackEntry;
struct UIWidgetStackEntry {
  UIWidgetStackEntry *prev;
  UIWidgetStackEntry *next;

  UIWidget *widget;
  UIWidget *last_child;
};

typedef struct UIWidgetStack {
  UIWidgetStackEntry *first;
  UIWidgetStackEntry *last;

  UIWidgetStackEntry *first_free;
  UIWidgetStackEntry *last_free;
} UIWidgetStack;

static bool ui_widget_stack_is_empty(UIWidgetStack *stack) {
  return !stack->first;
}

static void ui_widget_stack_push(UIWidgetStack *stack, UIWidget *widget,
                                 Arena *arena) {
  UIWidgetStackEntry *entry = stack->last_free;
  if (entry) {
    DLL_REMOVE(stack->first_free, stack->last_free, entry, prev, next);
  } else {
    entry = arena_push_struct(arena, UIWidgetStackEntry);
  }
  entry->widget = widget;
  entry->last_child = widget->doppelganger ? widget->doppelganger->first : 0;
  DLL_APPEND(stack->first, stack->last, entry, prev, next);
}

static UIWidget *ui_widget_stack_pop(UIWidgetStack *stack) {
  ASSERTF(stack->last, "mismatched begin/end calls, widget stack is empty");
  UIWidgetStackEntry *entry = stack->last;
  DLL_REMOVE(stack->first, stack->last, entry, prev, next);
  DLL_APPEND(stack->first_free, stack->last_free, entry, prev, next);
  return entry->widget;
}

typedef struct UIFrame {
  Arena arena;
  UIWidgetStack stack;
  UIWidget *root;
  bool open;
} UIFrame;

typedef struct UIHitTestState {
  Arena arena;
  UIHitTestResult result;
} UIHitTestState;

static void ui_hit_test_state_clear(UIHitTestState *self) {
  self->result = (UIHitTestResult){0};
  arena_clear(&self->arena);
}

/// Update widget references in this hit test path from old widget tree to the
/// new widget tree.
static void ui_hit_test_state_sync(UIHitTestState *self) {
  UIHitTestEntry *entry = self->result.last;
  for (; entry && entry->widget; entry = entry->prev) {
    entry->widget = entry->widget->doppelganger;
    if (!entry->widget) {
      break;
    }
  }

  for (; entry; entry = entry->prev) {
    entry->widget = 0;
  }
}

typedef struct UIGestureArenaMember UIGestureArenaMember;
struct UIGestureArenaMember {
  UIGestureArenaMember *prev;
  UIGestureArenaMember *next;
  UIGestureRecognizer recognizer;
};

typedef struct UIGestureArena UIGestureArena;
struct UIGestureArena {
  UIGestureArena *prev;
  UIGestureArena *next;
  u32 pointer;
  UIGestureArenaMember *first_member;
  UIGestureArenaMember *last_member;
};

static inline void ui_gesture_arena_add_member(UIGestureArena *self,
                                               UIGestureArenaMember *member) {
  DLL_APPEND(self->first_member, self->last_member, member, prev, next);
}

typedef struct UIGestureArenaState {
  Arena arena;

  UIGestureArena *first_arena;
  UIGestureArena *last_arena;
  UIGestureArena *first_free_arena;
  UIGestureArena *last_free_arena;

  UIGestureArenaMember *first_free_member;
  UIGestureArenaMember *last_free_member;
} UIGestureArenaState;

typedef struct UIPointerDownState {
  u32 pointer;
  u32 button;
} UIPointerDownState;

typedef struct UIInputState {
  Arena arena;

  f32 dt;

  UIPointerDownState pointer_down_state;
  u32 prev_pointer;

  // hit test state for button down.
  UIHitTestState button_down_hit_test;
  // hit test result for mouse move
  UIHitTestState button_move_hit_tests[2];
  usize button_move_hit_test_index;

  UIGestureArenaState gesture_arena_state;
} UIInputState;

typedef struct UIState {
  UIFrame frames[2];
  u64 frame_index;
  UIFrame *current_frame;
  UIFrame *last_frame;

  bool should_rebuild;

  Vec2 viewport_min;
  Vec2 viewport_max;

  UIInputState input;
  f32 fast_animation_rate;
} UIState;

THREAD_LOCAL UIState t_ui_state;

static inline UIState *ui_state_get(void) { return &t_ui_state; }

static inline UIFrame *ui_state_get_current_frame(UIState *state) {
  UIFrame *frame = state->current_frame;
  if (!frame) {
    frame = state->frames;
  }
  return frame;
}

void ui_set_viewport(Vec2 min, Vec2 max) {
  UIState *state = ui_state_get();
  state->viewport_min = min;
  state->viewport_max = max;
}

void ui_set_delta_time(f32 dt) {
  UIState *state = ui_state_get();
  state->input.dt = dt;
  state->fast_animation_rate = 1.0f - f32_exp(-50.f * dt);
}

void ui_set_rebuild(bool should_rebuild) {
  UIState *state = ui_state_get();
  state->should_rebuild = should_rebuild;
}

static void ui_cleanup_last_frame_widget(UIWidget *widget) {
  for (UIWidget *child = widget->first; child; child = child->next) {
    ui_cleanup_last_frame_widget(child);
  }
  widget->doppelganger = 0;
}

void ui_begin_frame(void) {
  UIState *state = ui_state_get();

  if (state->input.dt == 0) {
    // Assume 60 FPS if `dt` is not explicitly set.
    ui_set_delta_time(1000.0f / 60.0f);
  }

  state->frame_index += 1;
  state->current_frame =
      state->frames + (state->frame_index % ARRAY_COUNT(state->frames));
  state->last_frame =
      state->frames + ((state->frame_index - 1) % ARRAY_COUNT(state->frames));

  UIFrame *frame = state->current_frame;
  arena_clear(&frame->arena);
  frame->stack = (UIWidgetStack){0};
  frame->root = 0;
  frame->open = true;

  state->should_rebuild = false;

  if (state->last_frame->root) {
    ui_cleanup_last_frame_widget(state->last_frame->root);
  }
}

static inline void ui_widget_mount(UIWidget *widget) {
  ASSERT(!widget->doppelganger);
  if (widget->klass->mount) {
    widget->klass->mount(widget);
  }
}

static inline void ui_widget_unmount(UIWidget *widget) {
  ASSERT(!widget->doppelganger);
  if (widget->klass->unmount) {
    widget->klass->unmount(widget);
  }
}

static void ui_widget_layout_default(UIWidget *widget,
                                     UIBoxConstraints constraints);

static inline void ui_widget_layout(UIWidget *widget,
                                    UIBoxConstraints constraints) {
  if (widget->klass->layout) {
    widget->klass->layout(widget, constraints);
  } else {
    ui_widget_layout_default(widget, constraints);
  }
}

static inline void ui_widget_layout_sliver(
    UIWidget *widget, const UISliverConstraints *constraints,
    UISliverGeometry *geometry) {
  if (widget->klass->layout_sliver) {
    widget->klass->layout_sliver(widget, constraints, geometry);
  } else {
    DEBUG_ASSERTF(false, "layout_sliver is not implemented for %s",
                  widget->klass->name);
  }
}

static void ui_widget_paint_default(UIWidget *widget,
                                    UIPaintingContext *context, Vec2 offset);

static inline void ui_widget_paint(UIWidget *widget, UIPaintingContext *context,
                                   Vec2 offset) {
  if (widget->klass->paint) {
    widget->klass->paint(widget, context, offset);
  } else {
    ui_widget_paint_default(widget, context, offset);
  }
}

static bool ui_widget_hit_test_defer_to_children(UIWidget *widget,
                                                 UIHitTestResult *result,
                                                 Arena *arena);

static inline bool ui_widget_hit_test(UIWidget *widget, UIHitTestResult *result,
                                      Arena *arena) {
  if (widget->klass->hit_test) {
    return widget->klass->hit_test(widget, result, arena);
  } else {
    return ui_widget_hit_test_defer_to_children(widget, result, arena);
  }
}

static inline void ui_widget_handle_pointer_event(UIWidget *widget,
                                                  UIPointerEvent *event) {
  if (widget->klass->handle_pointer_event) {
    widget->klass->handle_pointer_event(widget, event);
  }
}

static UIWidget *ui_state_get_root_for_hit_test(UIState *self) {
  if (!self->current_frame) {
    return 0;
  }

  if (!self->current_frame->open) {
    return self->current_frame->root;
  }

  return self->last_frame->root;
}

static bool ui_hit_test_state_has_widget(UIHitTestState *self,
                                         UIWidget *widget) {
  // TODO: Use hash map to speed up lookup.
  bool result = false;
  for (UIHitTestEntry *entry = self->result.first; entry; entry = entry->next) {
    if (entry->widget == widget) {
      result = true;
      break;
    }
  }
  return result;
}

/// Returns true if hit any widget.
static void ui_hit_test_state_hit_test(UIHitTestState *self, UIWidget *root,
                                       Vec2 pos) {
  ASSERT(!self->result.first);
  if (!root) {
    return;
  }

  self->result.position = pos;
  self->result.transform = ui_transform_identity();
  self->result.local_position = pos;
  bool hit = ui_widget_hit_test(root, &self->result, &self->arena);
  DEBUG_ASSERT(vec2_eq(self->result.local_position, pos));
  if (!hit) {
    ui_hit_test_state_clear(self);
  }
}

void ui_on_mouse_button_down(Vec2 pos, u32 button) {
  UIState *state = ui_state_get();
  UIInputState *input = &state->input;

  UIPointerDownState *down = &input->pointer_down_state;
  down->button |= button;

  // TODO: Multiple touch?
  if (down->pointer) {
    // If there is already button down, treat this as MOVE event.
    ui_on_mouse_move(pos);
  } else {
    down->pointer = ++input->prev_pointer;

    ASSERT(down->button == button);

    ui_hit_test_state_hit_test(&state->input.button_down_hit_test,
                               ui_state_get_root_for_hit_test(state), pos);
    for (UIHitTestEntry *entry = state->input.button_down_hit_test.result.first;
         entry; entry = entry->next) {
      ui_widget_handle_pointer_event(
          entry->widget, &(UIPointerEvent){
                             .type = UI_POINTER_EVENT_DOWN,
                             .pointer = down->pointer,
                             .button = down->button,
                             .position = pos,
                             .transform = entry->transform,
                             .local_position = entry->local_position,
                         });
    }
  }
}

void ui_on_mouse_button_up(Vec2 pos, u32 button) {
  UIState *state = ui_state_get();
  UIInputState *input = &state->input;
  UIPointerDownState *down = &input->pointer_down_state;
  down->button &= (~button);
  if (!down->button) {
    ASSERT(down->pointer);
    // Only send UP event when no button is down.
    for (UIHitTestEntry *entry = state->input.button_down_hit_test.result.first;
         entry; entry = entry->next) {
      if (entry->widget) {
        ui_widget_handle_pointer_event(
            entry->widget,
            &(UIPointerEvent){
                .type = UI_POINTER_EVENT_UP,
                .pointer = down->pointer,
                .button = button,
                .position = pos,
                .transform = entry->transform,
                .local_position = ui_transform_dot_vec2(entry->transform, pos),
            });
      }
    }

    ui_hit_test_state_clear(&state->input.button_down_hit_test);
    down->pointer = 0;
  } else {
    ui_on_mouse_move(pos);
  }
}

void ui_on_mouse_move(Vec2 pos) {
  UIState *state = ui_state_get();
  UIInputState *input = &state->input;
  UIPointerDownState *down = &input->pointer_down_state;

  UIWidget *root = ui_state_get_root_for_hit_test(state);

  // Handle ENTER/EXIT event
  {
    UIHitTestState *last_hit_test =
        input->button_move_hit_tests + input->button_move_hit_test_index;
    input->button_move_hit_test_index =
        (input->button_move_hit_test_index + 1) %
        ARRAY_COUNT(input->button_move_hit_tests);
    UIHitTestState *hit_test =
        input->button_move_hit_tests + input->button_move_hit_test_index;
    ui_hit_test_state_hit_test(hit_test, root, pos);

    for (UIHitTestEntry *entry = last_hit_test->result.first;
         entry && entry->widget; entry = entry->next) {
      if (!ui_hit_test_state_has_widget(hit_test, entry->widget)) {
        ui_widget_handle_pointer_event(
            entry->widget,
            &(UIPointerEvent){
                .type = UI_POINTER_EVENT_EXIT,
                .pointer = down->pointer,
                .button = down->button,
                .position = pos,
                .transform = entry->transform,
                .local_position = ui_transform_dot_vec2(entry->transform, pos),
            });
      }
    }

    for (UIHitTestEntry *entry = hit_test->result.first; entry;
         entry = entry->next) {
      if (!ui_hit_test_state_has_widget(last_hit_test, entry->widget)) {
        ui_widget_handle_pointer_event(
            entry->widget, &(UIPointerEvent){
                               .type = UI_POINTER_EVENT_ENTER,
                               .pointer = down->pointer,
                               .button = down->button,
                               .position = pos,
                               .transform = entry->transform,
                               .local_position = entry->local_position,
                           });
      }
    }

    ui_hit_test_state_clear(last_hit_test);
  }

  if (down->pointer) {
    for (UIHitTestEntry *entry = input->button_down_hit_test.result.first;
         entry; entry = entry->next) {
      if (entry->widget) {
        ui_widget_handle_pointer_event(
            entry->widget,
            &(UIPointerEvent){
                .type = UI_POINTER_EVENT_MOVE,
                .pointer = down->pointer,
                .button = down->button,
                .position = pos,
                .transform = entry->transform,
                .local_position = ui_transform_dot_vec2(entry->transform, pos),
            });
      }
    }
    return;
  } else {
    // Only send HOVER events if there isn't button down.
    ui_hit_test_state_hit_test(&state->input.button_down_hit_test, root, pos);
    for (UIHitTestEntry *entry = state->input.button_down_hit_test.result.first;
         entry; entry = entry->next) {
      ui_widget_handle_pointer_event(
          entry->widget, &(UIPointerEvent){
                             .type = UI_POINTER_EVENT_HOVER,
                             .button = down->button,
                             .position = pos,
                             .transform = entry->transform,
                             .local_position = entry->local_position,
                         });
    }
    ui_hit_test_state_clear(&state->input.button_down_hit_test);
  }
}

void ui_on_mouse_scroll(Vec2 pos, Vec2 delta) {
  UIState *state = ui_state_get();
  UIWidget *root = ui_state_get_root_for_hit_test(state);
  if (!root) {
    return;
  }

  Scratch scratch = scratch_begin(0, 0);
  UIHitTestResult result = {0};
  result.position = pos;
  result.transform = ui_transform_identity();
  result.local_position = pos;
  if (ui_widget_hit_test(root, &result, scratch.arena)) {
    for (UIHitTestEntry *entry = result.first; entry; entry = entry->next) {
      ui_widget_handle_pointer_event(
          entry->widget, &(UIPointerEvent){
                             .type = UI_POINTER_EVENT_SCROLL,
                             .position = pos,
                             .transform = entry->transform,
                             .local_position = entry->local_position,
                             .scroll_delta = delta,
                         });
    }
  }
  scratch_end(scratch);
}

void ui_on_focus_lost(Vec2 pos) {
  UIState *state = ui_state_get();

  if (!state->input.button_down_hit_test.result.first) {
    return;
  }

  for (UIHitTestEntry *entry = state->input.button_down_hit_test.result.first;
       entry; entry = entry->next) {
    if (entry->widget) {
      ui_widget_handle_pointer_event(
          entry->widget,
          &(UIPointerEvent){
              .type = UI_POINTER_EVENT_CANCEL,
              .position = pos,
              .transform = entry->transform,
              .local_position = ui_transform_dot_vec2(entry->transform, pos),
          });
    }
  }

  ui_hit_test_state_clear(&state->input.button_down_hit_test);
}

static inline bool ui_widget_has_at_most_one_child(UIWidget *widget) {
  return !widget->first || widget->first == widget->last;
}

/// The default layout method for a widget just defers the layout to its child
/// and sizes itself around the child. Only works for widget that has at most
/// one child.
static void ui_widget_layout_default(UIWidget *widget,
                                     UIBoxConstraints constraints) {
  DEBUG_ASSERTF(
      ui_widget_has_at_most_one_child(widget),
      "Default layout method doesn't work for UI_WIDGET_MANY_CHILDREN.");
  UIWidget *child = widget->first;
  if (child) {
    ui_widget_layout(child, constraints);
    widget->size = child->size;
  } else {
    widget->size = ui_box_constraints_get_smallest(constraints);
  }
}

static void ui_widget_layout_for_sliver(UIWidget *widget,
                                        UIBoxConstraints constraints) {
  (void)constraints;

  DEBUG_ASSERTF(false, "layout is not implemented for sliver %s",
                widget->klass->name);
}

static void ui_widget_paint_child_default(UIWidget *child,
                                          UIPaintingContext *context,
                                          Vec2 offset) {
  ui_widget_paint(child, context, vec2_add(offset, child->offset));
}

static void ui_widget_paint_default(UIWidget *widget,
                                    UIPaintingContext *context, Vec2 offset) {
  for (UIWidget *child = widget->first; child; child = child->next) {
    ui_widget_paint_child_default(child, context, offset);
  }
}

static bool ui_widget_hit_test_children_default(UIWidget *widget,
                                                UIHitTestResult *result,
                                                Arena *arena) {
  for (UIWidget *child = widget->last; child; child = child->prev) {
    UITransform transform = result->transform;
    Vec2 local_position = result->local_position;

    result->transform = ui_transform_dot(
        transform, ui_transform_offset(-child->offset.x, -child->offset.y));
    result->local_position = vec2_sub(result->local_position, child->offset);

    bool hit = ui_widget_hit_test(child, result, arena);

    result->transform = transform;
    result->local_position = local_position;

    if (hit) {
      return true;
    }
  }
  return false;
}

void ui_hit_test_result_add(UIHitTestResult *self, UIWidget *widget,
                            Arena *arena) {
  UIHitTestEntry *entry = arena_push_struct(arena, UIHitTestEntry);
  entry->widget = widget;
  entry->transform = self->transform;
  entry->local_position = self->local_position;
  DLL_APPEND(self->first, self->last, entry, prev, next);
}

static bool ui_widget_hit_test_defer_to_children(UIWidget *widget,
                                                 UIHitTestResult *result,
                                                 Arena *arena) {
  if (!vec2_contains(result->local_position, vec2_zero(), widget->size)) {
    return false;
  }

  if (!ui_widget_hit_test_children_default(widget, result, arena)) {
    return false;
  }

  ui_hit_test_result_add(result, widget, arena);
  return true;
}

static bool ui_widget_hit_test_transluscent(UIWidget *widget,
                                            UIHitTestResult *result,
                                            Arena *arena) {
  if (!vec2_contains(result->local_position, vec2_zero(), widget->size)) {
    return false;
  }

  bool hit_children =
      ui_widget_hit_test_children_default(widget, result, arena);

  ui_hit_test_result_add(result, widget, arena);

  return hit_children;
}

static bool ui_widget_hit_test_opaque(UIWidget *widget, UIHitTestResult *result,
                                      Arena *arena) {
  if (!vec2_contains(result->local_position, vec2_zero(), widget->size)) {
    return false;
  }

  ui_widget_hit_test_children_default(widget, result, arena);

  ui_hit_test_result_add(result, widget, arena);
  return true;
}

static inline bool ui_widget_hit_test_by_behaviour(UIWidget *widget,
                                                   UIHitTestBehaviour behaviour,
                                                   UIHitTestResult *result,
                                                   Arena *arena) {
  switch (behaviour) {
    case UI_HIT_TEST_BEHAVIOUR_DEFER_TO_CHILD: {
      return ui_widget_hit_test_defer_to_children(widget, result, arena);
    } break;

    case UI_HIT_TEST_BEHAVIOUR_TRANSLUCENT: {
      return ui_widget_hit_test_transluscent(widget, result, arena);
    } break;

    case UI_HIT_TEST_BEHAVIOUR_OPAQUE: {
      return ui_widget_hit_test_opaque(widget, result, arena);
    } break;

    default:
      UNREACHABLE;
  }
  return false;
}

void ui_widget_layout_sliver_default(UIWidget *widget,
                                     const UISliverConstraints *constraints,
                                     UISliverGeometry *geometry) {
  (void)constraints;
  (void)geometry;
  DEBUG_ASSERTF(false, "ui_widget_layout_sliver is not implemented for %s",
                widget->klass->name);
}

static UIWidget *ui_widget_get_child_by_key(UIWidget *parent, UIKey key) {
  UIWidget *result = 0;
  if (!ui_key_is_zero(key)) {
    // TODO: Use hash map to speed up the search.
    for (UIWidget *child = parent->first; child; child = child->next) {
      if (ui_key_eq(child->key, key)) {
        result = child;
        break;
      }
    }
  }
  return result;
}

static UIWidget *ui_widget_get_child_nth(UIWidget *widget, usize nth) {
  UIWidget *child = widget->first;
  while (child && nth) {
    child = child->next;
    nth -= 1;
  }
  return child;
}

static bool ui_widget_eq(UIWidget *a, UIWidget *b) {
  if (!a || !b) {
    return false;
  }

  if (a->klass != b->klass) {
    return false;
  }

  return ui_key_eq(a->key, b->key);
}

static void ui_unmount_widgets(UIWidget *widget) {
  for (UIWidget *child = widget->first; child; child = child->next) {
    ui_unmount_widgets(child);
  }

  if (!widget->doppelganger) {
    ui_widget_unmount(widget);
  }
}

void ui_end_frame(void) {
  UIState *state = ui_state_get();
  UIFrame *frame = ui_state_get_current_frame(state);
  ASSERTF(ui_widget_stack_is_empty(&frame->stack),
          "mismatched begin/end calls, last begin: %s",
          frame->stack.last->widget->klass->name);

  // Update widget references in hit tests so we can send events to widgets
  // later.
  ui_hit_test_state_sync(&state->input.button_down_hit_test);
  ui_hit_test_state_sync(&state->input.button_move_hit_tests[0]);
  ui_hit_test_state_sync(&state->input.button_move_hit_tests[1]);

  // Layout and paint
  if (frame->root) {
    if (state->last_frame->root) {
      ui_unmount_widgets(state->last_frame->root);
    }

    Vec2 viewport_size = vec2_sub(state->viewport_max, state->viewport_min);
    ui_widget_layout(frame->root, ui_box_constraints_tight(viewport_size.x,
                                                           viewport_size.y));
  }

  frame->open = false;
}

void ui_paint(void) {
  UIState *state = ui_state_get();
  UIFrame *frame = ui_state_get_current_frame(state);
  UIPaintingContext context = {0};
  ui_widget_paint(frame->root, &context, state->viewport_min);
}

bool ui_should_rebuild(void) {
  UIState *state = ui_state_get();
  return state->should_rebuild;
}

Str8 ui_push_str8f(const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  Str8 result = ui_push_str8fv(format, ap);
  va_end(ap);
  return result;
}

Str8 ui_push_str8fv(const char *format, va_list ap) {
  UIState *state = ui_state_get();
  UIFrame *frame = ui_state_get_current_frame(state);
  return arena_push_str8fv(&frame->arena, format, ap);
}

f32 ui_animate_fast_f32(f32 value, f32 target) {
  UIState *state = ui_state_get();
  f32 result;
  f32 diff = (target - value);
  if (f32_abs(diff) < UI_PRECISION_ERROR_TOLERANCE) {
    result = target;
  } else {
    result = value + diff * state->fast_animation_rate;
  }
  return result;
}

void ui_gesture_arena_add(u32 pointer, UIGestureRecognizer recognizer) {
  UIGestureArenaState *state = &ui_state_get()->input.gesture_arena_state;
  UIGestureArenaMember *member = state->last_free_member;
  if (member) {
    DLL_REMOVE(state->first_free_member, state->last_free_member, member, prev,
               next);
  } else {
    member = arena_push_struct(&state->arena, UIGestureArenaMember);
  }
  member->recognizer = recognizer;

  bool added = false;
  for (UIGestureArena *gesture_arena = state->first_arena; gesture_arena;
       gesture_arena = gesture_arena->next) {
    if (gesture_arena->pointer == pointer) {
      ui_gesture_arena_add_member(gesture_arena, member);
      added = true;
      break;
    }
  }

  if (!added) {
    UIGestureArena *gesture_arena = state->first_free_arena;
    if (gesture_arena) {
      DLL_REMOVE(state->first_free_arena, state->last_free_arena, gesture_arena,
                 prev, next);
    } else {
      gesture_arena = arena_push_struct(&state->arena, UIGestureArena);
    }
    gesture_arena->pointer = pointer;
    DLL_APPEND(state->first_arena, state->last_arena, gesture_arena, prev,
               next);

    ui_gesture_arena_add_member(gesture_arena, member);
  }
}

static void ui_gesture_arena_free(u32 pointer) {
  UIGestureArenaState *state = &ui_state_get()->input.gesture_arena_state;

  for (UIGestureArena *gesture_arena = state->first_arena; gesture_arena;
       gesture_arena = gesture_arena->next) {
    if (gesture_arena->pointer == pointer) {
      DLL_REMOVE(state->first_arena, state->last_arena, gesture_arena, prev,
                 next);

      DLL_CONCAT(state->first_free_member, state->last_free_member,
                 gesture_arena->first_member, gesture_arena->last_member, prev,
                 next);
      gesture_arena->first_member = gesture_arena->last_member = 0;

      DLL_APPEND(state->first_free_arena, state->last_free_arena, gesture_arena,
                 prev, next);
      break;
    }
  }
}

static UIKey ui_key_from_str8(UIKey seed, Str8 str) {
  UIKey result;
  result.hash = str8_hash_with_seed(str, seed.hash ? seed.hash : 0x100);
  return result;
}

static UIKey ui_key_from_u8(UIKey seed, u8 ch) {
  u8 str[2] = {ch, 0};
  UIKey key = ui_key_from_str8(seed, (Str8){.ptr = str, .len = 1});
  return key;
}

static UIKey ui_key_from_u32(UIKey seed, u32 num) {
  UIKey key = seed;
  u32 s = num;
  while (s > 0) {
    key = ui_key_from_u8(key, (u8)(s & 0xFF));
    s = s >> 8;
  }
  return key;
}

// TODO: Global Key
static UIKey ui_key_local(UIKey seed, u32 seq, const char *tag, Str8 id) {
  // key = seed + tag + (id || seq)
  UIKey key = seed;
  key = ui_key_from_str8(key, str8_from_cstr(tag));
  if (!str8_is_empty(id)) {
    key = ui_key_from_str8(key, id);
  } else {
    key = ui_key_from_u32(key, seq);
  }
  return key;
}

/// Allocate a UIWidget in current frame's arena.
static UIWidget *ui_widget_alloc(Arena *arena, UIWidgetClass *klass) {
  UIWidget *widget = arena_push(arena, sizeof(UIWidget) + klass->state_size,
                                ARENA_PUSH_NO_ZERO);
  memory_zero(widget, sizeof(UIWidget));
  widget->klass = klass;
  return widget;
}

/// Append `child` as a child to `parent`.
static void ui_widget_append(UIWidget *parent, UIWidget *child,
                             bool allow_many_chilren) {
  child->parent = parent;
  if (allow_many_chilren) {
    DLL_APPEND(parent->first, parent->last, child, prev, next);
    ++parent->child_count;
  } else {
    DEBUG_ASSERTF(!parent->first, "%s expects at most one child",
                  parent->klass->name);
    parent->first = parent->last = child;
    parent->child_count = 1;
  }
}

static inline bool ui_can_reuse_widget(UIWidget *widget,
                                       UIWidget *last_widget) {
  return last_widget && !last_widget->doppelganger &&
         ui_widget_eq(widget, last_widget);
}

UIWidget *ui_widget_begin(UIWidgetClass *klass, UIKey key) {
  UIState *state = ui_state_get();
  UIFrame *frame = state->current_frame;

  UIWidget *widget = ui_widget_alloc(&frame->arena, klass);
  UIWidget *last_widget = 0;

  UIWidgetStackEntry *parent = frame->stack.last;
  if (parent) {
    if (parent->widget->doppelganger) {
      // TODO: Check for global key
      last_widget =
          ui_widget_get_child_by_key(parent->widget->doppelganger, key);
      if (!ui_can_reuse_widget(widget, last_widget)) {
        last_widget = 0;
      }

      if (!last_widget) {
        last_widget = parent->last_child;
        if (!ui_can_reuse_widget(widget, last_widget)) {
          last_widget = 0;
        }
      }
    }

    ui_widget_append(parent->widget, widget,
                     parent->widget->klass->flags & UI_WIDGET_MANY_CHILDREN);

    if (parent->last_child) {
      parent->last_child = parent->last_child->next;
    }
  } else {
    DEBUG_ASSERTF(!frame->root, "root widget already exists.");
    frame->root = widget;
    if (ui_can_reuse_widget(widget, state->last_frame->root)) {
      last_widget = state->last_frame->root;
    }
  }

  if (last_widget) {
    ASSERTF(last_widget->klass == widget->klass,
            "last_widget->klass => '%s', widget->klass => '%s'",
            last_widget->klass->name, widget->klass->name);
    ASSERT(!last_widget->doppelganger);

    last_widget->doppelganger = widget;
    widget->size = last_widget->size;
    widget->offset = last_widget->offset;
    widget->doppelganger = last_widget;

    if (klass->state_size > 0) {
      memory_copy(ui_widget_get_state_(widget, klass->state_size),
                  ui_widget_get_state_(last_widget, klass->state_size),
                  klass->state_size);
    }
  } else {
    if (widget->klass->state_size > 0) {
      void *state = ui_widget_get_state_(widget, klass->state_size);
      memory_zero(state, klass->state_size);
    }
    ui_widget_mount(widget);
  }

  ui_widget_stack_push(&frame->stack, widget, &frame->arena);
  return widget;
}

void ui_widget_end(UIWidgetClass *klass) {
  UIState *state = ui_state_get();
  UIFrame *frame = state->current_frame;
  UIWidget *widget = ui_widget_stack_pop(&frame->stack);
  ASSERTF(widget->klass == klass,
          "mismatched begin/end calls. Begin with %s, end with %s",
          widget->klass->name, klass->name);
}

UIWidget *ui_widget_get_current(void) {
  UIState *state = ui_state_get();
  UIFrame *frame = state->current_frame;
  UIWidget *result = 0;
  if (frame->stack.last) {
    result = frame->stack.last->widget;
  }
  return result;
}

static UIWidget *ui_widget_find_first_ancestor(UIWidget *widget,
                                               UIWidgetClass *klass) {
  while (widget && widget->klass != klass) {
    widget = widget->parent;
  }
  return widget;
}

UIWidget *ui_widget_get_root(void) {
  UIState *state = ui_state_get();
  if (state->current_frame) {
    return state->current_frame->root;
  }
  return 0;
}

UIWidget *ui_widget_get_last_child(void) {
  UIWidget *current = ui_widget_get_current();
  if (current) {
    return current->last;
  }
  return 0;
}

void *ui_widget_set_parent_data_(UIWidget *widget, u64 type, usize data_size) {
  UIState *state = ui_state_get();
  UIFrame *frame = ui_state_get_current_frame(state);
  Str8 key = str8((u8 *)&type, sizeof(type));
  return hash_trie_upsert_(&widget->parent_data, key, &frame->arena, data_size);
}

////////////////////////////////////////////////////////////////////////////////
///
/// UILimitedBox
///
typedef struct UILimitedBoxState {
  f32 max_width;
  f32 max_height;
} UILimitedBoxState;

static UIBoxConstraints ui_limited_box_limit_constraints(
    UILimitedBoxState *state, UIBoxConstraints constraints) {
  return (UIBoxConstraints){
      .min_width = constraints.min_width,
      .max_width = ui_box_constraints_has_bounded_width(constraints)
                       ? constraints.max_width
                       : ui_box_constraints_constrain_width(constraints,
                                                            state->max_width),
      .min_height = constraints.min_height,
      .max_height = ui_box_constraints_has_bounded_height(constraints)
                        ? constraints.max_height
                        : ui_box_constraints_constrain_height(
                              constraints, state->max_height),
  };
}

static void ui_limited_box_layout(UIWidget *widget,
                                  UIBoxConstraints constraints) {
  UIBoxConstraints limited_constraints = ui_limited_box_limit_constraints(
      ui_widget_get_state(widget, UILimitedBoxState), constraints);

  UIWidget *child = widget->first;
  if (child) {
    ui_widget_layout(child, limited_constraints);
    Vec2 child_size = child->size;
    widget->size = ui_box_constraints_constrain(constraints, child_size);
  } else {
    widget->size =
        ui_box_constraints_constrain(limited_constraints, vec2_zero());
  }
}

UIWidgetClass ui_limited_box_class = {
    .name = "LimitedBox",
    .state_size = sizeof(UILimitedBoxState),
    .layout = ui_limited_box_layout,
};

void ui_limited_box_begin(const UILimitedBoxProps *props) {
  UIWidget *widget = ui_widget_begin(&ui_limited_box_class, props->key);
  UILimitedBoxState *state = ui_widget_get_state(widget, UILimitedBoxState);
  state->max_width = props->max_width;
  state->max_height = props->max_height;
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIColoredBox
///
typedef struct UIColoredBoxState {
  UIColor color;
} UIColoredBoxState;

static void ui_colored_box_paint(UIWidget *widget, UIPaintingContext *context,
                                 Vec2 offset) {
  UIColoredBoxState *state = ui_widget_get_state(widget, UIColoredBoxState);
  Vec2 size = widget->size;
  if (size.x > 0 && size.y > 0) {
    fill_rect(offset, vec2_add(offset, size), state->color);
  }

  for (UIWidget *child = widget->first; child; child = child->next) {
    ui_widget_paint_child_default(child, context, offset);
  }
}

UIWidgetClass ui_colored_box_class = {
    .name = "ColoredBox",
    .state_size = sizeof(UIColoredBoxState),
    .paint = ui_colored_box_paint,
    .hit_test = ui_widget_hit_test_opaque,
};

void ui_colored_box_begin(const UIColoredBoxProps *props) {
  UIWidget *widget = ui_widget_begin(&ui_colored_box_class, props->key);
  UIColoredBoxState *state = ui_widget_get_state(widget, UIColoredBoxState);
  state->color = props->color;
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIConstrainedBox
///
typedef struct UIConstrainedBoxState {
  UIBoxConstraints constraints;
} UIConstrainedBoxState;

static void ui_constrained_box_layout(UIWidget *widget,
                                      UIBoxConstraints constraints) {
  ASSERT(ui_widget_has_at_most_one_child(widget));
  UIConstrainedBoxState *state =
      ui_widget_get_state(widget, UIConstrainedBoxState);
  UIBoxConstraints enforced_constraints =
      ui_box_constraints_enforce(state->constraints, constraints);
  UIWidget *child = widget->first;
  if (child) {
    ui_widget_layout(child, enforced_constraints);
    widget->size = child->size;
  } else {
    widget->size =
        ui_box_constraints_constrain(enforced_constraints, vec2_zero());
  }
}

UIWidgetClass ui_constrained_box_class = {
    .name = "ConstrainedBox",
    .state_size = sizeof(UIConstrainedBoxState),
    .layout = ui_constrained_box_layout,
};

void ui_constrained_box_begin(const UIConstrainedBoxProps *props) {
  UIWidget *widget = ui_widget_begin(&ui_constrained_box_class, props->key);
  UIConstrainedBoxState *state =
      ui_widget_get_state(widget, UIConstrainedBoxState);
  state->constraints = props->constraints;
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIAlign
///
typedef struct UIAlignState {
  UIAlignment alignment;
  f32o width;
  f32o height;
} UIAlignState;

static void ui_widget_align_children(UIWidget *widget, UIAlignment alignment) {
  // TODO: UITextDirection
  for (UIWidget *child = widget->first; child; child = child->next) {
    child->offset = ui_alignment_align_offset(
        alignment, vec2_sub(widget->size, child->size));
  }
}

static void ui_align_layout_impl(UIWidget *widget, UIAlignment alignment,
                                 f32o width, f32o height,
                                 UIBoxConstraints constraints) {
  if (width.present) {
    ASSERTF(width.value >= 0, "widget must be positive, got %f", width.value);
  }
  if (height.present) {
    ASSERTF(height.value >= 0, "height must be positive, got %f", height.value);
  }
  bool should_shrink_wrap_width =
      width.present || f32_is_infinity(constraints.max_width);
  bool should_shrink_wrap_height =
      height.present || f32_is_infinity(constraints.max_height);
  UIBoxConstraints child_constraints = ui_box_constraints_loosen(constraints);

  UIWidget *child = widget->first;
  if (child) {
    ui_widget_layout(child, child_constraints);
    Vec2 child_size = child->size;

    Vec2 wrap_size =
        vec2(should_shrink_wrap_width
                 ? (child_size.x * (width.present ? width.value : 1.0f))
                 : F32_INFINITY,
             should_shrink_wrap_height
                 ? (child_size.y * (height.present ? height.value : 1.0f))
                 : F32_INFINITY);

    widget->size = ui_box_constraints_constrain(constraints, wrap_size);

    ui_widget_align_children(widget, alignment);
  } else {
    Vec2 size = vec2(should_shrink_wrap_width ? 0 : F32_INFINITY,
                     should_shrink_wrap_height ? 0 : F32_INFINITY);
    widget->size = ui_box_constraints_constrain(constraints, size);
  }
}

static void ui_align_layout(UIWidget *widget, UIBoxConstraints constraints) {
  UIAlignState *state = ui_widget_get_state(widget, UIAlignState);
  ui_align_layout_impl(widget, state->alignment, state->width, state->height,
                       constraints);
}

UIWidgetClass ui_align_class = {
    .name = "Align",
    .state_size = sizeof(UIAlignState),
    .layout = ui_align_layout,
};

void ui_align_begin(const UIAlignProps *props) {
  UIWidget *widget = ui_widget_begin(&ui_align_class, props->key);
  UIAlignState *state = ui_widget_get_state(widget, UIAlignState);
  state->alignment = props->alignment;
  state->width = props->width;
  state->height = props->height;
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIUnconstrainedBox
///
/// A widget that imposes no constraints on its child, allowing it to render
/// at its "natural" size.
typedef struct UIUnconstrainedBoxState {
  UIAlignment alignment;
} UIUnconstrainedBoxState;

static void ui_unconstrained_box_layout(UIWidget *widget,
                                        UIBoxConstraints constraints) {
  UIUnconstrainedBoxState *state =
      ui_widget_get_state(widget, UIUnconstrainedBoxState);
  UIBoxConstraints child_constraints =
      ui_box_constraints(0, F32_INFINITY, 0, F32_INFINITY);
  UIWidget *child = widget->first;
  if (child) {
    ui_widget_layout(widget->first, child_constraints);
    Vec2 child_size = child->size;
    widget->size = ui_box_constraints_constrain(constraints, child_size);
  } else {
    widget->size = ui_box_constraints_constrain(constraints, vec2_zero());
  }

  ui_widget_align_children(widget, state->alignment);
}

UIWidgetClass ui_unconstrained_box_class = {
    .name = "UnconstrainedBox",
    .state_size = sizeof(UIUnconstrainedBoxState),
    .layout = ui_unconstrained_box_layout,
};

void ui_unconstrained_box_begin(const UIUnconstrainedBoxProps *props) {
  UIWidget *widget = ui_widget_begin(&ui_unconstrained_box_class, props->key);
  UIUnconstrainedBoxState *state =
      ui_widget_get_state(widget, UIUnconstrainedBoxState);
  state->alignment = props->alignment;
}

////////////////////////////////////////////////////////////////////////////////
///
/// UICenter
///
typedef struct UICenterState {
  f32o width;
  f32o height;
} UICenterState;

static void ui_center_layout(UIWidget *widget, UIBoxConstraints constraints) {
  UICenterState *state = ui_widget_get_state(widget, UICenterState);
  ui_align_layout_impl(widget, ui_alignment_center(), state->width,
                       state->height, constraints);
}

UIWidgetClass ui_center_class = {
    .name = "Center",
    .state_size = sizeof(UICenterState),
    .layout = ui_center_layout,
};

void ui_center_begin(const UICenterProps *props) {
  UIWidget *widget = ui_widget_begin(&ui_center_class, props->key);
  UICenterState *state = ui_widget_get_state(widget, UICenterState);
  state->width = props->width;
  state->height = props->height;
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIPadding
///
typedef struct UIPaddingState {
  UIEdgeInsets padding;
} UIPaddingState;

typedef struct UIResolvedEdgeInsets {
  f32 left;
  f32 right;
  f32 top;
  f32 bottom;
} UIResolvedEdgeInsets;

static inline f32 ui_resolved_edge_insets_get_horizontal(
    UIResolvedEdgeInsets edge_insets) {
  return edge_insets.left + edge_insets.right;
}

static inline f32 ui_resolved_edge_insets_get_vertical(
    UIResolvedEdgeInsets edge_insets) {
  return edge_insets.top + edge_insets.bottom;
}

static inline UIBoxConstraints ui_box_constraints_deflate(
    UIBoxConstraints constraints, UIResolvedEdgeInsets edge_insets) {
  f32 horizontal = ui_resolved_edge_insets_get_horizontal(edge_insets);
  f32 vertical = ui_resolved_edge_insets_get_vertical(edge_insets);
  f32 deflated_min_width = f32_max(0, constraints.min_width - horizontal);
  f32 deflated_min_height = f32_max(0, constraints.min_height - vertical);
  return ui_box_constraints(
      deflated_min_width,
      f32_max(deflated_min_width, constraints.max_width - horizontal),
      deflated_min_height,
      f32_max(deflated_min_height, constraints.max_height - vertical));
}

static void ui_padding_layout(UIWidget *widget, UIBoxConstraints constraints) {
  UIPaddingState *state = ui_widget_get_state(widget, UIPaddingState);
  // TODO: UITextDirection
  UIResolvedEdgeInsets resolved_padding = {
      .left = state->padding.start,
      .right = state->padding.end,
      .top = state->padding.top,
      .bottom = state->padding.bottom,
  };
  f32 horizontal = ui_resolved_edge_insets_get_horizontal(resolved_padding);
  f32 vertical = ui_resolved_edge_insets_get_vertical(resolved_padding);
  if (widget->first) {
    UIBoxConstraints inner_constraints =
        ui_box_constraints_deflate(constraints, resolved_padding);

    UIWidget *child = widget->first;
    ui_widget_layout(child, inner_constraints);
    Vec2 child_size = child->size;
    child->offset = vec2(resolved_padding.left, resolved_padding.top);

    widget->size = ui_box_constraints_constrain(
        constraints, vec2(horizontal + child_size.x, vertical + child_size.y));
  } else {
    widget->size =
        ui_box_constraints_constrain(constraints, vec2(horizontal, vertical));
  }
}

UIWidgetClass ui_padding_class = {
    .name = "Padding",
    .state_size = sizeof(UIPaddingState),
    .layout = ui_padding_layout,
};

void ui_padding_begin(const UIPaddingProps *props) {
  UIWidget *widget = ui_widget_begin(&ui_padding_class, props->key);
  UIPaddingState *state = ui_widget_get_state(widget, UIPaddingState);
  state->padding = props->padding;
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIContainer
///
typedef struct UIContainerState {
  UIAlignmentO alignment;
  UIEdgeInsetsO padding;
  UIColorO color;

  UIBoxConstraintsO constraints;
  UIEdgeInsetsO margin;
} UIContainerState;

UIWidgetClass ui_container_class = {
    .name = "Container",
    .state_size = sizeof(UIContainerState),
};

void ui_container_begin(const UIContainerProps *props) {
  UIWidget *widget = ui_widget_begin(&ui_container_class, props->key);
  UIContainerState *state = ui_widget_get_state(widget, UIContainerState);
  state->alignment = props->alignment;
  state->padding = props->padding;
  state->color = props->color;
  state->constraints = props->constraints;
  state->margin = props->margin;

  if (props->width.present || props->height.present) {
    if (state->constraints.present) {
      state->constraints = ui_box_constraints_some(ui_box_constraints_tighten(
          state->constraints.value, props->width, props->height));
    } else {
      state->constraints = ui_box_constraints_some(
          ui_box_constraints_tight_for(props->width, props->height));
    }
  }

  if (state->margin.present) {
    ui_padding_begin(&(UIPaddingProps){
        .padding = state->margin.value,
    });
  }

  if (state->constraints.present) {
    ui_constrained_box_begin(&(UIConstrainedBoxProps){
        .constraints = state->constraints.value,
    });
  }

  if (state->color.present) {
    ui_colored_box_begin(&(UIColoredBoxProps){
        .color = state->color.value,
    });
  }

  if (state->padding.present) {
    ui_padding_begin(&(UIPaddingProps){
        .padding = state->padding.value,
    });
  }

  if (state->alignment.present) {
    ui_align_begin(&(UIAlignProps){
        .alignment = state->alignment.value,
    });
  }
}

void ui_container_end(void) {
  UIWidget *current = ui_widget_get_current();
  UIWidget *widget =
      ui_widget_find_first_ancestor(current, &ui_container_class);
  ASSERT(widget);
  UIContainerState *state = ui_widget_get_state(widget, UIContainerState);

  if (!current->first &&
      (!state->constraints.present ||
       !ui_box_constraints_is_tight(state->constraints.value))) {
    ui_limited_box_begin(&(UILimitedBoxProps){0});
    ui_constrained_box_begin(&(UIConstrainedBoxProps){
        .constraints = ui_box_constraints(F32_INFINITY, F32_INFINITY,
                                          F32_INFINITY, F32_INFINITY),
    });
    ui_constrained_box_end();
    ui_limited_box_end();
  }

  if (state->alignment.present) {
    ui_align_end();
  }

  if (state->padding.present) {
    ui_padding_end();
  }

  if (state->color.present) {
    ui_colored_box_end();
  }

  if (state->constraints.present) {
    ui_constrained_box_end();
  }

  if (state->margin.present) {
    ui_padding_end();
  }

  ui_widget_end(&ui_container_class);
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIStack
///
typedef struct UIStackState {
  UIStackFit fit;
  bool has_visual_overflow;
} UIStackState;

static Vec2 ui_stack_compute_size(UIWidget *widget, UIStackFit fit,
                                  UIBoxConstraints constraints) {
  if (!widget->first) {
    Vec2 biggest = ui_box_constraints_get_biggest(constraints);
    if (vec2_is_finite(biggest)) {
      return biggest;
    }
    return ui_box_constraints_get_smallest(constraints);
  }

  f32 width = constraints.min_width;
  f32 height = constraints.min_height;

  UIBoxConstraints non_positioned_constraints;
  switch (fit) {
    case UI_STACK_FIT_LOOSE: {
      non_positioned_constraints = ui_box_constraints_loosen(constraints);
    } break;
    case UI_STACK_FIT_EXPAND: {
      Vec2 biggest = ui_box_constraints_get_biggest(constraints);
      non_positioned_constraints =
          ui_box_constraints_tight(biggest.x, biggest.y);
    } break;
    default: {
      non_positioned_constraints = constraints;
    } break;
  }

  bool has_non_positioned_child = false;
  for (UIWidget *child = widget->first; child; child = child->next) {
    UIParentDataStack *data = ui_widget_get_parent_data(
        child, UI_PARENT_DATA_STACK, UIParentDataStack);

    if (!ui_parent_data_stack_is_positioned(data)) {
      has_non_positioned_child = true;
      ui_widget_layout(child, non_positioned_constraints);
      Vec2 child_size = child->size;

      width = f32_max(width, child_size.x);
      height = f32_max(height, child_size.y);
    }
  }

  Vec2 size;
  if (has_non_positioned_child) {
    size = vec2(width, height);
    ASSERT(size.x == ui_box_constraints_constrain_width(constraints, width));
    ASSERT(size.y == ui_box_constraints_constrain_height(constraints, height));
  } else {
    size = ui_box_constraints_get_biggest(constraints);
  }

  ASSERT(vec2_is_finite(size));
  return size;
}

/// Returns true when the child has visual overflow.
static bool ui_stack_layout_positioned_child(UIWidget *child,
                                             UIParentDataStack *data,
                                             Vec2 size) {
  UIBoxConstraints child_constraints =
      ui_parent_data_stack_get_positioned_child_constraints(data, size);
  ui_widget_layout(child, child_constraints);
  Vec2 child_size = child->size;

  f32 x;
  if (data->left.present) {
    x = data->left.value;
  } else if (data->right.present) {
    x = size.x - data->right.value - child_size.x;
  } else {
    // TODO: alignment
    x = 0;
  }

  f32 y;
  if (data->top.present) {
    y = data->top.value;
  } else if (data->bottom.present) {
    y = size.y - data->bottom.value - child_size.y;
  } else {
    // TODO: alignment
    y = 0;
  }

  child->offset = vec2(x, y);

  return x < 0 || x + child_size.x > size.x || y < 0 ||
         y + child_size.y > size.y;
}

static void ui_stack_layout(UIWidget *widget, UIBoxConstraints constraints) {
  UIStackState *state = ui_widget_get_state(widget, UIStackState);
  Vec2 size = ui_stack_compute_size(widget, state->fit, constraints);
  widget->size = size;

  state->has_visual_overflow = false;
  for (UIWidget *child = widget->first; child; child = child->next) {
    UIParentDataStack *data = ui_widget_get_parent_data(
        child, UI_PARENT_DATA_STACK, UIParentDataStack);
    if (!ui_parent_data_stack_is_positioned(data)) {
      // TODO: alignment
    } else {
      state->has_visual_overflow =
          ui_stack_layout_positioned_child(child, data, size) ||
          state->has_visual_overflow;
    }
  }
}

static void ui_stack_paint(UIWidget *widget, UIPaintingContext *context,
                           Vec2 offset) {
  UIStackState *state = ui_widget_get_state(widget, UIStackState);

  bool should_clip = state->has_visual_overflow;
  if (should_clip) {
    push_clip_rect(offset, vec2_add(offset, widget->size));
  }

  for (UIWidget *child = widget->first; child; child = child->next) {
    ui_widget_paint_child_default(child, context, offset);
  }

  if (should_clip) {
    pop_clip_rect();
  }
}

UIWidgetClass ui_stack_class = {
    .name = "Stack",
    .flags = UI_WIDGET_MANY_CHILDREN,
    .state_size = sizeof(UIStackState),
    .layout = ui_stack_layout,
    .paint = ui_stack_paint,
};

void ui_stack_begin(const UIStackProps *props) {
  UIWidget *widget = ui_widget_begin(&ui_stack_class, props->key);
  UIStackState *state = ui_widget_get_state(widget, UIStackState);
  state->fit = props->fit;
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIPositioned
///
UIWidgetClass ui_positioned_class = {
    .name = "Positioned",
};

void ui_positioned_begin(const UIPositionedProps *props) {
  UIWidget *widget = ui_widget_begin(&ui_positioned_class, props->key);
  *ui_widget_set_parent_data(widget, UI_PARENT_DATA_STACK, UIParentDataStack) =
      (UIParentDataStack){
          .left = props->left,
          .right = props->right,
          .top = props->top,
          .bottom = props->bottom,
          .width = props->width,
          .height = props->height,
      };
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIFlexible
///
UIWidgetClass ui_flexible_class = {
    .name = "Flexible",
};

void ui_flexible_begin(const UIFlexibleProps *props) {
  UIWidget *widget = ui_widget_begin(&ui_flexible_class, props->key);
  UIParentDataFlex flex = {
      .flex = props->flex,
      .fit = props->fit,
  };
  *(ui_widget_set_parent_data(widget, UI_PARENT_DATA_FLEX, UIParentDataFlex)) =
      flex;
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIExpanded
///
UIWidgetClass ui_expanded_class = {
    .name = "Expanded",
};

void ui_expanded_begin(const UIExpandedProps *props) {
  UIWidget *widget = ui_widget_begin(&ui_expanded_class, props->key);
  UIParentDataFlex flex = {
      .flex = props->flex,
      .fit = UI_FLEX_FIT_TIGHT,
  };
  *ui_widget_set_parent_data(widget, UI_PARENT_DATA_FLEX, UIParentDataFlex) =
      flex;
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIFlex
///
typedef struct UIFlexState {
  UIAxis direction;
  UIMainAxisAlignment main_axis_alignment;
  UIMainAxisSize main_axis_size;
  UICrossAxisAlignment cross_axis_alignment;
  f32 spacing;
} UIFlexState;

static inline UIBoxConstraints ui_box_constraints_for_non_flex_child(
    UIAxis direction, UICrossAxisAlignment cross_axis_alignment,
    UIBoxConstraints constraints) {
  bool should_fill_cross_axis = false;
  if (cross_axis_alignment == UI_CROSS_AXIS_ALIGNMENT_STRETCH) {
    should_fill_cross_axis = true;
  }

  UIBoxConstraints result;
  switch (direction) {
    case UI_AXIS_HORIZONTAL: {
      if (should_fill_cross_axis) {
        result = ui_box_constraints_tight_height(constraints.max_height);
      } else {
        result = (UIBoxConstraints){
            .min_width = 0,
            .max_width = F32_INFINITY,
            .min_height = 0,
            .max_height = constraints.max_height,
        };
      }
    } break;
    case UI_AXIS_VERTICAL: {
      if (should_fill_cross_axis) {
        result = ui_box_constraints_tight_width(constraints.max_width);
      } else {
        result = (UIBoxConstraints){
            .min_width = 0,
            .max_width = constraints.max_width,
            .min_height = 0,
            .max_height = F32_INFINITY,
        };
      }
    } break;
    default:
      UNREACHABLE;
  }
  return result;
}

static inline UIBoxConstraints ui_box_constraints_for_flex_child(
    UIAxis direction, UICrossAxisAlignment cross_axis_alignment,
    UIBoxConstraints constraints, f32 max_child_extent,
    UIParentDataFlex *data) {
  ASSERT(data->flex > 0);
  ASSERT(max_child_extent >= 0.0f);
  f32 min_child_extent = 0.0;
  if (data->fit == UI_FLEX_FIT_TIGHT) {
    min_child_extent = max_child_extent;
  }
  bool should_fill_cross_axis = false;
  if (cross_axis_alignment == UI_CROSS_AXIS_ALIGNMENT_STRETCH) {
    should_fill_cross_axis = true;
  }
  UIBoxConstraints result;
  if (direction == UI_AXIS_HORIZONTAL) {
    result = (UIBoxConstraints){
        .min_width = min_child_extent,
        .max_width = max_child_extent,
        .min_height = should_fill_cross_axis ? constraints.max_height : 0,
        .max_height = constraints.max_height,
    };
  } else {
    result = (UIBoxConstraints){
        .min_width = should_fill_cross_axis ? constraints.max_width : 0,
        .max_width = constraints.max_width,
        .min_height = min_child_extent,
        .max_height = max_child_extent,
    };
  }
  return result;
}

typedef struct AxisSize {
  f32 main;
  f32 cross;
} AxisSize;

static inline AxisSize axis_size(f32 main, f32 cross) {
  return (AxisSize){.main = main, .cross = cross};
}

static inline Vec2 convert_size(Vec2 size, UIAxis direction) {
  switch (direction) {
    case UI_AXIS_HORIZONTAL: {
      return size;
    } break;
    case UI_AXIS_VERTICAL: {
      return vec2(size.y, size.x);
    } break;
    default:
      UNREACHABLE;
  }
}

static inline AxisSize axis_size_from_size(Vec2 size, UIAxis direction) {
  Vec2 converted = convert_size(size, direction);
  return axis_size(converted.x, converted.y);
}

static AxisSize axis_size_constrains(AxisSize size,
                                     UIBoxConstraints constraints,
                                     UIAxis direction) {
  UIBoxConstraints effective_constraints = constraints;
  if (direction != UI_AXIS_HORIZONTAL) {
    effective_constraints = ui_box_constraints_flip(constraints);
  }
  Vec2 constrained_size = ui_box_constraints_constrain(
      effective_constraints, vec2(size.main, size.cross));
  return axis_size(constrained_size.x, constrained_size.y);
}

typedef struct UIFlexLayoutSize {
  AxisSize size;
  f32 main_axis_free_space;
  bool can_flex;
  f32 space_per_flex;
} UIFlexLayoutSize;

static UIFlexLayoutSize ui_flex_compute_size(
    UIWidget *widget, UIAxis direction, UIMainAxisSize main_axis_size,
    UICrossAxisAlignment cross_axis_alignment, f32 spacing,
    UIBoxConstraints constraints) {
  // Determine used flex factor, size inflexible items, calculate free space.
  f32 max_main_size;
  switch (direction) {
    case UI_AXIS_HORIZONTAL: {
      max_main_size =
          ui_box_constraints_constrain_width(constraints, F32_INFINITY);
    } break;
    case UI_AXIS_VERTICAL: {
      max_main_size =
          ui_box_constraints_constrain_height(constraints, F32_INFINITY);
    } break;
    default:
      UNREACHABLE;
  }
  bool can_flex = f32_is_finite(max_main_size);
  UIBoxConstraints non_flex_child_constraints =
      ui_box_constraints_for_non_flex_child(direction, cross_axis_alignment,
                                            constraints);
  // TODO: Baseline aligned

  // The first pass lays out non-flex children and computes total flex.
  i32 total_flex = 0;
  UIWidget *first_flex_child = 0;
  // Initially, accumulated_size is the sum of the spaces between children in
  // the main axis.
  AxisSize accumulated_size =
      axis_size(spacing * (widget->child_count - 1), 0.0f);
  for (UIWidget *child = widget->first; child; child = child->next) {
    i32 child_flex = 0;
    if (can_flex) {
      UIParentDataFlex *data = ui_widget_get_parent_data(
          child, UI_PARENT_DATA_FLEX, UIParentDataFlex);
      if (data) {
        child_flex = data->flex;
      }
    }

    if (child_flex > 0) {
      total_flex += child_flex;
      if (!first_flex_child) {
        first_flex_child = child;
      }
    } else {
      ui_widget_layout(child, non_flex_child_constraints);
      AxisSize child_size = axis_size_from_size(child->size, direction);

      accumulated_size.main += child_size.main;
      accumulated_size.cross =
          f32_max(accumulated_size.cross, child_size.cross);
    }
  }

  DEBUG_ASSERT((total_flex == 0) == (first_flex_child == 0));
  DEBUG_ASSERT(first_flex_child == 0 || can_flex);

  // The second pass distributes free space to flexible children.
  f32 flex_space = f32_max(0.0f, max_main_size - accumulated_size.main);
  f32 space_per_flex = flex_space / total_flex;
  for (UIWidget *child = widget->first; child && total_flex > 0;
       child = child->next) {
    UIParentDataFlex *data =
        ui_widget_get_parent_data(child, UI_PARENT_DATA_FLEX, UIParentDataFlex);
    if (!data || data->flex <= 0) {
      continue;
    }
    total_flex -= data->flex;
    DEBUG_ASSERT(f32_is_finite(space_per_flex));
    f32 max_child_extent = space_per_flex * data->flex;
    DEBUG_ASSERT(data->fit == UI_FLEX_FIT_LOOSE ||
                 max_child_extent < F32_INFINITY);
    UIBoxConstraints child_constraints = ui_box_constraints_for_flex_child(
        direction, cross_axis_alignment, constraints, max_child_extent, data);
    ui_widget_layout(child, child_constraints);
    AxisSize child_size = axis_size_from_size(child->size, direction);

    accumulated_size.main += child_size.main;
    accumulated_size.cross = f32_max(accumulated_size.cross, child_size.cross);
  }
  DEBUG_ASSERT(total_flex == 0);

  f32 ideal_main_size;
  if (main_axis_size == UI_MAIN_AXIS_SIZE_MAX && f32_is_finite(max_main_size)) {
    ideal_main_size = max_main_size;
  } else {
    ideal_main_size = accumulated_size.main;
  }

  AxisSize size = axis_size(ideal_main_size, accumulated_size.cross);
  AxisSize constrained_size =
      axis_size_constrains(size, constraints, direction);

  return (UIFlexLayoutSize){
      .size = constrained_size,
      .main_axis_free_space = constrained_size.main - accumulated_size.main,
      .can_flex = can_flex,
      .space_per_flex = can_flex ? space_per_flex : 0,
  };
}

static void ui_flex_distribute_space(UIMainAxisAlignment main_axis_alignment,
                                     f32 free_space, u32 item_count,
                                     bool flipped, f32 spacing,
                                     f32 *leading_space, f32 *between_space) {
  switch (main_axis_alignment) {
    case UI_MAIN_AXIS_ALIGNMENT_START: {
      if (flipped) {
        *leading_space = free_space;
      } else {
        *leading_space = 0;
      }
      *between_space = spacing;
    } break;

    case UI_MAIN_AXIS_ALIGNMENT_END: {
      ui_flex_distribute_space(UI_MAIN_AXIS_ALIGNMENT_START, free_space,
                               item_count, !flipped, spacing, leading_space,
                               between_space);
    } break;

    case UI_MAIN_AXIS_ALIGNMENT_SPACE_BETWEEN: {
      if (item_count < 2) {
        ui_flex_distribute_space(UI_MAIN_AXIS_ALIGNMENT_START, free_space,
                                 item_count, flipped, spacing, leading_space,
                                 between_space);
      } else {
        *leading_space = 0;
        *between_space = free_space / (item_count - 1) + spacing;
      }
    } break;

    case UI_MAIN_AXIS_ALIGNMENT_SPACE_AROUND: {
      if (item_count == 0) {
        ui_flex_distribute_space(UI_MAIN_AXIS_ALIGNMENT_START, free_space,
                                 item_count, flipped, spacing, leading_space,
                                 between_space);
      } else {
        *leading_space = free_space / item_count / 2;
        *between_space = free_space / item_count + spacing;
      }
    } break;

    case UI_MAIN_AXIS_ALIGNMENT_CENTER: {
      *leading_space = free_space / 2.0f;
      *between_space = spacing;
    } break;

    case UI_MAIN_AXIS_ALIGNMENT_SPACE_EVENLY: {
      *leading_space = free_space / (item_count + 1);
      *between_space = free_space / (item_count + 1) + spacing;
    } break;

    default:
      UNREACHABLE;
  }
}

static f32 ui_flex_get_child_cross_axis_offset(
    UICrossAxisAlignment cross_axis_alignment, f32 free_space, bool flipped) {
  switch (cross_axis_alignment) {
    case UI_CROSS_AXIS_ALIGNMENT_STRETCH:
    case UI_CROSS_AXIS_ALIGNMENT_BASELINE: {
      return 0.0f;
    } break;

    case UI_CROSS_AXIS_ALIGNMENT_START: {
      return flipped ? free_space : 0.0f;
    } break;

    case UI_CROSS_AXIS_ALIGNMENT_CENTER: {
      return free_space / 2.0f;
    } break;

    case UI_CROSS_AXIS_ALIGNMENT_END: {
      return ui_flex_get_child_cross_axis_offset(UI_CROSS_AXIS_ALIGNMENT_START,
                                                 free_space, !flipped);
    } break;

    default:
      UNREACHABLE;
  }
}

static f32 ui_flex_get_cross_size(Vec2 size, UIAxis direction) {
  if (direction == UI_AXIS_HORIZONTAL) {
    return size.y;
  } else {
    return size.x;
  }
}

static f32 ui_flex_get_main_size(Vec2 size, UIAxis direction) {
  if (direction == UI_AXIS_HORIZONTAL) {
    return size.x;
  } else {
    return size.y;
  }
}

static void ui_flex_layout(UIWidget *widget, UIBoxConstraints constraints) {
  UIFlexState *state = ui_widget_get_state(widget, UIFlexState);
  UIFlexLayoutSize sizes = ui_flex_compute_size(
      widget, state->direction, state->main_axis_size,
      state->cross_axis_alignment, state->spacing, constraints);
  f32 cross_axis_extent = sizes.size.cross;
  widget->size =
      convert_size(vec2(sizes.size.main, sizes.size.cross), state->direction);
  // TODO: Handle overflow.

  f32 remaining_space = f32_max(0.0f, sizes.main_axis_free_space);
  // TODO: Handle text direction and vertical direction.
  f32 leading_space;
  f32 between_space;
  ui_flex_distribute_space(state->main_axis_alignment, remaining_space,
                           widget->child_count, /* flipped= */ false,
                           state->spacing, &leading_space, &between_space);

  // Position all children in visual order: starting from the top-left child and
  // work towards the child that's farthest away from the origin.
  f32 child_main_position = leading_space;
  for (UIWidget *child = widget->first; child; child = child->next) {
    f32 child_cross_position = ui_flex_get_child_cross_axis_offset(
        state->cross_axis_alignment,
        cross_axis_extent -
            ui_flex_get_cross_size(child->size, state->direction),
        /* flipped= */ false);
    if (state->direction == UI_AXIS_HORIZONTAL) {
      child->offset = vec2(child_main_position, child_cross_position);
    } else {
      child->offset = vec2(child_cross_position, child_main_position);
    }
    child_main_position +=
        ui_flex_get_main_size(child->size, state->direction) + between_space;
  }
}

UIWidgetClass ui_flex_class = {
    .name = "Flex",
    .flags = UI_WIDGET_MANY_CHILDREN,
    .state_size = sizeof(UIFlexState),
    .layout = ui_flex_layout,
};

void ui_flex_begin(const UIFlexProps *props) {
  UIWidget *widget = ui_widget_begin(&ui_flex_class, props->key);
  UIFlexState *state = ui_widget_get_state(widget, UIFlexState);
  state->direction = props->direction;
  state->main_axis_alignment = props->main_axis_alignment;
  state->main_axis_size = props->main_axis_size;
  state->cross_axis_alignment = props->cross_axis_alignment;
  state->spacing = props->spacing;
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIColumn
///
UIWidgetClass ui_column_class = {
    .name = "Column",
    .flags = UI_WIDGET_MANY_CHILDREN,
    .state_size = sizeof(UIFlexState),
    .layout = ui_flex_layout,
};

void ui_column_begin(const UIColumnProps *props) {
  UIWidget *widget = ui_widget_begin(&ui_column_class, props->key);
  UIFlexState *state = ui_widget_get_state(widget, UIFlexState);
  state->direction = UI_AXIS_VERTICAL;
  state->main_axis_alignment = props->main_axis_alignment;
  state->main_axis_size = props->main_axis_size;
  state->cross_axis_alignment = props->cross_axis_alignment;
  state->spacing = props->spacing;
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIRow
///
UIWidgetClass ui_row_class = {
    .name = "Row",
    .flags = UI_WIDGET_MANY_CHILDREN,
    .state_size = sizeof(UIFlexState),
    .layout = ui_flex_layout,
};

void ui_row_begin(const UIRowProps *props) {
  UIWidget *widget = ui_widget_begin(&ui_row_class, props->key);
  UIFlexState *state = ui_widget_get_state(widget, UIFlexState);
  state->direction = UI_AXIS_HORIZONTAL;
  state->main_axis_alignment = props->main_axis_alignment;
  state->main_axis_size = props->main_axis_size;
  state->cross_axis_alignment = props->cross_axis_alignment;
  state->spacing = props->spacing;
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIPointerListener
///
typedef struct UIPointerEventQueueEntry UIPointerEventQueueEntry;
struct UIPointerEventQueueEntry {
  UIPointerEventQueueEntry *prev;
  UIPointerEventQueueEntry *next;
  UIPointerEvent event;
};

typedef struct UIPointerEventQueue {
  UIPointerEventQueueEntry *first;
  UIPointerEventQueueEntry *last;
} UIPointerEventQueue;

typedef struct UIPointerListenerState {
  UIHitTestBehaviour behaviour;
  UIPointerEventQueue event_queue;
} UIPointerListenerState;

static bool ui_pointer_listener_hit_test(UIWidget *widget,
                                         UIHitTestResult *result,
                                         Arena *arena) {
  UIPointerListenerState *state =
      ui_widget_get_state(widget, UIPointerListenerState);
  return ui_widget_hit_test_by_behaviour(widget, state->behaviour, result,
                                         arena);
}

static void ui_pointer_listener_handle_pointer_event(
    UIWidget *widget, const UIPointerEvent *event) {
  UIPointerListenerState *state =
      ui_widget_get_state(widget, UIPointerListenerState);
  UIFrame *frame = ui_state_get_current_frame(ui_state_get());
  UIPointerEventQueueEntry *entry =
      arena_push_struct(&frame->arena, UIPointerEventQueueEntry);
  entry->event = *event;
  DLL_APPEND(state->event_queue.first, state->event_queue.last, entry, prev,
             next);
}

UIWidgetClass ui_pointer_listener_class = {
    .name = "PointerListener",
    .state_size = sizeof(UIPointerListenerState),
    .hit_test = ui_pointer_listener_hit_test,
    .handle_pointer_event = ui_pointer_listener_handle_pointer_event,
};

void ui_pointer_listener_begin(const UIPointerListenerProps *props) {
  UIWidget *widget = ui_widget_begin(&ui_pointer_listener_class, props->key);
  UIPointerListenerState *state =
      ui_widget_get_state(widget, UIPointerListenerState);
  state->behaviour = props->behaviour;

  UIPointerEventO down = ui_pointer_event_none();
  UIPointerEventO move = ui_pointer_event_none();
  UIPointerEventO up = ui_pointer_event_none();
  UIPointerEventO enter = ui_pointer_event_none();
  UIPointerEventO hover = ui_pointer_event_none();
  UIPointerEventO exit = ui_pointer_event_none();
  UIPointerEventO cancel = ui_pointer_event_none();
  UIPointerEventO scroll = ui_pointer_event_none();
  for (UIPointerEventQueueEntry *entry = state->event_queue.first; entry;
       entry = entry->next) {
    switch (entry->event.type) {
      case UI_POINTER_EVENT_DOWN: {
        down = ui_pointer_event_some(entry->event);
      } break;

      case UI_POINTER_EVENT_MOVE: {
        move = ui_pointer_event_some(entry->event);
      } break;

      case UI_POINTER_EVENT_UP: {
        up = ui_pointer_event_some(entry->event);
      } break;

      case UI_POINTER_EVENT_ENTER: {
        enter = ui_pointer_event_some(entry->event);
      } break;

      case UI_POINTER_EVENT_HOVER: {
        hover = ui_pointer_event_some(entry->event);
      } break;

      case UI_POINTER_EVENT_EXIT: {
        exit = ui_pointer_event_some(entry->event);
      } break;

      case UI_POINTER_EVENT_CANCEL: {
        cancel = ui_pointer_event_some(entry->event);
      } break;

      case UI_POINTER_EVENT_SCROLL: {
        scroll = ui_pointer_event_some(entry->event);
      } break;

      default: {
      } break;
    }
  }

  if (props->down) {
    *props->down = down;
  }
  if (props->move) {
    *props->move = move;
  }
  if (props->up) {
    *props->up = up;
  }
  if (props->enter) {
    *props->enter = enter;
  }
  if (props->hover) {
    *props->hover = hover;
  }
  if (props->exit) {
    *props->exit = exit;
  }
  if (props->cancel) {
    *props->cancel = cancel;
  }
  if (props->scroll) {
    *props->scroll = scroll;
  }

  state->event_queue = (UIPointerEventQueue){0};
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIMouseRegion
///
typedef struct UIMouseRegionState {
  UIPointerEventO enter;
  UIPointerEventO hover;
  UIPointerEventO exit;
} UIMouseRegionState;

static void ui_mouse_region_handle_pointer_event(UIWidget *widget,
                                                 const UIPointerEvent *event) {
  UIMouseRegionState *state = ui_widget_get_state(widget, UIMouseRegionState);
  switch (event->type) {
    case UI_POINTER_EVENT_ENTER: {
      state->enter = ui_pointer_event_some(*event);
    } break;
    case UI_POINTER_EVENT_HOVER: {
      state->hover = ui_pointer_event_some(*event);
    } break;
    case UI_POINTER_EVENT_EXIT: {
      state->exit = ui_pointer_event_some(*event);
    } break;
    default: {
    } break;
  }
}

UIWidgetClass ui_mouse_region_class = {
    .name = "MouseRegion",
    .state_size = sizeof(UIMouseRegionState),
    .handle_pointer_event = ui_mouse_region_handle_pointer_event,
};

void ui_mouse_region_begin(const UIMouseRegionProps *props) {
  UIWidget *widget = ui_widget_begin(&ui_mouse_region_class, props->key);
  UIMouseRegionState *state = ui_widget_get_state(widget, UIMouseRegionState);
  if (props->enter) {
    *props->enter = state->enter;
  }
  if (props->hover) {
    *props->hover = state->hover;
  }
  if (props->exit) {
    *props->exit = state->exit;
  }

  state->enter = ui_pointer_event_none();
  state->hover = ui_pointer_event_none();
  state->exit = ui_pointer_event_none();
}

////////////////////////////////////////////////////////////////////////////////
///
/// UITapGestureDetector
///

typedef struct UITapGestureDetectorState {
  UIHitTestBehaviour behaviour;
  u32 button;
  u32 pointer;
  UIGestureDetailO tap_down;
  UIGestureDetailO tap_up;
  UIGestureDetailO tap;
} UITapGestureDetectorState;

static bool ui_tap_gesture_detector_hit_test(UIWidget *widget,
                                             UIHitTestResult *result,
                                             Arena *arena) {
  UITapGestureDetectorState *state =
      ui_widget_get_state(widget, UITapGestureDetectorState);
  return ui_widget_hit_test_by_behaviour(widget, state->behaviour, result,
                                         arena);
}

static void ui_tap_gesture_detector_handle_pointer_event(
    UIWidget *widget, const UIPointerEvent *event) {
  UITapGestureDetectorState *state =
      ui_widget_get_state(widget, UITapGestureDetectorState);

  // TODO: check for state: ready, possible, defunct?
  switch (event->type) {
    case UI_POINTER_EVENT_DOWN: {
      if ((event->button & state->button) && !state->pointer) {
        // TODO: add to gesture arena
        state->pointer = event->pointer;
        state->tap_down = ui_gesture_detail_some((UIGestureDetail){
            .local_position = event->local_position,
        });
      }
    } break;

    case UI_POINTER_EVENT_UP: {
      // TODO: claim win
      if (event->pointer == state->pointer) {
        state->pointer = 0;

        if (vec2_contains(event->local_position, vec2_zero(), widget->size)) {
          state->tap_up = ui_gesture_detail_some((UIGestureDetail){
              .local_position = event->local_position,
          });
          state->tap = ui_gesture_detail_some((UIGestureDetail){
              .local_position = event->local_position,
          });
        }
      }
    } break;

    default: {
    } break;
  }
}

UIWidgetClass ui_tap_gesture_detector_class = {
    .name = "TapGestureDetector",
    .state_size = sizeof(UITapGestureDetectorState),
    .hit_test = ui_tap_gesture_detector_hit_test,
    .handle_pointer_event = ui_tap_gesture_detector_handle_pointer_event,
};

void ui_tap_gesture_detector_begin(const UITapGestureDetectorProps *props) {
  UIWidget *widget =
      ui_widget_begin(&ui_tap_gesture_detector_class, props->key);
  UITapGestureDetectorState *state =
      ui_widget_get_state(widget, UITapGestureDetectorState);

  state->behaviour = props->behaviour;

  if (props->tap_down || props->tap_up || props->tap) {
    state->button |= UI_BUTTON_PRIMARY;
  } else {
    state->button &= (~UI_BUTTON_PRIMARY);
  }

  if (props->tap_down) {
    *props->tap_down = state->tap_down;
  }
  state->tap_down = ui_gesture_detail_none();

  if (props->tap_up) {
    *props->tap_up = state->tap_up;
  }
  state->tap_up = ui_gesture_detail_none();

  if (props->tap) {
    *props->tap = state->tap;
  }
  state->tap = ui_gesture_detail_none();
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIGestureDetector
///
typedef struct UIGestureDetectorState {
  u32 down_button;
  Vec2 down_local_position;
  Vec2 last_drag_local_position;
  bool dragging;
} UIGestureDetectorState;

UIWidgetClass ui_gesture_detector_class = {
    .name = "GestureDetector",
    .state_size = sizeof(UIGestureDetectorState),
};

void ui_gesture_detector_begin(const UIGestureDetectorProps *props) {
  UIWidget *widget = ui_widget_begin(&ui_gesture_detector_class, props->key);
  UIGestureDetectorState *state =
      ui_widget_get_state(widget, UIGestureDetectorState);

  UIPointerEventO down;
  UIPointerEventO move;
  UIPointerEventO up;
  ui_pointer_listener_begin(&(UIPointerListenerProps){
      .behaviour = props->behaviour,
      .down = &down,
      .move = &move,
      .up = &up,
  });

  UIGestureDetailO tap_down = ui_gesture_detail_none();
  UIGestureDetailO tap_up = ui_gesture_detail_none();
  UIGestureDetailO tap = ui_gesture_detail_none();
  UIGestureDetailO drag_start = ui_gesture_detail_none();
  UIGestureDetailO drag_update = ui_gesture_detail_none();
  UIGestureDetailO drag_end = ui_gesture_detail_none();

  if (down.present) {
    if (down.value.button & UI_BUTTON_PRIMARY) {
      tap_down = ui_gesture_detail_some((UIGestureDetail){
          .local_position = down.value.local_position,
      });
    }

    state->down_button = down.value.button;
    state->down_local_position = down.value.local_position;
  }

  if (move.present) {
    if (state->down_button & UI_BUTTON_PRIMARY) {
      if (!state->dragging) {
        state->dragging = true;
        drag_start = ui_gesture_detail_some((UIGestureDetail){
            .local_position = move.value.local_position,
        });
      } else {
        drag_update = ui_gesture_detail_some((UIGestureDetail){
            .local_position = move.value.local_position,
            .delta = vec2_sub(move.value.local_position,
                              state->last_drag_local_position),
        });
      }
      state->last_drag_local_position = move.value.local_position;
    }
  }

  if (up.present) {
    if (up.value.button & UI_BUTTON_PRIMARY) {
      tap_up = ui_gesture_detail_some((UIGestureDetail){
          .local_position = up.value.local_position,
      });
    }

    if (vec2_contains(up.value.local_position, vec2_zero(), widget->size)) {
      if ((state->down_button & UI_BUTTON_PRIMARY)) {
        tap = ui_gesture_detail_some((UIGestureDetail){
            .local_position = state->down_local_position,
        });
      }
    }

    if (state->dragging) {
      drag_end = ui_gesture_detail_some((UIGestureDetail){
          .local_position = move.value.local_position,
          .delta =
              vec2_sub(move.value.local_position, state->down_local_position),
      });

      state->dragging = false;
    }

    state->down_button = 0;
  }

  if (props->tap_down) {
    *props->tap_down = tap_down;
  }
  if (props->tap_up) {
    *props->tap_up = tap_up;
  }
  if (props->tap) {
    *props->tap = tap;
  }
  if (props->drag_start) {
    *props->drag_start = drag_start;
  }
  if (props->drag_update) {
    *props->drag_update = drag_update;
  }
  if (props->drag_end) {
    *props->drag_end = drag_end;
  }
}

void ui_gesture_detector_end(void) {
  ui_pointer_listener_end();
  ui_widget_end(&ui_gesture_detector_class);
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIText
///
typedef struct UITextState {
  UIBoxConstraints constraints;
  Str8 text;
  UIColor color;
  f32 font_size;
} UITextState;

static void ui_text_layout(UIWidget *widget, UIBoxConstraints constraints) {
  DEBUG_ASSERTF(!widget->first, "UIText should be a leaf node");

  UITextState *state = ui_widget_get_state(widget, UITextState);

  state->constraints = constraints;

  TextMetrics metrics =
      layout_text_str8(state->text, state->font_size, constraints.min_width,
                       constraints.max_width);
  // TODO: Handle overflow.
  widget->size = ui_box_constraints_constrain(constraints, metrics.size);
}

static void ui_text_paint(UIWidget *widget, UIPaintingContext *context,
                          Vec2 offset) {
  (void)context;

  UITextState *state = ui_widget_get_state(widget, UITextState);

  draw_text_str8(offset, state->text, state->font_size,
                 state->constraints.min_width, state->constraints.max_width,
                 state->color);
}

UIWidgetClass ui_text_class = {
    .name = "Text",
    .state_size = sizeof(UITextState),
    .layout = ui_text_layout,
    .paint = ui_text_paint,
    .hit_test = ui_widget_hit_test_opaque,
};

void ui_text(const UITextProps *props) {
  UIWidget *widget = ui_widget_begin(&ui_text_class, props->key);
  UITextState *state = ui_widget_get_state(widget, UITextState);
  state->text = props->text;

  // TODO: Get default text style from widget tree.
  f32 font_size = 13;
  if (props->style.present) {
    if (props->style.value.font_size.present) {
      font_size = props->style.value.font_size.value;
    }
  }
  state->font_size = font_size;

  // TODO: Get default text style from widget tree.
  UIColor color = ui_color(1, 1, 1, 1);
  if (props->style.present) {
    if (props->style.value.color.present) {
      color = props->style.value.color.value;
    }
  }
  state->color = color;

  ui_widget_end(&ui_text_class);
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIButton
///
typedef struct UIButtonState {
  bool down;
  bool hovering;
} UIButtonState;

UIWidgetClass ui_button_class = {
    .name = "Button",
    .state_size = sizeof(UIButtonState),
};

void ui_button(UIButtonProps *props) {
  UIColor fill_color = ui_color_zero();
  if (props->fill_color.present) {
    fill_color = props->fill_color.value;
  }
  UIColor hover_color = ui_color_zero();
  if (props->hover_color.present) {
    hover_color = props->hover_color.value;
  }
  UIColor splash_color = ui_color_zero();
  if (props->splash_color.present) {
    splash_color = props->splash_color.value;
  }

  UIWidget *widget = ui_widget_begin(&ui_button_class, props->key);
  UIButtonState *state = ui_widget_get_state(widget, UIButtonState);

  UIPointerEventO enter;
  UIPointerEventO exit;
  ui_mouse_region_begin(&(UIMouseRegionProps){
      .enter = &enter,
      .exit = &exit,
  });
  if (enter.present) {
    state->hovering = true;
  } else if (exit.present) {
    state->hovering = false;
  }

  UIGestureDetailO down;
  UIGestureDetailO up;
  UIGestureDetailO tap;
  ui_gesture_detector_begin(&(UIGestureDetectorProps){
      .tap_down = &down,
      .tap_up = &up,
      .tap = &tap,
  });

  if (props->pressed) {
    *props->pressed = tap.present;
  }

  if (down.present) {
    state->down = true;
  }
  if (up.present) {
    state->down = false;
  }

  ui_colored_box_begin(&(UIColoredBoxProps){
      .color = state->down ? splash_color
                           : (state->hovering ? hover_color : fill_color),
  });

  UIEdgeInsets padding = ui_edge_insets_zero();
  if (props->padding.present) {
    padding = props->padding.value;
  }
  ui_padding_begin(&(UIPaddingProps){
      .padding = padding,
  });

  ui_text(&(UITextProps){
      .text = props->text,
      .style = props->text_style,
  });

  ui_padding_end();
  ui_colored_box_end();
  ui_gesture_detector_end();
  ui_mouse_region_end();
  ui_widget_end(&ui_button_class);
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIViewport
///
typedef struct UIViewportState {
  UIAxisDirection axis_direction;
  UIAxisDirection cross_axis_direction;
  f32 anchor;
  f32 cache_extent;
  UIViewportOffset offset;
  UIViewportOffset next_offset;
  f32 max_scroll_extent;
  f32 max_scroll_offset;
  bool has_visual_overflow;
} UIViewportState;

static UIScrollDirection ui_scroll_direction_apply_growth_direction(
    UIScrollDirection self, UIGrowthDirection growth) {
  if (growth == UI_GROWTH_DIRECTION_REVERSE) {
    return ui_scroll_direction_flip(self);
  }

  return self;
}

static f32 ui_viewport_layout_children(
    UIWidget *widget, UIViewportState *state, UIWidget *child,
    f32 scroll_offset, f32 overlap, f32 layout_offset,
    f32 remaining_painting_extent, f32 main_axis_extent, f32 cross_axis_extent,
    UIGrowthDirection growth_direction, f32 remaining_cache_extent,
    f32 cache_origin) {
  ASSERT(f32_is_finite(scroll_offset));
  ASSERT(scroll_offset >= 0);
  f32 initial_layout_offset = layout_offset;
  UIScrollDirection scroll_direction =
      ui_scroll_direction_apply_growth_direction(state->offset.scroll_direction,
                                                 growth_direction);
  f32 max_paint_offset = layout_offset + overlap;
  f32 preceeding_scroll_extent = 0;

  while (child) {
    f32 sliver_scroll_offset = scroll_offset < 0 ? 0 : scroll_offset;
    f32 corrected_cache_origin = f32_max(cache_origin, -sliver_scroll_offset);
    f32 cache_extent_correction = cache_origin - corrected_cache_origin;
    ASSERT(sliver_scroll_offset >= f32_abs(corrected_cache_origin));
    ASSERT(corrected_cache_origin <= 0);
    ASSERT(sliver_scroll_offset >= 0);
    ASSERT(cache_extent_correction <= 0);

    UIParentDataSliver *data = ui_widget_set_parent_data(
        child, UI_PARENT_DATA_SLIVER, UIParentDataSliver);
    data->layout_offset = layout_offset;
    data->next_scroll_offset = state->next_offset.points;
    ui_widget_layout_sliver(
        child,
        &(UISliverConstraints){
            .axis_direction = state->axis_direction,
            .growth_direction = growth_direction,
            .scroll_direction = scroll_direction,
            .scroll_offset = sliver_scroll_offset,
            .preceeding_scroll_extent = preceeding_scroll_extent,
            .overlap = max_paint_offset - layout_offset,
            .remaining_paint_extent =
                f32_max(0, remaining_painting_extent - layout_offset +
                               initial_layout_offset),
            .cross_axis_extent = cross_axis_extent,
            .cross_axis_direction = state->cross_axis_direction,
            .main_axis_extent = main_axis_extent,
            .remaining_cache_extent =
                f32_max(0, remaining_cache_extent + cache_extent_correction),
            .cache_origin = corrected_cache_origin,
        },
        &data->geometry);

    if (data->geometry.scroll_offset_correction != 0) {
      return data->geometry.scroll_offset_correction;
    }

    state->has_visual_overflow |= data->geometry.has_visual_overflow;

    f32 effective_layout_offset = layout_offset + data->geometry.paint_origin;
    switch (ui_axis_direction_apply_growth_direction(state->axis_direction,
                                                     growth_direction)) {
      case UI_AXIS_DIRECTION_UP: {
        child->offset = vec2(
            0, widget->size.y - layout_offset - data->geometry.paint_extent);
      } break;

      case UI_AXIS_DIRECTION_DOWN: {
        child->offset = vec2(0, layout_offset);
      } break;

      case UI_AXIS_DIRECTION_LEFT: {
        child->offset = vec2(
            widget->size.x - layout_offset - data->geometry.paint_extent, 0);
      } break;

      case UI_AXIS_DIRECTION_RIGHT: {
        child->offset = vec2(layout_offset, 0);
      } break;

      default: {
        UNREACHABLE;
      } break;
    }

    max_paint_offset =
        f32_max(effective_layout_offset + data->geometry.paint_extent,
                max_paint_offset);
    scroll_offset -= data->geometry.scroll_extent;
    preceeding_scroll_extent += data->geometry.scroll_extent;
    layout_offset += data->geometry.layout_extent;
    if (data->geometry.cache_extent != 0) {
      remaining_cache_extent -=
          data->geometry.cache_extent - cache_extent_correction;
      cache_origin =
          f32_min(corrected_cache_origin + data->geometry.cache_extent, 0);
    }

    child = child->next;
  }

  state->max_scroll_offset +=
      f32_max(0, preceeding_scroll_extent - main_axis_extent);
  state->max_scroll_extent += preceeding_scroll_extent;

  return 0;
}

static f32 ui_viewport_attempt_layout(UIWidget *widget, UIViewportState *state,
                                      f32 main_axis_extent,
                                      f32 cross_axis_extent, f32 offset) {
  f32 center_offset = main_axis_extent * state->anchor - offset;
  f32 reverse_direction_remaining_paint_extent =
      f32_clamp(center_offset, 0, main_axis_extent);
  f32 forward_direction_remaining_paint_extent =
      f32_clamp(main_axis_extent - center_offset, 0, main_axis_extent);

  f32 cache_extent = state->cache_extent;
  f32 full_cache_extent = main_axis_extent + 2 * cache_extent;
  f32 center_cache_offset = center_offset + cache_extent;
  f32 reverse_direction_remaining_cache_extent =
      f32_clamp(center_cache_offset, 0, full_cache_extent);
  f32 forward_direction_remaining_cache_extent =
      f32_clamp(full_cache_extent - center_offset, 0, full_cache_extent);

  state->has_visual_overflow = false;
  state->max_scroll_offset = cache_extent;
  state->max_scroll_extent = cache_extent;
  return ui_viewport_layout_children(
      widget, state, /* child= */ widget->first,
      /* scroll_offset= */ f32_max(0, -center_offset),
      /* overlap= */ f32_min(0, -center_offset),
      /* layout_offset= */ center_offset >= main_axis_extent
          ? center_offset
          : reverse_direction_remaining_paint_extent,
      forward_direction_remaining_paint_extent, main_axis_extent,
      cross_axis_extent, UI_GROWTH_DIRECTION_FORWARD,
      forward_direction_remaining_cache_extent,
      f32_clamp(center_offset, -cache_extent, 0));
}

static void ui_viewport_layout(UIWidget *widget, UIBoxConstraints constraints) {
  UIViewportState *state = ui_widget_get_state(widget, UIViewportState);

  Vec2 size = ui_box_constraints_get_biggest(constraints);
  widget->size = size;

  if (!widget->first) {
    return;
  }

  if (f32_is_infinity(size.x) || f32_is_infinity(size.y)) {
    DEBUG_ASSERTF(false, "Cannot layout Viewport with infinity space.");
    return;
  }

  f32 main_axis_extent = size.x;
  f32 cross_axis_extent = size.y;
  if (ui_axis_direction_to_axis(state->axis_direction) == UI_AXIS_VERTICAL) {
    main_axis_extent = size.y;
    cross_axis_extent = size.x;
  }

  u32 max_layout_counts = 10 * widget->child_count;
  u32 layout_index = 0;
  for (; layout_index < max_layout_counts; ++layout_index) {
    f32 correction =
        ui_viewport_attempt_layout(widget, state, main_axis_extent,
                                   cross_axis_extent, state->offset.points);
    if (correction != 0.0f) {
      UNREACHABLE;
    } else {
      break;
    }
  }
  ASSERT(layout_index < max_layout_counts);
}

static void ui_viewport_paint(UIWidget *widget, UIPaintingContext *context,
                              Vec2 offset) {
  UIViewportState *state = ui_widget_get_state(widget, UIViewportState);

  bool should_clip = state->has_visual_overflow;
  if (should_clip) {
    push_clip_rect(offset, vec2_add(offset, widget->size));
  }

  for (UIWidget *child = widget->first; child; child = child->next) {
    UIParentDataSliver *data = ui_widget_get_parent_data(
        child, UI_PARENT_DATA_SLIVER, UIParentDataSliver);
    ASSERT(data);
    if (data->geometry.paint_extent > 0) {
      ui_widget_paint_child_default(child, context, offset);
    }
  }

  if (should_clip) {
    pop_clip_rect();
  }
}

UIWidgetClass ui_viewport_class = {
    .name = "Viewport",
    .flags = UI_WIDGET_MANY_CHILDREN,
    .state_size = sizeof(UIViewportState),
    .layout = ui_viewport_layout,
    .paint = ui_viewport_paint,
    .hit_test = ui_widget_hit_test_opaque,
};

void ui_viewport_begin(const UIViewportProps *props) {
  UIWidget *widget = ui_widget_begin(&ui_viewport_class, props->key);
  UIViewportState *state = ui_widget_get_state(widget, UIViewportState);
  state->axis_direction = props->axis_direction;
  state->cross_axis_direction = props->cross_axis_direction;
  state->anchor = props->anchor;
  // Artificially delay the change of offset by 1 frame so that slivers using
  // builder pattern have time to build new children.
  state->offset = state->next_offset;
  state->next_offset = props->offset;
  state->cache_extent = props->cache_extent;
  if (props->max_scroll_offset) {
    *props->max_scroll_offset = state->max_scroll_offset;
  }
  if (props->max_scroll_extent) {
    *props->max_scroll_extent = state->max_scroll_extent;
  }
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIScrollable
///
typedef struct UIScrollableState {
  UIAxisDirection axis_direction;
  UIAxisDirection cross_axis_direction;
  f32 cache_extent;
  f32 scroll_offset;
  f32 target_scroll_offset;
  f32 max_scroll_offset;
  f32 max_scroll_extent;
  bool handle_scrolling;
  bool hovering;
  f32 handle_down_offset;
  f32 *scroll_output;
} UIScrollableState;

static void ui_scrollable_mount(UIWidget *widget) {
  (void)widget;

  // Set rebuild so the scrollbar can have correct size on first appearance.
  ui_set_rebuild(true);
}

UIWidgetClass ui_scrollable_class = {
    .name = "Scrollable",
    .state_size = sizeof(UIScrollableState),
    .mount = ui_scrollable_mount,
};

void ui_scrollable_begin(const UIScrollableProps *props) {
  UIWidget *widget = ui_widget_begin(&ui_scrollable_class, props->key);
  UIScrollableState *state = ui_widget_get_state(widget, UIScrollableState);
  state->axis_direction = props->axis_direction;
  state->cross_axis_direction = props->cross_axis_direction;
  state->cache_extent = props->cache_extent;

  if (props->scroll) {
    state->target_scroll_offset =
        f32_clamp(*props->scroll, 0, state->max_scroll_offset);
  }
  state->scroll_output = props->scroll;

  state->scroll_offset =
      ui_animate_fast_f32(state->scroll_offset, state->target_scroll_offset);

  UIPointerEventO scroll;
  ui_pointer_listener_begin(&(UIPointerListenerProps){
      .scroll = &scroll,
  });

  ui_row_begin(&(UIRowProps){0});
  ui_expanded_begin(&(UIExpandedProps){.flex = 1});

  ui_viewport_begin(&(UIViewportProps){
      .axis_direction = props->axis_direction,
      .cross_axis_direction = props->cross_axis_direction,
      .offset =
          (UIViewportOffset){
              .points = state->scroll_offset,
          },
      .max_scroll_offset = &state->max_scroll_offset,
      .max_scroll_extent = &state->max_scroll_extent,
      .cache_extent = props->cache_extent,
  });

  if (scroll.present) {
    state->target_scroll_offset += scroll.value.scroll_delta.y;
  }
}

static void ui_scrollable_scrollbar_handle(UIScrollableState *state,
                                           f32 handle_size) {
  UIPointerEventO enter;
  UIPointerEventO exit;
  ui_mouse_region_begin(&(UIMouseRegionProps){
      .enter = &enter,
      .exit = &exit,
  });
  if (enter.present) {
    state->hovering = true;
  } else if (exit.present) {
    state->hovering = false;
  }
  UIGestureDetailO tap_down;
  UIGestureDetailO tap_up;
  ui_gesture_detector_begin(&(UIGestureDetectorProps){
      .tap_down = &tap_down,
      .tap_up = &tap_up,
  });
  if (tap_down.present) {
    state->handle_scrolling = true;
    state->handle_down_offset = tap_down.value.local_position.y;
  }
  if (tap_up.present) {
    state->handle_scrolling = false;
  }
  ui_container_begin(&(UIContainerProps){
      .color = ui_color_some(state->hovering ? ui_color(0.58, 0.58, 0.58, 1)
                                             : ui_color(0.75, 0.75, 0.75, 1)),
      .height = f32_some(handle_size),
  });
  ui_container_end();
  ui_gesture_detector_end();
  ui_mouse_region_end();
}

static void ui_scrollable_scrollbar(UIWidget *widget,
                                    UIScrollableState *state) {
  if (state->max_scroll_offset <= 0) {
    return;
  }
  f32 ratio = widget->size.y / state->max_scroll_extent;
  f32 main_axis_extent = state->max_scroll_extent - state->max_scroll_offset;
  f32 scroll_track_width = 10;
  f32 padding_top = state->scroll_offset * ratio;
  f32 handle_size = f32_max(4, main_axis_extent * ratio);

  UIGestureDetailO tap_down;
  UIGestureDetailO drag_start;
  UIGestureDetailO drag_update;
  ui_gesture_detector_begin(&(UIGestureDetectorProps){
      .tap_down = &tap_down,
      .drag_start = &drag_start,
      .drag_update = &drag_update,
  });
  ui_container_begin(&(UIContainerProps){
      .color = ui_color_some(ui_color(0.96, 0.96, 0.96, 1)),
      .alignment = ui_alignment_some(ui_alignment_top_center()),
      .padding = ui_edge_insets_some(ui_edge_insets(0, 0, padding_top, 0)),
      .width = f32_some(scroll_track_width),
  });
  ui_scrollable_scrollbar_handle(state, handle_size);
  ui_container_end();
  ui_gesture_detector_end();

  f32 handle_offset = main_axis_extent / 2.0f;
  if (state->handle_scrolling) {
    handle_offset = state->handle_down_offset / ratio;
  }

  if (tap_down.present) {
    state->target_scroll_offset =
        tap_down.value.local_position.y / ratio - handle_offset;
  }
  if (drag_start.present) {
    state->target_scroll_offset =
        drag_start.value.local_position.y / ratio - handle_offset;
  }
  if (drag_update.present) {
    state->target_scroll_offset =
        drag_update.value.local_position.y / ratio - handle_offset;
  }
}

void ui_scrollable_end(void) {
  ui_viewport_end();
  ui_expanded_end();

  UIWidget *widget = ui_widget_find_first_ancestor(ui_widget_get_current(),
                                                   &ui_scrollable_class);
  ASSERT(widget);
  UIScrollableState *state = ui_widget_get_state(widget, UIScrollableState);
  ui_scrollable_scrollbar(widget, state);

  state->target_scroll_offset =
      f32_clamp(state->target_scroll_offset, 0, state->max_scroll_offset);
  if (state->scroll_output) {
    *state->scroll_output = state->target_scroll_offset;
  }

  ui_row_end();
  ui_pointer_listener_end();
  ui_widget_end(&ui_scrollable_class);
}

////////////////////////////////////////////////////////////////////////////////
///
/// UISliverFixedExtentList
///
typedef struct UISliverFixedExtentListState {
  bool init;
  UISliverConstraints last_constraints;
  f32 next_scroll_offset;
  f32 item_extent;
  i32 item_count;
  bool has_builder;
} UISliverFixedExtentListState;

static void ui_sliver_fixed_extent_list_mount(UIWidget *widget) {
  UISliverFixedExtentListState *state =
      ui_widget_get_state(widget, UISliverFixedExtentListState);
  if (state->has_builder) {
    // If call site uses builder, set rebuild because the builder is empty
    // for the first frame.
    ui_set_rebuild(true);
  }
}

static i32 ui_sliver_fixed_extent_list_get_min_child_index(f32 item_extent,
                                                           f32 scroll_offset) {
  if (item_extent <= 0.0f) {
    return 0;
  }
  f32 actual = scroll_offset / item_extent;
  i32 round = f32_round(actual);
  if (f32_abs(actual * item_extent - round * item_extent) <
      UI_PRECISION_ERROR_TOLERANCE) {
    return round;
  }
  return f32_floor(actual);
}

static i32 ui_sliver_fixed_extent_list_get_max_child_index(f32 item_extent,
                                                           f32 scroll_offset) {
  if (item_extent <= 0.0f) {
    return 0;
  }

  f32 actual = scroll_offset / item_extent - 1;
  i32 round = f32_round(actual);
  if (f32_abs(actual * item_extent - round * item_extent) <
      UI_PRECISION_ERROR_TOLERANCE) {
    return f32_max(0, round);
  }

  return f32_max(0, f32_ceil(actual));
}

static void ui_sliver_fixed_extent_list_calc_item_count(
    f32 item_extent, f32 scroll_offset, f32 remaining_extent, i32 *first_index,
    i32 *target_last_index) {
  *first_index = ui_sliver_fixed_extent_list_get_min_child_index(item_extent,
                                                                 scroll_offset);
  ASSERT(*first_index >= 0);

  f32 target_end_scroll_offset = scroll_offset + remaining_extent;
  if (f32_is_finite(target_end_scroll_offset)) {
    *target_last_index = ui_sliver_fixed_extent_list_get_max_child_index(
        item_extent, target_end_scroll_offset);
  } else {
    *target_last_index = I32_MAX;
  }
}

static void ui_sliver_fixed_extent_list_layout_sliver(
    UIWidget *widget, const UISliverConstraints *constraints,
    UISliverGeometry *geometry) {
  UISliverFixedExtentListState *state =
      ui_widget_get_state(widget, UISliverFixedExtentListState);

  f32 scroll_offset = constraints->scroll_offset + constraints->cache_origin;
  ASSERT(scroll_offset >= 0.0f);
  f32 remaining_extent = constraints->remaining_cache_extent;
  ASSERT(remaining_extent >= 0.0f);

  f32 item_extent = state->item_extent;
  i32 first_index;
  i32 target_last_index;
  ui_sliver_fixed_extent_list_calc_item_count(item_extent, scroll_offset,
                                              remaining_extent, &first_index,
                                              &target_last_index);

  i32 item_count = state->item_count;
  i32 child_index = 0;
  if (state->has_builder) {
    child_index = first_index;
  }

  for (UIWidget *child = widget->first; child; child = child->next) {
    UIBoxConstraints child_constraints =
        ui_sliver_constraints_as_box_constraints(constraints, item_extent,
                                                 item_extent);
    ui_widget_layout(child, child_constraints);
    f32 layout_offset = child_index * item_extent;
    child->offset = vec2(0, layout_offset - scroll_offset);

    child_index += 1;
  }

  f32 leading_scroll_offset = scroll_offset;
  f32 trailing_scroll_offset =
      scroll_offset + (target_last_index - first_index + 1) * item_extent;

  f32 scroll_extent = item_count * item_extent;
  f32 paint_extent = ui_sliver_constraints_calc_paint_offset(
      constraints, leading_scroll_offset, trailing_scroll_offset);
  f32 cache_extent = ui_sliver_constraints_calc_cache_offset(
      constraints, leading_scroll_offset, trailing_scroll_offset);

  widget->size = vec2(constraints->cross_axis_extent, paint_extent);

  f32 target_end_scroll_offset_for_paint =
      constraints->scroll_offset + constraints->remaining_paint_extent;
  bool has_target_last_index_for_paint =
      f32_is_finite(target_end_scroll_offset_for_paint);
  f32 target_last_index_for_paint =
      has_target_last_index_for_paint
          ? ui_sliver_fixed_extent_list_get_max_child_index(
                item_extent, target_end_scroll_offset_for_paint)
          : 0;

  *geometry = (UISliverGeometry){
      .scroll_extent = scroll_extent,
      .paint_extent = paint_extent,
      .cache_extent = cache_extent,
      .layout_extent = paint_extent,
      .hit_test_extent = paint_extent,
      .max_paint_extent = scroll_extent,
      .has_visual_overflow = constraints->scroll_offset > 0 ||
                             (has_target_last_index_for_paint &&
                              child_index >= target_last_index_for_paint),
  };

  UIParentDataSliver *data = ui_widget_get_parent_data(
      widget, UI_PARENT_DATA_SLIVER, UIParentDataSliver);
  state->next_scroll_offset = data->next_scroll_offset;
  state->last_constraints = *constraints;
}

static void ui_sliver_fixed_extent_list_paint(UIWidget *widget,
                                              UIPaintingContext *context,
                                              Vec2 offset) {
  Rect2 widget_rect = rect2(vec2_zero(), widget->size);
  for (UIWidget *child = widget->first; child; child = child->next) {
    Rect2 intersection = rect2_from_intersection(
        widget_rect,
        rect2(child->offset, vec2_add(child->offset, child->size)));
    if (rect2_get_area(intersection) > 0) {
      ui_widget_paint_child_default(child, context, offset);
    }
  }
}

UIWidgetClass ui_sliver_fixed_extent_list_class = {
    .name = "SliverFixedExtentList",
    .flags = UI_WIDGET_MANY_CHILDREN,
    .state_size = sizeof(UISliverFixedExtentListState),
    .mount = ui_sliver_fixed_extent_list_mount,
    .layout = ui_widget_layout_for_sliver,
    .layout_sliver = ui_sliver_fixed_extent_list_layout_sliver,
    .paint = ui_sliver_fixed_extent_list_paint,
};

void ui_sliver_fixed_extent_list_begin(
    const UISliverFixedExtentListProps *props) {
  UIWidget *widget =
      ui_widget_begin(&ui_sliver_fixed_extent_list_class, props->key);
  UISliverFixedExtentListState *state =
      ui_widget_get_state(widget, UISliverFixedExtentListState);

  state->item_extent = props->item_extent;
  state->item_count = props->item_count;
  state->has_builder = props->builder != 0;

  f32 scroll_offset = scroll_offset =
      state->next_scroll_offset + state->last_constraints.cache_origin;
  f32 remaining_extent = remaining_extent =
      state->last_constraints.remaining_cache_extent;

  i32 first_index, target_last_index;
  ui_sliver_fixed_extent_list_calc_item_count(props->item_extent, scroll_offset,
                                              remaining_extent, &first_index,
                                              &target_last_index);
  UIListBuilder builder = (UIListBuilder){
      .first_index = first_index,
      .last_index = f32_min(props->item_count - 1, target_last_index),
  };

  if (props->builder) {
    *props->builder = builder;
  }
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIListView
///
void ui_list_view_begin(const UIListViewProps *props) {
  ui_scrollable_begin(&(UIScrollableProps){
      .axis_direction = UI_AXIS_DIRECTION_DOWN,
      .cross_axis_direction = UI_AXIS_DIRECTION_RIGHT,
      .scroll = props->scroll,
  });
  ui_sliver_fixed_extent_list_begin(&(UISliverFixedExtentListProps){
      .item_extent = props->item_extent,
      .item_count = props->item_count,
      .builder = props->builder,
  });
}

void ui_list_view_end(void) {
  ui_sliver_fixed_extent_list_end();
  ui_scrollable_end();
}
