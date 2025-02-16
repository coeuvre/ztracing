#include "src/ui.h"

#include <stdarg.h>
#include <stdbool.h>
#include <string.h>

#include "src/assert.h"
#include "src/draw.h"
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
  UIKey key = ui_widget_get_key(widget);
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
      if (ui_key_is_equal(ui_widget_get_key(widget), key)) {
        result = widget;
        break;
      }
    }
  }
  return result;
}

typedef struct UIFrame {
  Arena arena;
  UIWidget *root;
  bool open;
} UIFrame;

typedef struct UIWidgetStackEntry {
  UIWidget *widget;
  UIWidget *last_widget;
  UIWidget *last_child;
  const u8 *build_arena_top;
} UIWidgetStackEntry;

typedef struct UIWidgetStack {
  Arena arena;
  UIWidgetStackEntry *current;
} UIWidgetStack;

static bool ui_widget_stack_is_empty(UIWidgetStack *stack) {
  if (stack->current) {
    return false;
  }

  if (!stack->arena.current_block) {
    return true;
  }

  if (stack->arena.current_block->prev) {
    return false;
  }

  return stack->arena.current_block->pos == stack->arena.current_block->begin;
}

static void ui_widget_stack_push(UIWidgetStack *stack, Arena *build_arena,
                                 UIWidget *widget, UIWidget *last_widget) {
  UIWidgetStackEntry *entry =
      arena_push_array(&stack->arena, UIWidgetStackEntry, 1);
  entry->widget = widget;
  entry->last_widget = last_widget;
  if (last_widget) {
    entry->last_child = last_widget->first;
  }
  if (build_arena->current_block) {
    entry->build_arena_top = build_arena->current_block->pos;
  } else {
    entry->build_arena_top = 0;
  }
  stack->current = entry;
}

static UIWidget *ui_widget_stack_pop(UIWidgetStack *stack, Arena *build_arena) {
  ASSERT(stack->current);
  UIWidgetStackEntry *entry = stack->current;
  arena_pop(&stack->arena, sizeof(UIWidgetStackEntry));
  ASSERT(entry == arena_seek(&stack->arena, 0));
  stack->current = arena_seek(&stack->arena, sizeof(UIWidgetStackEntry));
  if (entry->build_arena_top) {
    ASSERTF(build_arena->current_block &&
                build_arena->current_block->pos == entry->build_arena_top,
            "build arena was not cleaned up properly by the widget");
  } else {
    ASSERTF(
        !build_arena->current_block || (!build_arena->current_block->prev &&
                                        build_arena->current_block->pos ==
                                            build_arena->current_block->begin),
        "build arena was not cleaned up properly by the widget");
  }
  return entry->widget;
}

typedef struct UIHitTestState {
  Arena arena;
  UIHitTestResult result;
} UIHitTestState;

static void ui_hit_test_state_clear(UIHitTestState *self) {
  self->result = (UIHitTestResult){0};
  arena_clear(&self->arena);
}

// Find widget inside the tree rooted at `root` that `widget->old_widget ==
// entry->widget` using BFS.
static UIWidget *ui_hit_test_find_widget_bfs(UIHitTestEntry *entry,
                                             UIWidget *root) {
  Scratch scratch = scratch_begin(0, 0);

  UIWidget *result = 0;
  UIWidgetList queue = {0};
  ui_widget_list_add_last(&queue, scratch.arena, root);

  for (UIWidgetListNode *node = ui_widget_list_remove_first(&queue); node;
       node = ui_widget_list_remove_first(&queue)) {
    if (entry->widget == node->widget->old_widget) {
      result = node->widget;
      break;
    }

    for (UIWidget *child = node->widget->first; child; child = child->next) {
      ui_widget_list_add_last(&queue, scratch.arena, child);
    }
  }

  scratch_end(scratch);

  return result;
}

/// Update widget references in this hit test path from old widget tree to the
/// new widget tree.
static void ui_hit_test_state_sync(UIHitTestState *self, UIWidget *new_root) {
  UIHitTestEntry *entry = self->result.last;
  if (new_root) {
    UIWidget *new_widget = new_root;
    for (; entry; entry = entry->prev) {
      UIWidget *match = ui_hit_test_find_widget_bfs(entry, new_widget);
      if (!match) {
        break;
      }
      entry->widget = match;
      new_widget = match;
    }
  }

  for (; entry; entry = entry->prev) {
    entry->widget = 0;
  }
}

typedef struct UIInputState {
  Vec2 last_pointer_pos;
  u32 current_down_button;

  // hit test state for button down.
  UIHitTestState button_down_hit_test;
  // hit test result for mouse move
  UIHitTestState button_move_hit_tests[2];
  usize button_move_hit_test_index;
} UIInputState;

