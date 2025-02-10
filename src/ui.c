#include "src/ui.h"

#include <string.h>

#include "src/assert.h"
#include "src/draw.h"
#include "src/list.h"
#include "src/math.h"
#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"

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
} UIFrame;

typedef struct UIWidgetStackEntry {
  UIWidget *widget;
  UIHitTestEntry *hit_test_entry;
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
                                 UIWidget *widget, UIWidget *last_widget,
                                 UIHitTestEntry *hit_test_entry) {
  UIWidgetStackEntry *entry =
      arena_push_array(&stack->arena, UIWidgetStackEntry, 1);
  entry->widget = widget;
  entry->last_widget = last_widget;
  if (last_widget) {
    entry->last_child = last_widget->first;
  }
  entry->hit_test_entry = hit_test_entry;
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

typedef struct UIInputState {
  Vec2 last_pointer_pos;

  Arena hit_test_arena;
  UIHitTestResult *hit_test_result;
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

void ui_set_viewport(Vec2 min, Vec2 max) {
  UIState *state = ui_state_get();
  state->viewport_min = min;
  state->viewport_max = max;
}

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
}

static void ui_widget_mount(UIWidget *widget) {
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

static void ui_widget_unmount(UIWidget *widget) {
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

static void ui_widget_layout(UIWidget *widget, UIBoxConstraints constraints) {
  UIMessage message = {
      .layout =
          {
              .type = UI_MESSAGE_LAYOUT,
              .constraints = constraints,
          },
  };
  widget->klass->callback(widget, &message);
}

static void ui_widget_paint(UIWidget *widget, UIPaintingContext *context,
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

static bool ui_widget_get_parent_data(UIWidget *widget, u32 parent_data_id,
                                      void *parent_data) {
  UIMessage message = {
      .get_parent_data =
          {
              .type = UI_MESSAGE_GET_PARENT_DATA,
              .parent_data_id = parent_data_id,
              .parent_data = parent_data,
          },
  };
  return widget->klass->callback(widget, &message);
}

static bool ui_widget_hit_test(UIWidget *widget, UIHitTestResult *result,
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

static void ui_widget_handle_event(UIWidget *widget, UIPointerEvent *event) {
  UIMessage message = {
      .handle_event =
          {
              .type = UI_MESSAGE_HANDLE_EVENT,
              .event = event,
          },
  };
  widget->klass->callback(widget, &message);
}

void ui_on_mouse_button_down(Vec2 pos, u32 button) {
  UIState *state = ui_state_get();
  if (state->input.hit_test_result) {
    ui_on_mouse_button_up(pos, button);
  }

  ASSERT(!state->input.hit_test_result);

  UIWidget *root = ui_widget_get_root();
  if (!root) {
    return;
  }

  Arena *hit_test_arena = &state->input.hit_test_arena;
  state->input.hit_test_result =
      arena_push_struct(hit_test_arena, UIHitTestResult);
  if (ui_widget_hit_test(root, state->input.hit_test_result, hit_test_arena,
                         pos)) {
    for (UIHitTestEntry *entry = state->input.hit_test_result->first; entry;
         entry = entry->next) {
      ui_widget_handle_event(entry->widget,
                             &(UIPointerEvent){
                                 .type = UI_POINTER_EVENT_DOWN,
                                 .button = button,
                                 .position = pos,
                                 .local_position = entry->local_position,
                             });
    }
  } else {
    state->input.hit_test_result = 0;
    arena_clear(hit_test_arena);
  }
}

void ui_on_mouse_button_up(Vec2 pos, u32 button) {
  UIState *state = ui_state_get();
  if (!state->input.hit_test_result) {
    return;
  }

  for (UIHitTestEntry *entry = state->input.hit_test_result->first; entry;
       entry = entry->next) {
    ui_widget_handle_event(entry->widget,
                           &(UIPointerEvent){
                               .type = UI_POINTER_EVENT_UP,
                               .button = button,
                               .position = pos,
                               .local_position = entry->local_position,
                           });
  }

  state->input.hit_test_result = 0;
  arena_clear(&state->input.hit_test_arena);
}

void ui_on_mouse_move(Vec2 pos) {
  UIState *state = ui_state_get();
  if (vec2_is_equal(state->input.last_pointer_pos, pos)) {
    return;
  }
  state->input.last_pointer_pos = pos;

  if (state->input.hit_test_result) {
    for (UIHitTestEntry *entry = state->input.hit_test_result->first; entry;
         entry = entry->next) {
      ui_widget_handle_event(entry->widget,
                             &(UIPointerEvent){
                                 .type = UI_POINTER_EVENT_MOVE,
                                 .position = pos,
                                 .local_position = entry->local_position,
                             });
    }
  } else {
    UIWidget *root = ui_widget_get_root();
    if (!root) {
      return;
    }

    Scratch scratch = scratch_begin(0, 0);
    UIHitTestResult result = {0};
    if (ui_widget_hit_test(root, &result, scratch.arena, pos)) {
      for (UIHitTestEntry *entry = result.first; entry; entry = entry->next) {
        ui_widget_handle_event(entry->widget,
                               &(UIPointerEvent){
                                   .type = UI_POINTER_EVENT_HOVER,
                                   .position = pos,
                                   .local_position = entry->local_position,
                               });
      }
    }
    scratch_end(scratch);
  }
}

void ui_on_focus_lost(Vec2 pos) {
  UIState *state = ui_state_get();
  if (!state->input.hit_test_result) {
    return;
  }

  for (UIHitTestEntry *entry = state->input.hit_test_result->first; entry;
       entry = entry->next) {
    ui_widget_handle_event(entry->widget,
                           &(UIPointerEvent){
                               .type = UI_POINTER_EVENT_CANCEL,
                               .position = pos,
                               .local_position = entry->local_position,
                           });
  }

  state->input.hit_test_result = 0;
  arena_clear(&state->input.hit_test_arena);
}

static Vec2 ui_widget_layout_stack_children(UIWidget *widget,
                                            UIBoxConstraints constraints) {
  Vec2 max_child_size = vec2_zero();
  for (UIWidget *child = widget->first; child; child = child->next) {
    ui_widget_layout(child, constraints);
    max_child_size = vec2_max(max_child_size, child->size);
  }
  return max_child_size;
}

static void ui_widget_layout_default(UIWidget *widget,
                                     UIBoxConstraints constraints) {
  // The default layout just stacks children.
  Vec2 max_child_size = ui_widget_layout_stack_children(widget, constraints);
  widget->size = ui_box_constraints_constrain(constraints, max_child_size);
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

static i32 ui_widget_callback_default(UIWidget *widget, UIMessage *message) {
  i32 result = 0;
  switch (message->type) {
    case UI_MESSAGE_LAYOUT: {
      ui_widget_layout_default(widget, message->layout.constraints);
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
  UIState *state = ui_state_get();
  UIFrame *frame = state->current_frame;
  ASSERTF(!state->widget_stack.current,
          "Mismatched begin/end calls, last begin: %s",
          state->widget_stack.current->widget->klass->name);

  // Layout and paint
  if (frame->root) {
    unmount_widgets_if_not(state->last_frame->root);

    Vec2 viewport_size = vec2_sub(state->viewport_max, state->viewport_min);
    ui_widget_layout(frame->root, ui_box_constraints_tight(viewport_size.x,
                                                           viewport_size.y));

    UIPaintingContext context = {0};
    ui_widget_paint(frame->root, &context, state->viewport_min);
  }
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
static void ui_widget_append(UIWidget *parent, UIWidget *child) {
  child->parent = parent;
  DLL_APPEND(parent->first, parent->last, child, prev, next);
  ++parent->child_count;
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
  UIHitTestEntry *hit_test_entry = 0;

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

    ui_widget_append(parent->widget, widget);
    if (parent->last_child) {
      parent->last_child = parent->last_child->next;
    }

    if (parent->hit_test_entry) {
      hit_test_entry = parent->hit_test_entry->prev;
    }
  } else {
    ASSERTF(!frame->root, "root widget already exists.");
    frame->root = widget;
    last_widget = state->last_frame->root;

    if (state->input.hit_test_result) {
      hit_test_entry = state->input.hit_test_result->last;
    }
  }

  if (last_widget) {
    ASSERT(last_widget->status == UI_WIDGET_STATUS_MOUNTED);
    widget->state = last_widget->state;
    widget->status = UI_WIDGET_STATUS_MOUNTED;
    last_widget->state = 0;
    // The state is transfered into current, effectively make last unmounted.
    last_widget->status = UI_WIDGET_STATUS_UNMOUNTED;
  } else {
    ui_widget_mount(widget);
  }

  // Update widget references in input.hit_test_result so we can send events
  // to widgets later.
  if (hit_test_entry && hit_test_entry->widget == last_widget) {
    hit_test_entry->widget = widget;
  } else {
    hit_test_entry = 0;
  }

  ui_widget_stack_push(&state->widget_stack, &state->build_arena, widget,
                       last_widget, hit_test_entry);
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
  UIBoxConstraints limited_constraints =
      ui_limited_box_limit_constraints(limited_box, constraints);

  if (widget->first) {
    Vec2 max_child_size =
        ui_widget_layout_stack_children(widget, limited_constraints);
    widget->size = ui_box_constraints_constrain(constraints, max_child_size);
  } else {
    widget->size =
        ui_box_constraints_constrain(limited_constraints, vec2_zero());
  }
}

static i32 ui_limited_box_callback(UIWidget *widget, UIMessage *message) {
  i32 result = 0;
  switch (message->type) {
    case UI_MESSAGE_LAYOUT: {
      ui_limited_box_layout(widget,
                            ui_widget_get_props(widget, UILimitedBoxProps),
                            message->layout.constraints);
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
  UIBoxConstraints enforced_constraints =
      ui_box_constraints_enforce(constrained_box->constraints, constraints);
  Vec2 max_child_size =
      ui_widget_layout_stack_children(widget, enforced_constraints);
  widget->size =
      ui_box_constraints_constrain(enforced_constraints, max_child_size);
}

static i32 ui_constrained_box_callback(UIWidget *widget, UIMessage *message) {
  i32 result = 0;
  switch (message->type) {
    case UI_MESSAGE_LAYOUT: {
      ui_constrained_box_layout(
          widget, ui_widget_get_props(widget, UIConstrainedBoxProps),
          message->layout.constraints);
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
    Vec2 max_child_size = vec2_zero();

    for (UIWidget *child = widget->first; child; child = child->next) {
      ui_widget_layout(child, child_constraints);

      Vec2 wrap_size =
          vec2(should_shrink_wrap_width
                   ? (child->size.x * (width.present ? width.value : 1.0f))
                   : F32_INFINITY,
               should_shrink_wrap_height
                   ? (child->size.y * (height.present ? height.value : 1.0f))
                   : F32_INFINITY);

      max_child_size = vec2_max(max_child_size, wrap_size);
    }

    widget->size = ui_box_constraints_constrain(constraints, max_child_size);

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
    case UI_MESSAGE_LAYOUT: {
      ui_align_layout(widget, ui_widget_get_props(widget, UIAlignProps),
                      message->layout.constraints);
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

  UIBoxConstraints child_constraints =
      ui_box_constraints(0, F32_INFINITY, 0, F32_INFINITY);
  Vec2 max_child_size =
      ui_widget_layout_stack_children(widget, child_constraints);
  widget->size = ui_box_constraints_constrain(constraints, max_child_size);

  ui_widget_align_children(widget, unconstrained_box->alignment);
}

static i32 ui_unconstrained_box_callback(UIWidget *widget, UIMessage *message) {
  i32 result = 0;
  switch (message->type) {
    case UI_MESSAGE_LAYOUT: {
      ui_unconstrained_box_layout(
          widget, ui_widget_get_props(widget, UIUnconstrainedBoxProps),
          message->layout.constraints);
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
    case UI_MESSAGE_LAYOUT: {
      UICenterProps *center = ui_widget_get_props(widget, UICenterProps);
      UIAlignProps align = {
          .key = center->key,
          .width = center->width,
          .height = center->height,
      };
      ui_align_layout(widget, &align, message->layout.constraints);
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
    Vec2 max_child_size = vec2_zero();

    for (UIWidget *child = widget->first; child; child = child->next) {
      ui_widget_layout(child, inner_constraints);
      child->offset = vec2(resolved_padding.left, resolved_padding.top);

      max_child_size = vec2_max(max_child_size, child->size);
    }

    widget->size = ui_box_constraints_constrain(
        constraints,
        vec2(horizontal + max_child_size.x, vertical + max_child_size.y));
  } else {
    widget->size =
        ui_box_constraints_constrain(constraints, vec2(horizontal, vertical));
  }
}

static i32 ui_padding_callback(UIWidget *widget, UIMessage *message) {
  i32 result = 0;
  switch (message->type) {
    case UI_MESSAGE_LAYOUT: {
      ui_padding_layout(widget, ui_widget_get_props(widget, UIPaddingProps),
                        message->layout.constraints);
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
static i32 ui_flexible_callback(UIWidget *widget, UIMessage *message) {
  i32 result = 0;
  switch (message->type) {
    case UI_MESSAGE_GET_PARENT_DATA: {
      if (message->get_parent_data.parent_data_id ==
          UI_WIDGET_PARENT_DATA_FLEX) {
        UIFlexibleProps *flexible =
            ui_widget_get_props(widget, UIFlexibleProps);
        UIWidgetParentDataFlex *data = message->get_parent_data.parent_data;
        *data = (UIWidgetParentDataFlex){
            .flex = flexible->flex,
            .fit = flexible->fit,
        };
        result = 1;
      }
    } break;
    default: {
      result = ui_widget_callback_default(widget, message);
    } break;
  }
  return result;
}

UIWidgetClass ui_flexible_vtable = {
    .name = "Flexible",
    .props_size = sizeof(UIFlexibleProps),
    .callback = &ui_flexible_callback,
};

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
    UIWidgetParentDataFlex data) {
  DEBUG_ASSERT(data.flex > 0);
  DEBUG_ASSERT(max_child_extent >= 0.0f);
  f32 min_child_extent = 0.0;
  if (data.fit == UI_FLEX_FIT_TIGHT) {
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
      UIWidgetParentDataFlex data;
      if (ui_widget_get_parent_data(child, UI_WIDGET_PARENT_DATA_FLEX, &data)) {
        child_flex = data.flex;
      }
    }

    if (child_flex > 0) {
      total_flex += child_flex;
      if (!first_flex_child) {
        first_flex_child = child;
      }
    } else {
      ui_widget_layout(child, non_flex_child_constraints);
      AxisSize child_size = axis_size_from_size(child->size, flex->direction);

      accumulated_size.main += child_size.main;
      accumulated_size.cross += child_size.cross;
    }
  }

  DEBUG_ASSERT((total_flex == 0) == (first_flex_child == 0));
  DEBUG_ASSERT(first_flex_child == 0 || can_flex);

  // The second pass distributes free space to flexible children.
  f32 flex_space = f32_max(0.0f, max_main_size - accumulated_size.main);
  f32 space_per_flex = flex_space / total_flex;
  for (UIWidget *child = widget->first; child && total_flex > 0;
       child = child->next) {
    UIWidgetParentDataFlex data;
    bool has_parent_data =
        ui_widget_get_parent_data(child, UI_WIDGET_PARENT_DATA_FLEX, &data);
    if (!has_parent_data || data.flex <= 0) {
      continue;
    }
    total_flex -= data.flex;
    DEBUG_ASSERT(f32_is_finite(space_per_flex));
    f32 max_child_extent = space_per_flex * data.flex;
    DEBUG_ASSERT(data.fit == UI_FLEX_FIT_LOOSE ||
                 max_child_extent < F32_INFINITY);
    UIBoxConstraints child_constraints = ui_box_constraints_for_flex_child(
        flex, constraints, max_child_extent, data);
    ui_widget_layout(child, child_constraints);
    AxisSize child_size = axis_size_from_size(child->size, flex->direction);

    accumulated_size.main += child_size.main;
    accumulated_size.cross += child_size.cross;
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
    case UI_MESSAGE_LAYOUT: {
      ui_flex_layout(widget, ui_widget_get_props(widget, UIFlexProps),
                     message->layout.constraints);
    } break;
    default: {
      result = ui_widget_callback_default(widget, message);
    } break;
  }
  return result;
}

UIWidgetClass ui_flex_class = {
    .name = "Flex",
    .props_size = sizeof(UIFlexProps),
    .callback = &ui_flex_callback,
};

////////////////////////////////////////////////////////////////////////////////
///
/// UIColumn
///
static i32 ui_column_callback(UIWidget *widget, UIMessage *message) {
  i32 result = 0;
  switch (message->type) {
    case UI_MESSAGE_LAYOUT: {
      UIColumnProps *column = ui_widget_get_props(widget, UIColumnProps);
      UIFlexProps flex = {
          .key = column->key,
          .direction = UI_AXIS_VERTICAL,
          .main_axis_alignment = column->main_axis_alignment,
          .main_axis_size = column->main_axis_size,
          .cross_axis_alignment = column->cross_axis_alignment,
          .spacing = column->spacing,
      };
      ui_flex_layout(widget, &flex, message->layout.constraints);
    } break;
    default: {
      result = ui_widget_callback_default(widget, message);
    } break;
  }
  return result;
}

UIWidgetClass ui_column_class = {
    .name = "Column",
    .props_size = sizeof(UIColumnProps),
    .callback = &ui_column_callback,
};

////////////////////////////////////////////////////////////////////////////////
///
/// UIRow
///
static i32 ui_row_callback(UIWidget *widget, UIMessage *message) {
  i32 result = 0;
  switch (message->type) {
    case UI_MESSAGE_LAYOUT: {
      UIRowProps *row = ui_widget_get_props(widget, UIRowProps);
      UIFlexProps flex = {
          .key = row->key,
          .direction = UI_AXIS_HORIZONTAL,
          .main_axis_alignment = row->main_axis_alignment,
          .main_axis_size = row->main_axis_size,
          .cross_axis_alignment = row->cross_axis_alignment,
          .spacing = row->spacing,
      };
      ui_flex_layout(widget, &flex, message->layout.constraints);
    } break;
    default: {
      result = ui_widget_callback_default(widget, message);
    } break;
  }
  return result;
}

UIWidgetClass ui_row_class = {
    .name = "Row",
    .props_size = sizeof(UIRowProps),
    .callback = &ui_row_callback,
};

////////////////////////////////////////////////////////////////////////////////
///
/// UIPointerListener
///
typedef struct UIPointerListenerState {
  UIPointerEventO down;
  UIPointerEventO move;
  UIPointerEventO up;
  UIPointerEventO cancel;
  UIPointerEventO hover;
} UIPointerListenerState;

static i32 ui_pointer_listener_callback(UIWidget *widget, UIMessage *message) {
  i32 result = 0;
  switch (message->type) {
    case UI_MESSAGE_MOUNT: {
      widget->state = memory_alloc(sizeof(UIPointerListenerState));
    } break;

    case UI_MESSAGE_UNMOUNT: {
      memory_free(widget->state, sizeof(UIPointerListenerState));
    } break;

    case UI_MESSAGE_HANDLE_EVENT: {
      UIPointerListenerState *state = (UIPointerListenerState *)widget->state;
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
        case UI_POINTER_EVENT_HOVER: {
          state->hover = ui_pointer_event_some(*event);
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
    .callback = &ui_pointer_listener_callback,
};

void ui_pointer_listener_begin(const UIPointerListenerProps *props) {
  UIWidget *widget = ui_widget_begin(&ui_pointer_listener_class, props);
  ASSERT(widget->state);
  UIPointerListenerState *state = widget->state;
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
  if (props->hover) {
    *props->hover = state->hover;
  }

  *state = (UIPointerListenerState){0};
}