typedef struct UIState {
  UIFrame frames[2];
  u64 frame_index;
  UIFrame *current_frame;
  UIFrame *last_frame;

  UIWidgetStack widget_stack;
  Arena build_arena;

  Vec2 viewport_min;
  Vec2 viewport_max;

  UIInputState input;
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

static i32 ui_widget_callback_default(UIWidget *widget, UIMessage *message);

typedef struct UIRootProps {
  UIKey key;
} UIRootProps;

static UIWidgetClass ui_root_class = {
    .name = "Root",
    .props_size = sizeof(UIKey),
    .callback = &ui_widget_callback_default,
};

static inline void ui_root_begin(const UIRootProps *props) {
  ui_widget_begin(&ui_root_class, props);
}
static inline void ui_root_end(void) { ui_widget_end(&ui_root_class); }

void ui_begin_frame(void) {
  UIState *state = ui_state_get();
  ASSERT(ui_widget_stack_is_empty(&state->widget_stack));

  state->frame_index += 1;
  state->current_frame =
      state->frames + (state->frame_index % ARRAY_COUNT(state->frames));
  state->last_frame =
      state->frames + ((state->frame_index - 1) % ARRAY_COUNT(state->frames));

  UIFrame *frame = state->current_frame;
  arena_clear(&frame->arena);
  frame->root = 0;
  frame->open = true;

  ui_root_begin(&(UIRootProps){0});
}

static inline void ui_widget_mount(UIWidget *widget) {
  ASSERT(widget->status == UI_WIDGET_STATUS_UNMOUNTED);
  UIMessage message = {
      .mount =
          {
              .type = UI_MESSAGE_MOUNT,
          },
  };
  widget->klass->callback(widget, &message);
  widget->status = UI_WIDGET_STATUS_MOUNTED;
}

static inline void ui_widget_update(UIWidget *widget) {
  UIMessage message = {
      .update =
          {
              .type = UI_MESSAGE_UPDATE,
          },
  };
  widget->klass->callback(widget, &message);
}

static inline void ui_widget_unmount(UIWidget *widget) {
  ASSERT(widget->status == UI_WIDGET_STATUS_MOUNTED);
  UIMessage message = {
      .umount =
          {
              .type = UI_MESSAGE_UNMOUNT,
          },
  };
  widget->klass->callback(widget, &message);
  widget->status = UI_WIDGET_STATUS_UNMOUNTED;
}

static inline Vec2 ui_widget_layout_box(UIWidget *widget,
                                        UIBoxConstraints constraints) {
  UIMessage message = {
      .layout_box =
          {
              .type = UI_MESSAGE_LAYOUT_BOX,
              .constraints = constraints,
          },
  };
  widget->klass->callback(widget, &message);
  return widget->size;
}

static inline void ui_widget_layout_sliver(UIWidget *widget,
                                           UISliverConstraints constraints) {
  UIMessage message = {
      .layout_sliver =
          {
              .type = UI_MESSAGE_LAYOUT_SLIVER,
              .constraints = constraints,
          },
  };
  widget->klass->callback(widget, &message);
}

static inline void ui_widget_paint(UIWidget *widget, UIPaintingContext *context,
                                   Vec2 offset) {
  UIMessage message = {
      .paint =
          {
              .type = UI_MESSAGE_PAINT,
              .context = context,
              .offset = offset,
          },
  };
  widget->klass->callback(widget, &message);
}

static inline bool ui_widget_hit_test(UIWidget *widget, UIHitTestResult *result,
                                      Arena *arena, Vec2 local_position) {
  UIMessage message = {
      .hit_test =
          {
              .type = UI_MESSAGE_HIT_TEST,
              .result = result,
              .arena = arena,
              .local_position = local_position,
          },
  };
  return widget->klass->callback(widget, &message);
}

static inline void ui_widget_handle_event(UIWidget *widget,
                                          UIPointerEvent *event) {
  UIMessage message = {
      .handle_event =
          {
              .type = UI_MESSAGE_HANDLE_EVENT,
              .event = event,
          },
  };
  widget->klass->callback(widget, &message);
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

static bool ui_hit_test_state_hit_test(UIHitTestState *self, UIWidget *root,
                                       Vec2 pos) {
  ASSERT(!self->result.first);
  if (ui_widget_hit_test(root, &self->result, &self->arena, pos)) {
    return true;
  } else {
    ui_hit_test_state_clear(self);
    return false;
  }
}

void ui_on_mouse_button_down(Vec2 pos, u32 button) {
  UIState *state = ui_state_get();

  state->input.current_down_button |= button;

  if (state->input.button_down_hit_test.result.first) {
    return;
  }

  UIWidget *root = ui_state_get_root_for_hit_test(state);
  if (!root) {
    return;
  }

  if (ui_hit_test_state_hit_test(&state->input.button_down_hit_test, root,
                                 pos)) {
    for (UIHitTestEntry *entry = state->input.button_down_hit_test.result.first;
         entry; entry = entry->next) {
      ui_widget_handle_event(entry->widget,
                             &(UIPointerEvent){
                                 .type = UI_POINTER_EVENT_DOWN,
                                 .button = button,
                                 .position = pos,
                                 .local_position = entry->local_position,
                             });
    }
  }
}

static void ui_hit_test_update_local_position(UIHitTestResult *result,
                                              Vec2 pos) {
  Vec2 local_position = pos;
  for (UIHitTestEntry *entry = result->last; entry && entry->widget;
       entry = entry->prev) {
    local_position = vec2_sub(local_position, entry->widget->offset);
    entry->local_position = local_position;
  }
}

void ui_on_mouse_button_up(Vec2 pos, u32 button) {
  UIState *state = ui_state_get();

  state->input.current_down_button &= (~button);
  if (state->input.current_down_button ||
      !state->input.button_down_hit_test.result.first) {
    return;
  }

  ui_hit_test_update_local_position(&state->input.button_down_hit_test.result,
                                    pos);
  for (UIHitTestEntry *entry = state->input.button_down_hit_test.result.first;
       entry; entry = entry->next) {
    if (entry->widget) {
      ui_widget_handle_event(entry->widget,
                             &(UIPointerEvent){
                                 .type = UI_POINTER_EVENT_UP,
                                 .button = button,
                                 .position = pos,
                                 .local_position = entry->local_position,
                             });
    }
  }

  ui_hit_test_state_clear(&state->input.button_down_hit_test);
}

void ui_on_mouse_move(Vec2 pos) {
  UIState *state = ui_state_get();
  UIInputState *input = &state->input;

  if (vec2_is_equal(input->last_pointer_pos, pos)) {
    return;
  }
  input->last_pointer_pos = pos;

  if (input->button_down_hit_test.result.first) {
    ui_hit_test_update_local_position(&state->input.button_down_hit_test.result,
                                      pos);
    for (UIHitTestEntry *entry = input->button_down_hit_test.result.first;
         entry; entry = entry->next) {
      if (entry->widget) {
        ui_widget_handle_event(entry->widget,
                               &(UIPointerEvent){
                                   .type = UI_POINTER_EVENT_MOVE,
                                   .position = pos,
                                   .local_position = entry->local_position,
                               });
      }
    }
  }

  UIWidget *root = ui_state_get_root_for_hit_test(state);
  if (!root) {
    return;
  }
  UIHitTestState *last_hit_test =
      input->button_move_hit_tests + input->button_move_hit_test_index;
  input->button_move_hit_test_index = (input->button_move_hit_test_index + 1) %
                                      ARRAY_COUNT(input->button_move_hit_tests);
  UIHitTestState *hit_test =
      input->button_move_hit_tests + input->button_move_hit_test_index;
  ui_hit_test_state_hit_test(hit_test, root, pos);

  for (UIHitTestEntry *entry = last_hit_test->result.first; entry;
       entry = entry->next) {
    if (!ui_hit_test_state_has_widget(hit_test, entry->widget)) {
      ui_widget_handle_event(entry->widget, &(UIPointerEvent){
                                                .type = UI_POINTER_EVENT_EXIT,
                                                .position = pos,
                                            });
    }
  }

  for (UIHitTestEntry *entry = hit_test->result.first; entry;
       entry = entry->next) {
    if (!ui_hit_test_state_has_widget(last_hit_test, entry->widget)) {
      ui_widget_handle_event(entry->widget,
                             &(UIPointerEvent){
                                 .type = UI_POINTER_EVENT_ENTER,
                                 .position = pos,
                                 .local_position = entry->local_position,
                             });
    }
  }

  // Only send HOVER events if there isn't button down.
  if (!input->button_down_hit_test.result.first) {
    for (UIHitTestEntry *entry = hit_test->result.first; entry;
         entry = entry->next) {
      ui_widget_handle_event(entry->widget,
                             &(UIPointerEvent){
                                 .type = UI_POINTER_EVENT_HOVER,
                                 .position = pos,
                                 .local_position = entry->local_position,
                             });
    }
  }

  ui_hit_test_state_clear(last_hit_test);
}

void ui_on_focus_lost(Vec2 pos) {
  UIState *state = ui_state_get();

  if (!state->input.button_down_hit_test.result.first) {
    return;
  }

  for (UIHitTestEntry *entry = state->input.button_down_hit_test.result.first;
       entry; entry = entry->next) {
    if (entry->widget) {
      ui_widget_handle_event(entry->widget, &(UIPointerEvent){
                                                .type = UI_POINTER_EVENT_CANCEL,
                                                .position = pos,
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
static void ui_widget_layout_box_default(UIWidget *widget,
                                         UIBoxConstraints constraints) {
  ASSERTF(ui_widget_has_at_most_one_child(widget),
          "Default layout method doesn't work for UI_WIDGET_MANY_CHILDREN.");
  if (widget->first) {
    widget->size = ui_widget_layout_box(widget->first, constraints);
  } else {
    widget->size = ui_box_constraints_constrain(constraints, vec2_zero());
  }
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
                                                Arena *arena,
                                                Vec2 local_position) {
  for (UIWidget *child = widget->last; child; child = child->prev) {
    if (ui_widget_hit_test(child, result, arena,
                           vec2_sub(local_position, child->offset))) {
      return true;
    }
  }
  return false;
}

void ui_hit_test_result_add(UIHitTestResult *self, Arena *arena,
                            UIWidget *widget, Vec2 local_position) {
  UIHitTestEntry *entry = arena_push_struct(arena, UIHitTestEntry);
  entry->widget = widget;
  entry->local_position = local_position;
  DLL_APPEND(self->first, self->last, entry, prev, next);
}

static bool ui_widget_hit_test_defer_to_children(UIWidget *widget,
                                                 UIHitTestResult *result,
                                                 Arena *arena,
                                                 Vec2 local_position) {
  if (!vec2_contains(local_position, vec2_zero(), widget->size)) {
    return false;
  }

  if (!ui_widget_hit_test_children_default(widget, result, arena,
                                           local_position)) {
    return false;
  }

  ui_hit_test_result_add(result, arena, widget, local_position);
  return true;
}

static bool ui_widget_hit_test_opaque(UIWidget *widget, UIHitTestResult *result,
                                      Arena *arena, Vec2 local_position) {
  if (!vec2_contains(local_position, vec2_zero(), widget->size)) {
    return false;
  }

  ui_widget_hit_test_children_default(widget, result, arena, local_position);

  ui_hit_test_result_add(result, arena, widget, local_position);
  return true;
}

static void ui_widget_mount_default(UIWidget *widget) {
  if (widget->klass->state_size > 0) {
    UIState *state = ui_state_get();
    UIFrame *frame = ui_state_get_current_frame(state);
    widget->state = arena_push(&frame->arena, widget->klass->state_size, 0);
  }
}

static void ui_widget_update_default(UIWidget *widget) {
  if (widget->klass->state_size > 0) {
    UIWidget *old_widget = widget->old_widget;
    void *old_state =
        ui_widget_get_state_(old_widget, widget->klass->state_size);

    UIState *state = ui_state_get();
    UIFrame *frame = ui_state_get_current_frame(state);
    widget->state = arena_push(&frame->arena, widget->klass->state_size,
                               ARENA_PUSH_NO_ZERO);
    memory_copy(widget->state, old_state, widget->klass->state_size);
  }
}

static i32 ui_widget_callback_default(UIWidget *widget, UIMessage *message) {
  i32 result = 0;
  switch (message->type) {
    case UI_MESSAGE_MOUNT: {
      ui_widget_mount_default(widget);
    } break;

    case UI_MESSAGE_UPDATE: {
      ui_widget_update_default(widget);
    } break;

    case UI_MESSAGE_LAYOUT_BOX: {
      ui_widget_layout_box_default(widget, message->layout_box.constraints);
    } break;

    case UI_MESSAGE_LAYOUT_SLIVER: {
      DEBUG_ASSERTF(false, "UI_MESSAGE_LAYOUT_SLIVER is not implemented for %s",
                    widget->klass->name);
    } break;

    case UI_MESSAGE_PAINT: {
      ui_widget_paint_default(widget, message->paint.context,
                              message->paint.offset);
    } break;

    case UI_MESSAGE_HIT_TEST: {
      result = ui_widget_hit_test_defer_to_children(
          widget, message->hit_test.result, message->hit_test.arena,
          message->hit_test.local_position);
    } break;
  }
  return result;
}

static UIWidget *ui_widget_get_child_by_key(UIWidget *parent, UIKey key) {
  UIWidget *result = 0;
  if (!ui_key_is_zero(key)) {
    // TODO: Use hash map to speed up the search.
    for (UIWidget *child = parent->first; child; child = child->next) {
      if (ui_key_is_equal(ui_widget_get_key(child), key)) {
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

static bool ui_widget_is_equal(UIWidget *a, UIWidget *b) {
  if (!a || !b) {
    return false;
  }

  if (a->klass != b->klass) {
    return false;
  }

  return ui_key_is_equal(ui_widget_get_key(a), ui_widget_get_key(b));
}

static void unmount_widgets_if_not(UIWidget *widget) {
  if (!widget) {
    return;
  }

  for (UIWidget *child = widget->first; child; child = child->next) {
    unmount_widgets_if_not(child);
  }

  if (widget->status != UI_WIDGET_STATUS_UNMOUNTED) {
    ui_widget_unmount(widget);
  }
}

void ui_end_frame(void) {
  ui_root_end();

  UIState *state = ui_state_get();
  UIFrame *frame = state->current_frame;
  ASSERTF(ui_widget_stack_is_empty(&state->widget_stack),
          "Mismatched begin/end calls, last begin: %s",
          state->widget_stack.current->widget->klass->name);

  // Update widget references in hit tests so we can send events to widgets
  // later.
  ui_hit_test_state_sync(&state->input.button_down_hit_test, frame->root);
  ui_hit_test_state_sync(&state->input.button_move_hit_tests[0], frame->root);
  ui_hit_test_state_sync(&state->input.button_move_hit_tests[1], frame->root);

  // Layout and paint
  if (frame->root) {
    unmount_widgets_if_not(state->last_frame->root);

    Vec2 viewport_size = vec2_sub(state->viewport_max, state->viewport_min);
    ui_widget_layout_box(frame->root, ui_box_constraints_tight(
                                          viewport_size.x, viewport_size.y));

    UIPaintingContext context = {0};
    ui_widget_paint(frame->root, &context, state->viewport_min);
  }

  frame->open = false;
}

Str8 ui_push_str8(Str8 str) {
  UIState *state = ui_state_get();
  UIFrame *frame = ui_state_get_current_frame(state);
  return arena_push_str8(&frame->arena, str);
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

static UIKey ui_key_from_str8(UIKey seed, Str8 str) {
  UIKey result = seed;
  if (str.len) {
    // djb2 hash function
    u64 hash = seed.hash ? seed.hash : 5381;
    for (usize i = 0; i < str.len; i += 1) {
      // hash * 33 + c
      hash = ((hash << 5) + hash) + str.ptr[i];
    }
    result.hash = hash;
  }
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
static UIWidget *ui_widget_alloc(Arena *arena, UIWidgetClass *klass,
                                 const void *props) {
  UIWidget *widget = arena_push(arena, sizeof(UIWidget) + klass->props_size,
                                ARENA_PUSH_NO_ZERO);
  memory_zero(widget, sizeof(UIWidget));
  widget->klass = klass;
  memory_copy(widget + 1, props, klass->props_size);
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
    DEBUG_ASSERTF(!parent->first, "%s expects only one child",
                  parent->klass->name);
    parent->first = parent->last = child;
    parent->child_count = 1;
  }
}

static inline bool can_reuse_widget(UIWidget *widget, UIWidget *last_widget) {
  return last_widget && last_widget->status == UI_WIDGET_STATUS_MOUNTED &&
         ui_widget_is_equal(widget, last_widget);
}

UIWidget *ui_widget_begin(UIWidgetClass *klass, const void *props) {
  ASSERTF(klass->props_size >= sizeof(UIKey),
          "The first field of props must be a UIKey");
  ASSERTF(klass->callback, "%s doesn't have callback.", klass->name);
  UIState *state = ui_state_get();
  UIFrame *frame = state->current_frame;

  UIWidget *widget = ui_widget_alloc(&frame->arena, klass, props);
  UIWidget *last_widget = 0;

  UIWidgetStackEntry *parent = state->widget_stack.current;
  if (parent) {
    if (parent->last_widget) {
      UIKey key = ui_widget_get_key(widget);

      // TODO: Check for global key
      last_widget = ui_widget_get_child_by_key(parent->last_widget, key);
      if (!can_reuse_widget(widget, last_widget)) {
        last_widget = 0;
      }

      if (!last_widget) {
        last_widget = parent->last_child;
        if (!can_reuse_widget(widget, last_widget)) {
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
    ASSERTF(!frame->root, "root widget already exists.");
    frame->root = widget;
    last_widget = state->last_frame->root;
  }

  if (last_widget) {
    ASSERT(last_widget->status == UI_WIDGET_STATUS_MOUNTED);
    ASSERT(last_widget->klass == widget->klass);

    widget->status = UI_WIDGET_STATUS_MOUNTED;
    widget->size = last_widget->size;
    widget->offset = last_widget->offset;
    widget->old_widget = last_widget;

    ui_widget_update(widget);

    // The state is transfered into current, effectively make last_widget
    // unmounted.
    last_widget->status = UI_WIDGET_STATUS_UNMOUNTED;
  } else {
    ui_widget_mount(widget);
  }

  ui_widget_stack_push(&state->widget_stack, &state->build_arena, widget,
                       last_widget);
  return widget;
}

void ui_widget_end(UIWidgetClass *klass) {
  UIState *state = ui_state_get();
  UIWidget *widget =
      ui_widget_stack_pop(&state->widget_stack, &state->build_arena);
  ASSERTF(widget->klass == klass,
          "mismatched begin/end calls. Begin with %s, end with %s",
          widget->klass->name, klass->name);
}

UIWidget *ui_widget_get_current(void) {
  UIState *state = ui_state_get();
  UIWidget *result = 0;
  if (state->widget_stack.current) {
    result = state->widget_stack.current->widget;
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

Arena *ui_get_build_arena(void) {
  UIState *state = ui_state_get();
  return &state->build_arena;
}

void ui_widget_set_parent_data(UIWidget *widget, u64 type, usize data_size,
                               void *data) {
  UIState *state = ui_state_get();
  UIFrame *frame = ui_state_get_current_frame(state);
  ui_parent_data_upsert(&frame->arena, &widget->parent_data, type, data_size,
                        data);
}

////////////////////////////////////////////////////////////////////////////////
///
/// UILimitedBox
///
static UIBoxConstraints ui_limited_box_limit_constraints(
    UILimitedBoxProps *limited_box, UIBoxConstraints constraints) {
  return (UIBoxConstraints){
      .min_width = constraints.min_width,
      .max_width = ui_box_constraints_has_bounded_width(constraints)
                       ? constraints.max_width
                       : ui_box_constraints_constrain_width(
                             constraints, limited_box->max_width),
      .min_height = constraints.min_height,
      .max_height = ui_box_constraints_has_bounded_height(constraints)
                        ? constraints.max_height
                        : ui_box_constraints_constrain_height(
                              constraints, limited_box->max_height),
  };
}

static void ui_limited_box_layout(UIWidget *widget,
                                  UILimitedBoxProps *limited_box,
                                  UIBoxConstraints constraints) {
  ASSERT(ui_widget_has_at_most_one_child(widget));

  UIBoxConstraints limited_constraints =
      ui_limited_box_limit_constraints(limited_box, constraints);

  if (widget->first) {
    Vec2 child_size = ui_widget_layout_box(widget->first, limited_constraints);
    widget->size = ui_box_constraints_constrain(constraints, child_size);
  } else {
    widget->size =
        ui_box_constraints_constrain(limited_constraints, vec2_zero());
  }
}

static i32 ui_limited_box_callback(UIWidget *widget, UIMessage *message) {
  i32 result = 0;
  switch (message->type) {
    case UI_MESSAGE_LAYOUT_BOX: {
      ui_limited_box_layout(widget,
                            ui_widget_get_props(widget, UILimitedBoxProps),
                            message->layout_box.constraints);
    } break;
    default: {
      result = ui_widget_callback_default(widget, message);
    } break;
  }
  return result;
}

UIWidgetClass ui_limited_box_class = {
    .name = "LimitedBox",
    .props_size = sizeof(UILimitedBoxProps),
    .callback = &ui_limited_box_callback,
};

////////////////////////////////////////////////////////////////////////////////
///
/// UIColoredBox
///
static void ui_colored_box_paint(UIWidget *widget,
                                 UIColoredBoxProps *colored_box,
                                 UIPaintingContext *context, Vec2 offset) {
  Vec2 size = widget->size;
  if (size.x > 0 && size.y > 0) {
    fill_rect(offset, vec2_add(offset, size),
              (ColorU32){
                  (u8)(colored_box->color.a * 255.0f),
                  (u8)(colored_box->color.r * 255.0f),
                  (u8)(colored_box->color.g * 255.0f),
                  (u8)(colored_box->color.b * 255.0f),
              });
  }

  for (UIWidget *child = widget->first; child; child = child->next) {
    ui_widget_paint_child_default(child, context, offset);
  }
}

static i32 ui_colored_box_callback(UIWidget *widget, UIMessage *message) {
  i32 result = 0;
  switch (message->type) {
    case UI_MESSAGE_PAINT: {
      ui_colored_box_paint(widget,
                           ui_widget_get_props(widget, UIColoredBoxProps),
                           message->paint.context, message->paint.offset);
    } break;

    case UI_MESSAGE_HIT_TEST: {
      result = ui_widget_hit_test_opaque(widget, message->hit_test.result,
                                         message->hit_test.arena,
                                         message->hit_test.local_position);
    } break;

    default: {
      result = ui_widget_callback_default(widget, message);
    } break;
  }
  return result;
}

UIWidgetClass ui_colored_box_class = {
    .name = "ColoredBox",
    .props_size = sizeof(UIColoredBoxProps),
    .callback = &ui_colored_box_callback,
};

////////////////////////////////////////////////////////////////////////////////
///
/// UIConstrainedBox
///
static void ui_constrained_box_layout(UIWidget *widget,
                                      UIConstrainedBoxProps *constrained_box,
                                      UIBoxConstraints constraints) {
  ASSERT(ui_widget_has_at_most_one_child(widget));
  UIBoxConstraints enforced_constraints =
      ui_box_constraints_enforce(constrained_box->constraints, constraints);
  if (widget->first) {
    widget->size = ui_widget_layout_box(widget->first, enforced_constraints);
  } else {
    widget->size =
        ui_box_constraints_constrain(enforced_constraints, vec2_zero());
  }
}

static i32 ui_constrained_box_callback(UIWidget *widget, UIMessage *message) {
  i32 result = 0;
  switch (message->type) {
    case UI_MESSAGE_LAYOUT_BOX: {
      ui_constrained_box_layout(
          widget, ui_widget_get_props(widget, UIConstrainedBoxProps),
          message->layout_box.constraints);
    } break;
    default: {
      result = ui_widget_callback_default(widget, message);
    } break;
  }
  return result;
}

UIWidgetClass ui_constrained_box_class = {
    .name = "ConstrainedBox",
    .props_size = sizeof(UIConstrainedBoxProps),
    .callback = &ui_constrained_box_callback,
};

////////////////////////////////////////////////////////////////////////////////
///
/// UIAlign
///
static void ui_widget_align_children(UIWidget *widget, UIAlignment alignment) {
  // TODO: UITextDirection
  for (UIWidget *child = widget->first; child; child = child->next) {
    child->offset = ui_alignment_align_offset(
        alignment, vec2_sub(widget->size, child->size));
  }
}

static void ui_align_layout(UIWidget *widget, UIAlignProps *align,
                            UIBoxConstraints constraints) {
  ASSERT(ui_widget_has_at_most_one_child(widget));

  f32o width = align->width;
  f32o height = align->height;

  if (width.present) {
    ASSERTF(width.value >= 0, "factor.widget must be positive, got %f",
            width.value);
  }
  if (height.present) {
    ASSERTF(align->height.value >= 0, "height_factor must be positive, got %f",
            align->height.value);
  }
  bool should_shrink_wrap_width =
      width.present || f32_is_infinity(constraints.max_width);
  bool should_shrink_wrap_height =
      height.present || f32_is_infinity(constraints.max_height);
  UIBoxConstraints child_constraints = ui_box_constraints_loosen(constraints);

  if (widget->first) {
    Vec2 child_size = ui_widget_layout_box(widget->first, child_constraints);

    Vec2 wrap_size =
        vec2(should_shrink_wrap_width
                 ? (child_size.x * (width.present ? width.value : 1.0f))
                 : F32_INFINITY,
             should_shrink_wrap_height
                 ? (child_size.y * (height.present ? height.value : 1.0f))
                 : F32_INFINITY);

    widget->size = ui_box_constraints_constrain(constraints, wrap_size);

    ui_widget_align_children(widget, align->alignment);
  } else {
    Vec2 size = vec2(should_shrink_wrap_width ? 0 : F32_INFINITY,
                     should_shrink_wrap_height ? 0 : F32_INFINITY);
    widget->size = ui_box_constraints_constrain(constraints, size);
  }
}

static i32 ui_align_callback(UIWidget *widget, UIMessage *message) {
  i32 result = 0;
  switch (message->type) {
    case UI_MESSAGE_LAYOUT_BOX: {
      ui_align_layout(widget, ui_widget_get_props(widget, UIAlignProps),
                      message->layout_box.constraints);
    } break;
    default: {
      result = ui_widget_callback_default(widget, message);
    } break;
  }
  return result;
}

UIWidgetClass ui_align_class = {
    .name = "Align",
    .props_size = sizeof(UIAlignProps),
    .callback = &ui_align_callback,
};

////////////////////////////////////////////////////////////////////////////////
///
/// UIUnconstrainedBox
///
/// A widget that imposes no constraints on its child, allowing it to render
/// at its "natural" size.
static void ui_unconstrained_box_layout(
    UIWidget *widget, UIUnconstrainedBoxProps *unconstrained_box,
    UIBoxConstraints constraints) {
  (void)constraints;

  ASSERT(ui_widget_has_at_most_one_child(widget));

  UIBoxConstraints child_constraints =
      ui_box_constraints(0, F32_INFINITY, 0, F32_INFINITY);
  if (widget->first) {
    Vec2 child_size = ui_widget_layout_box(widget->first, child_constraints);
    widget->size = ui_box_constraints_constrain(constraints, child_size);
  } else {
    widget->size = ui_box_constraints_constrain(constraints, vec2_zero());
  }

  ui_widget_align_children(widget, unconstrained_box->alignment);
}

static i32 ui_unconstrained_box_callback(UIWidget *widget, UIMessage *message) {
  i32 result = 0;
  switch (message->type) {
    case UI_MESSAGE_LAYOUT_BOX: {
      ui_unconstrained_box_layout(
          widget, ui_widget_get_props(widget, UIUnconstrainedBoxProps),
          message->layout_box.constraints);
    } break;
    default: {
      result = ui_widget_callback_default(widget, message);
    } break;
  }
  return result;
}

UIWidgetClass ui_unconstrained_box_class = {
    .name = "UnconstrainedBox",
    .props_size = sizeof(UIUnconstrainedBoxProps),
    .callback = &ui_unconstrained_box_callback,
};

////////////////////////////////////////////////////////////////////////////////
///
/// UICenter
///
static i32 ui_center_callback(UIWidget *widget, UIMessage *message) {
  i32 result = 0;
  switch (message->type) {
    case UI_MESSAGE_LAYOUT_BOX: {
      UICenterProps *center = ui_widget_get_props(widget, UICenterProps);
      UIAlignProps align = {
          .key = center->key,
          .width = center->width,
          .height = center->height,
      };
      ui_align_layout(widget, &align, message->layout_box.constraints);
    } break;
    default: {
      result = ui_widget_callback_default(widget, message);
    } break;
  }
  return result;
}

UIWidgetClass ui_center_class = {
    .name = "Center",
    .props_size = sizeof(UICenterProps),
    .callback = &ui_center_callback,
};

////////////////////////////////////////////////////////////////////////////////
///
/// UIPadding
///
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

static void ui_padding_layout(UIWidget *widget, UIPaddingProps *padding,
                              UIBoxConstraints constraints) {
  ASSERT(ui_widget_has_at_most_one_child(widget));

  // TODO: UITextDirection
  UIResolvedEdgeInsets resolved_padding = {
      .left = padding->padding.start,
      .right = padding->padding.end,
      .top = padding->padding.top,
      .bottom = padding->padding.bottom,
  };
  f32 horizontal = ui_resolved_edge_insets_get_horizontal(resolved_padding);
  f32 vertical = ui_resolved_edge_insets_get_vertical(resolved_padding);
  if (widget->first) {
    UIBoxConstraints inner_constraints =
        ui_box_constraints_deflate(constraints, resolved_padding);

    UIWidget *child = widget->first;
    Vec2 child_size = ui_widget_layout_box(widget->first, inner_constraints);
    child->offset = vec2(resolved_padding.left, resolved_padding.top);

    widget->size = ui_box_constraints_constrain(
        constraints, vec2(horizontal + child_size.x, vertical + child_size.y));
  } else {
    widget->size =
        ui_box_constraints_constrain(constraints, vec2(horizontal, vertical));
  }
}

static i32 ui_padding_callback(UIWidget *widget, UIMessage *message) {
  i32 result = 0;
  switch (message->type) {
    case UI_MESSAGE_LAYOUT_BOX: {
      ui_padding_layout(widget, ui_widget_get_props(widget, UIPaddingProps),
                        message->layout_box.constraints);
    } break;
    default: {
      result = ui_widget_callback_default(widget, message);
    } break;
  }
  return result;
}

UIWidgetClass ui_padding_class = {
    .name = "Padding",
    .props_size = sizeof(UIPaddingProps),
    .callback = &ui_padding_callback,
};

////////////////////////////////////////////////////////////////////////////////
///
/// UIContainer
///
UIWidgetClass ui_container_class = {
    .name = "Container",
    .props_size = sizeof(UIContainerProps),
    .callback = &ui_widget_callback_default,
};

void ui_container_begin(const UIContainerProps *props_) {
  UIWidget *widget = ui_widget_begin(&ui_container_class, props_);
  UIContainerProps *props = ui_widget_get_props(widget, UIContainerProps);

  if (props->width.present || props->height.present) {
    if (props->constraints.present) {
      props->constraints = ui_box_constraints_some(ui_box_constraints_tighten(
          props->constraints.value, props->width, props->height));
    } else {
      props->constraints = ui_box_constraints_some(
          ui_box_constraints_tight_for(props->width, props->height));
    }
  }

  if (props->margin.present) {
    ui_padding_begin(&(UIPaddingProps){
        .padding = props->margin.value,
    });
  }

  if (props->constraints.present) {
    ui_constrained_box_begin(&(UIConstrainedBoxProps){
        .constraints = props->constraints.value,
    });
  }

  if (props->color.present) {
    ui_colored_box_begin(&(UIColoredBoxProps){
        .color = props->color.value,
    });
  }

  if (props->padding.present) {
    ui_padding_begin(&(UIPaddingProps){
        .padding = props->padding.value,
    });
  }

  if (props->alignment.present) {
    ui_align_begin(&(UIAlignProps){
        .alignment = props->alignment.value,
    });
  }
}

void ui_container_end(void) {
  UIWidget *current = ui_widget_get_current();
  UIWidget *widget =
      ui_widget_find_first_ancestor(current, &ui_container_class);
  ASSERT(widget);
  UIContainerProps *props = ui_widget_get_props(widget, UIContainerProps);

  if (!current->first &&
      (!props->constraints.present ||
       !ui_box_constraints_is_tight(props->constraints.value))) {
    ui_limited_box_begin(&(UILimitedBoxProps){0});
    ui_constrained_box_begin(&(UIConstrainedBoxProps){
        .constraints = ui_box_constraints(F32_INFINITY, F32_INFINITY,
                                          F32_INFINITY, F32_INFINITY),
    });
    ui_constrained_box_end();
    ui_limited_box_end();
  }

  if (props->alignment.present) {
    ui_align_end();
  }

  if (props->padding.present) {
    ui_padding_end();
  }

  if (props->color.present) {
    ui_colored_box_end();
  }

  if (props->constraints.present) {
    ui_constrained_box_end();
  }

  if (props->margin.present) {
    ui_padding_end();
  }

  ui_widget_end(&ui_container_class);
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIFlexible
///
UIWidgetClass ui_flexible_class = {
    .name = "Flexible",
    .props_size = sizeof(UIFlexibleProps),
    .callback = &ui_widget_callback_default,
};

void ui_flexible_begin(const UIFlexibleProps *props) {
  UIWidget *widget = ui_widget_begin(&ui_flexible_class, props);
  UIParentDataFlex flex = {
      .flex = props->flex,
      .fit = props->fit,
  };
  ui_widget_set_parent_data(widget, UI_PARENT_DATA_FLEX,
                            sizeof(UIParentDataFlex), &flex);
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIExpanded
///
UIWidgetClass ui_expanded_class = {
    .name = "Expanded",
    .props_size = sizeof(UIExpandedProps),
    .callback = &ui_widget_callback_default,
};

void ui_expanded_begin(const UIExpandedProps *props) {
  UIWidget *widget = ui_widget_begin(&ui_expanded_class, props);
  UIParentDataFlex flex = {
      .flex = props->flex,
      .fit = UI_FLEX_FIT_TIGHT,
  };
  ui_widget_set_parent_data(widget, UI_PARENT_DATA_FLEX,
                            sizeof(UIParentDataFlex), &flex);
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIFlex
///
static inline UIBoxConstraints ui_box_constraints_for_non_flex_child(
    UIFlexProps *flex, UIBoxConstraints constraints) {
  bool should_fill_cross_axis = false;
  if (flex->cross_axis_alignment == UI_CROSS_AXIS_ALIGNMENT_STRETCH) {
    should_fill_cross_axis = true;
  }

  UIBoxConstraints result;
  switch (flex->direction) {
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
    UIFlexProps *flex, UIBoxConstraints constraints, f32 max_child_extent,
    UIParentDataFlex *data) {
  ASSERT(data->flex > 0);
  ASSERT(max_child_extent >= 0.0f);
  f32 min_child_extent = 0.0;
  if (data->fit == UI_FLEX_FIT_TIGHT) {
    min_child_extent = max_child_extent;
  }
  bool should_fill_cross_axis = false;
  if (flex->cross_axis_alignment == UI_CROSS_AXIS_ALIGNMENT_STRETCH) {
    should_fill_cross_axis = true;
  }
  UIBoxConstraints result;
  if (flex->direction == UI_AXIS_HORIZONTAL) {
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

static UIFlexLayoutSize ui_flex_compute_size(UIWidget *widget,
                                             UIFlexProps *flex,
                                             UIBoxConstraints constraints) {
  // Determine used flex factor, size inflexible items, calculate free space.
  f32 max_main_size;
  switch (flex->direction) {
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
      ui_box_constraints_for_non_flex_child(flex, constraints);
  // TODO: Baseline aligned

  // The first pass lays out non-flex children and computes total flex.
  i32 total_flex = 0;
  UIWidget *first_flex_child = 0;
  // Initially, accumulated_size is the sum of the spaces between children in
  // the main axis.
  AxisSize accumulated_size =
      axis_size(flex->spacing * (widget->child_count - 1), 0.0f);
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
      Vec2 child_size_raw =
          ui_widget_layout_box(child, non_flex_child_constraints);
      AxisSize child_size =
          axis_size_from_size(child_size_raw, flex->direction);

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
        flex, constraints, max_child_extent, data);
    Vec2 child_size_raw = ui_widget_layout_box(child, child_constraints);
    AxisSize child_size = axis_size_from_size(child_size_raw, flex->direction);

    accumulated_size.main += child_size.main;
    accumulated_size.cross = f32_max(accumulated_size.cross, child_size.cross);
  }
  DEBUG_ASSERT(total_flex == 0);

  f32 ideal_main_size;
  if (flex->main_axis_size == UI_MAIN_AXIS_SIZE_MAX &&
      f32_is_finite(max_main_size)) {
    ideal_main_size = max_main_size;
  } else {
    ideal_main_size = accumulated_size.main;
  }

  AxisSize size = axis_size(ideal_main_size, accumulated_size.cross);
  AxisSize constrained_size =
      axis_size_constrains(size, constraints, flex->direction);

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

static void ui_flex_layout(UIWidget *widget, UIFlexProps *flex,
                           UIBoxConstraints constraints) {
  UIFlexLayoutSize sizes = ui_flex_compute_size(widget, flex, constraints);
  f32 cross_axis_extent = sizes.size.cross;
  widget->size =
      convert_size(vec2(sizes.size.main, sizes.size.cross), flex->direction);
  // TODO: Handle overflow.

  f32 remaining_space = f32_max(0.0f, sizes.main_axis_free_space);
  // TODO: Handle text direction and vertical direction.
  f32 leading_space;
  f32 between_space;
  ui_flex_distribute_space(flex->main_axis_alignment, remaining_space,
                           widget->child_count, /* flipped= */ false,
                           flex->spacing, &leading_space, &between_space);

  // Position all children in visual order: starting from the top-left child and
  // work towards the child that's farthest away from the origin.
  f32 child_main_position = leading_space;
  for (UIWidget *child = widget->first; child; child = child->next) {
    f32 child_cross_position = ui_flex_get_child_cross_axis_offset(
        flex->cross_axis_alignment,
        cross_axis_extent -
            ui_flex_get_cross_size(child->size, flex->direction),
        /* flipped= */ false);
    if (flex->direction == UI_AXIS_HORIZONTAL) {
      child->offset = vec2(child_main_position, child_cross_position);
    } else {
      child->offset = vec2(child_cross_position, child_main_position);
    }
    child_main_position +=
        ui_flex_get_main_size(child->size, flex->direction) + between_space;
  }
}

static i32 ui_flex_callback(UIWidget *widget, UIMessage *message) {
  i32 result = 0;
  switch (message->type) {
    case UI_MESSAGE_LAYOUT_BOX: {
      ui_flex_layout(widget, ui_widget_get_props(widget, UIFlexProps),
                     message->layout_box.constraints);
    } break;
    default: {
      result = ui_widget_callback_default(widget, message);
    } break;
  }
  return result;
}

UIWidgetClass ui_flex_class = {
    .name = "Flex",
    .flags = UI_WIDGET_MANY_CHILDREN,
    .props_size = sizeof(UIFlexProps),
    .callback = &ui_flex_callback,
};

////////////////////////////////////////////////////////////////////////////////
///
/// UIColumn
///
UIWidgetClass ui_column_class = {
    .name = "Column",
    .flags = UI_WIDGET_MANY_CHILDREN,
    .props_size = sizeof(UIFlexProps),
    .callback = &ui_flex_callback,
};

void ui_column_begin(const UIColumnProps *props) {
  UIFlexProps flex = {
      .key = props->key,
      .direction = UI_AXIS_VERTICAL,
      .main_axis_alignment = props->main_axis_alignment,
      .main_axis_size = props->main_axis_size,
      .cross_axis_alignment = props->cross_axis_alignment,
      .spacing = props->spacing,
  };
  ui_widget_begin(&ui_column_class, &flex);
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIRow
///
UIWidgetClass ui_row_class = {
    .name = "Row",
    .flags = UI_WIDGET_MANY_CHILDREN,
    .props_size = sizeof(UIFlexProps),
    .callback = &ui_flex_callback,
};

void ui_row_begin(const UIRowProps *props) {
  UIFlexProps flex = {
      .key = props->key,
      .direction = UI_AXIS_HORIZONTAL,
      .main_axis_alignment = props->main_axis_alignment,
      .main_axis_size = props->main_axis_size,
      .cross_axis_alignment = props->cross_axis_alignment,
      .spacing = props->spacing,
  };
  ui_widget_begin(&ui_row_class, &flex);
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIPointerListener
///
typedef struct UIPointerListenerState {
  UIPointerEventO down;
  UIPointerEventO move;
  UIPointerEventO up;
  UIPointerEventO cancel;
} UIPointerListenerState;

static i32 ui_pointer_listener_callback(UIWidget *widget, UIMessage *message) {
  i32 result = 0;
  switch (message->type) {
    case UI_MESSAGE_HANDLE_EVENT: {
      UIPointerListenerState *state =
          ui_widget_get_state(widget, UIPointerListenerState);
      UIPointerEvent *event = message->handle_event.event;
      switch (event->type) {
        case UI_POINTER_EVENT_DOWN: {
          state->down = ui_pointer_event_some(*event);
        } break;
        case UI_POINTER_EVENT_MOVE: {
          state->move = ui_pointer_event_some(*event);
        } break;
        case UI_POINTER_EVENT_UP: {
          state->up = ui_pointer_event_some(*event);
        } break;
        case UI_POINTER_EVENT_CANCEL: {
          state->cancel = ui_pointer_event_some(*event);
        } break;
        default: {
        } break;
      }
    } break;
    default: {
      result = ui_widget_callback_default(widget, message);
    } break;
  }
  return result;
}

UIWidgetClass ui_pointer_listener_class = {
    .name = "PointerListener",
    .props_size = sizeof(UIPointerListenerProps),
    .state_size = sizeof(UIPointerListenerState),
    .callback = &ui_pointer_listener_callback,
};

void ui_pointer_listener_begin(const UIPointerListenerProps *props) {
  UIWidget *widget = ui_widget_begin(&ui_pointer_listener_class, props);
  UIPointerListenerState *state =
      ui_widget_get_state(widget, UIPointerListenerState);
  if (props->down) {
    *props->down = state->down;
  }
  if (props->move) {
    *props->move = state->move;
  }
  if (props->up) {
    *props->up = state->up;
  }
  if (props->cancel) {
    *props->cancel = state->cancel;
  }

  *state = (UIPointerListenerState){0};
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

static i32 ui_mouse_region_callback(UIWidget *widget, UIMessage *message) {
  i32 result = 0;
  switch (message->type) {
    case UI_MESSAGE_HANDLE_EVENT: {
      UIMouseRegionState *state =
          ui_widget_get_state(widget, UIMouseRegionState);
      UIPointerEvent *event = message->handle_event.event;
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
    } break;
    default: {
      result = ui_widget_callback_default(widget, message);
    } break;
  }
  return result;
}

UIWidgetClass ui_mouse_region_class = {
    .name = "MouseRegion",
    .props_size = sizeof(UIMouseRegionProps),
    .state_size = sizeof(UIMouseRegionState),
    .callback = &ui_mouse_region_callback,
};

void ui_mouse_region_begin(const UIMouseRegionProps *props) {
  UIWidget *widget = ui_widget_begin(&ui_mouse_region_class, props);
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

  *state = (UIMouseRegionState){0};
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIGestureDetector
///
typedef struct UIGestureDetectorState {
  u32 down_button;
} UIGestureDetectorState;

UIWidgetClass ui_gesture_detector_class = {
    .name = "GestureDetector",
    .props_size = sizeof(UIGestureDetectorProps),
    .state_size = sizeof(UIGestureDetectorState),
    .callback = &ui_widget_callback_default,
};

void ui_gesture_detector_begin(const UIGestureDetectorProps *props) {
  UIWidget *widget = ui_widget_begin(&ui_gesture_detector_class, props);
  UIGestureDetectorState *state =
      ui_widget_get_state(widget, UIGestureDetectorState);

  UIPointerEventO down;
  UIPointerEventO up;
  ui_pointer_listener_begin(&(UIPointerListenerProps){
      .down = &down,
      .up = &up,
  });

  bool tap_down = false;
  bool tap_up = false;
  bool tap = false;

  if (down.present) {
    if (down.value.button & UI_BUTTON_PRIMARY) {
      tap_down = true;
    }
  }

  if (up.present) {
    if (up.value.button & UI_BUTTON_PRIMARY) {
      tap_up = true;
    }
  }

  if (down.present) {
    state->down_button = down.value.button;
  } else if (up.present) {
    if (vec2_contains(up.value.local_position, vec2_zero(), widget->size)) {
      if ((state->down_button & UI_BUTTON_PRIMARY)) {
        tap = true;
      }
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
  f32 font_size;
} UITextState;

static i32 ui_text_callback(UIWidget *widget, UIMessage *message) {
  ASSERTF(!widget->first, "UIText should be a leaf node");

  i32 result = 0;
  switch (message->type) {
    case UI_MESSAGE_LAYOUT_BOX: {
      UITextProps *props = ui_widget_get_props(widget, UITextProps);
      UITextState *state = ui_widget_get_state(widget, UITextState);

      UIBoxConstraints constraints = message->layout_box.constraints;
      state->constraints = constraints;

      // TODO: Get default text style from widget tree.
      f32 font_size = 13;
      if (props->style.present) {
        if (props->style.value.font_size.present) {
          font_size = props->style.value.font_size.value;
        }
      }
      state->font_size = font_size;

      TextMetrics metrics = layout_text_str8(
          props->text, font_size, constraints.min_width, constraints.max_width);
      // TODO: Handle overflow.
      widget->size = ui_box_constraints_constrain(constraints, metrics.size);
    } break;

    case UI_MESSAGE_PAINT: {
      UITextProps *props = ui_widget_get_props(widget, UITextProps);
      UITextState *state = ui_widget_get_state(widget, UITextState);

      // TODO: Get default text style from widget tree.
      UIColor color = ui_color(1, 1, 1, 1);
      if (props->style.present) {
        if (props->style.value.color.present) {
          color = props->style.value.color.value;
        }
      }
      draw_text_str8(message->paint.offset, props->text, state->font_size,
                     state->constraints.min_width, state->constraints.max_width,
                     (ColorU32){
                         (u8)(color.a * 255.0f),
                         (u8)(color.r * 255.0f),
                         (u8)(color.g * 255.0f),
                         (u8)(color.b * 255.0f),
                     });
    } break;

    case UI_MESSAGE_HIT_TEST: {
      result = ui_widget_hit_test_opaque(widget, message->hit_test.result,
                                         message->hit_test.arena,
                                         message->hit_test.local_position);
    } break;

    default: {
      result = ui_widget_callback_default(widget, message);
    } break;
  }
  return result;
}

UIWidgetClass ui_text_class = {
    .name = "Text",
    .props_size = sizeof(UITextProps),
    .state_size = sizeof(UITextState),
    .callback = &ui_text_callback,
};

void ui_text(const UITextProps *props) {
  ui_widget_begin(&ui_text_class, props);
  ui_widget_end(&ui_text_class);
}

////////////////////////////////////////////////////////////////////////////////
///
/// UIButton
///
typedef struct UIButtonState {
  bool hovered;
  bool down;
} UIButtonState;

UIWidgetClass ui_button_class = {
    .name = "Button",
    .props_size = sizeof(UIButtonProps),
    .state_size = sizeof(UIButtonState),
    .callback = &ui_widget_callback_default,
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

  UIWidget *widget = ui_widget_begin(&ui_button_class, props);
  UIButtonState *state = ui_widget_get_state(widget, UIButtonState);

  UIPointerEventO enter;
  UIPointerEventO exit;
  bool down;
  bool up;
  ui_mouse_region_begin(&(UIMouseRegionProps){
      .enter = &enter,
      .exit = &exit,
  });
  if (enter.present) {
    state->hovered = true;
  }
  if (exit.present) {
    state->hovered = false;
  }

  ui_gesture_detector_begin(&(UIGestureDetectorProps){
      .tap_down = &down,
      .tap_up = &up,
      .tap = props->pressed,
  });

  if (down) {
    state->down = true;
  }
  if (up) {
    state->down = false;
  }

  ui_colored_box_begin(&(UIColoredBoxProps){
      .color = state->down ? splash_color
                           : (state->hovered ? hover_color : fill_color),
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
/// UISliverList.
///
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

static void ui_sliver_fixed_extent_list_layout_sliver(
    UIWidget *widget, UISliverConstraints constraints) {
  UISliverFixedExtentListProps *props =
      ui_widget_get_props(widget, UISliverFixedExtentListProps);
  f32 scroll_offset = constraints.scroll_offset + constraints.cache_origin;
  ASSERT(scroll_offset >= 0.0f);
  f32 remaining_extent = constraints.remaining_cache_extent;
  ASSERT(remaining_extent >= 0.0f);
  f32 target_end_scroll_offset = scroll_offset + remaining_extent;

  f32 item_extent = props->item_extent;
  i32 first_index = ui_sliver_fixed_extent_list_get_min_child_index(
      item_extent, scroll_offset);
  bool has_target_last_index = f32_is_finite(target_end_scroll_offset);
  i32 target_last_index = has_target_last_index
                              ? ui_sliver_fixed_extent_list_get_max_child_index(
                                    item_extent, target_end_scroll_offset)
                              : 0;

  // For now, assume the index of first child is 0.
  // TODO: builder indicates that they used first_index to build the first
  // child.

  ASSERT(first_index >= 0);
  UIWidget *child = ui_widget_get_child_nth(widget, first_index);
  i32 child_index = first_index;
  while (child && child_index <= target_last_index) {
    UIBoxConstraints child_constraints =
        ui_sliver_constraints_as_box_constraints(constraints, item_extent,
                                                 item_extent);
    ui_widget_layout_box(child, child_constraints);
    f32 layout_offset = child_index * item_extent;

    child = child->next;
    child_index += 1;
  }
}

static i32 ui_sliver_fixed_extent_list_callback(UIWidget *widget,
                                                UIMessage *message) {
  i32 result = 0;
  switch (message->type) {
    case UI_MESSAGE_LAYOUT_BOX: {
      DEBUG_ASSERTF(false, "UI_MESSAGE_LAYOUT_BOX is not implemented for %s",
                    widget->klass->name);
    } break;

    case UI_MESSAGE_LAYOUT_SLIVER: {
      ui_sliver_fixed_extent_list_layout_sliver(
          widget, message->layout_sliver.constraints);
    } break;

    default: {
      result = ui_widget_callback_default(widget, message);
    } break;
  }
  return result;
}

UIWidgetClass ui_sliver_fixed_extent_list_class = {
    .name = "SliverFixedExtentList",
    .flags = UI_WIDGET_MANY_CHILDREN,
    .props_size = sizeof(UISliverFixedExtentListProps),
    .callback = &ui_sliver_fixed_extent_list_callback,
};
