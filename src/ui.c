#include "src/ui.h"

#include <string.h>

#include "src/assert.h"
#include "src/list.h"
#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"

typedef struct UIWidgetHashSlot UIWidgetHashSlot;
struct UIWidgetHashSlot {
  UIWidgetHashSlot *prev;
  UIWidgetHashSlot *next;
  UIWidget *first;
  UIWidget *last;
};

typedef struct UIWidgetHashMap {
  u32 total_count;
  u32 slots_count;
  UIWidgetHashSlot *slots;
} UIWidgetHashMap;

typedef struct UIFrame {
  Arena arena;
  UIWidgetHashMap cache;
  UIWidget *root;
  UIWidget *current;
} UIFrame;

typedef struct UIState {
  UIFrame frames[2];
  u64 frame_index;
  UIFrame *current_frame;
  UIFrame *last_frame;
} UIState;

THREAD_LOCAL UIState t_ui_state;

static inline UIState *ui_state_get(void) { return &t_ui_state; }

static inline UIFrame *ui_frame_get(void) {
  UIState *state = ui_state_get();
  return state->current_frame;
}

void ui_begin_frame(void) {
  UIState *state = ui_state_get();
  if (state->current_frame) {
    ASSERT(!state->current_frame->current);
  }

  state->frame_index += 1;
  state->current_frame =
      state->frames + (state->frame_index % ARRAY_COUNT(state->frames));
  state->last_frame =
      state->frames + ((state->frame_index - 1) % ARRAY_COUNT(state->frames));

  UIFrame *frame = state->current_frame;
  arena_clear(&frame->arena);

  frame->cache = (UIWidgetHashMap){0};
  frame->cache.slots_count = 4096;
  frame->cache.slots = arena_push_array(&frame->arena, UIWidgetHashSlot,
                                        frame->cache.slots_count);
  frame->root = frame->current = 0;
}

void ui_end_frame(void) {
  UIState *state = ui_state_get();
  UIFrame *frame = state->current_frame;
  ASSERTF(!frame->current, "Mismatched begin/end calls");

  // Layout and render
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
static UIKey ui_key_make_local(UIKey seed, u32 seq, const char *tag, Str8 id) {
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

static void ui_widget_render(void *widget) { (void)widget; }

static UIWidgetVTable ui_widget_vtable = (UIWidgetVTable){
    .render = &ui_widget_render,
};

static UIWidget *ui_widget_get(UIFrame *frame, UIKey key) {
  UIWidget *result = 0;
  UIWidgetHashMap *cache = &frame->cache;
  if (!ui_key_is_zero(key) && cache->slots) {
    UIWidgetHashSlot *slot = cache->slots + (key.hash % cache->slots_count);
    for (UIWidget *widget = slot->first; widget; widget = widget->hash.next) {
      if (ui_key_is_equal(widget->key, key)) {
        result = widget;
        break;
      }
    }
  }
  return result;
}

static UIWidget *ui_widget_push(UIFrame *frame, usize size, UIKey key) {
  UIWidget *widget = arena_push(&frame->arena, size, 0);
  widget->key = key;

  UIWidgetHashSlot *slot =
      frame->cache.slots + (key.hash % frame->cache.slots_count);
  DLL_APPEND(slot->first, slot->last, widget, hash.prev, hash.next);
  ++frame->cache.total_count;

  return widget;
}

static void *ui_widget_begin(const char *tag, usize size, UIKey key) {
  UIState *state = ui_state_get();
  UIFrame *frame = state->current_frame;
  UIFrame *last_frame = state->last_frame;
  UIWidget *parent = frame->current;

  if (ui_key_is_zero(key)) {
    UIKey seed = key;
    if (parent) {
      seed = parent->key;
    }
    u32 seq = 0;
    if (parent) {
      seq = parent->children_count;
    }
    key = ui_key_make_local(seed, seq, tag, str8_zero());
  }

  DEBUG_ASSERT(size >= sizeof(UIWidget));
  UIWidget *widget = ui_widget_push(frame, size, key);
  widget->vtable = &ui_widget_vtable;
  if (parent) {
    DLL_APPEND(parent->tree.first, parent->tree.last, widget, tree.prev,
               tree.next);
    ++parent->children_count;
    widget->tree.parent = parent;
  } else {
    frame->root = widget;
  }
  widget->tag = tag;

  // Copy additional data from last frame
  usize additional_bytes = size - sizeof(UIWidget);
  if (additional_bytes > 0) {
    UIWidget *last_widget = ui_widget_get(last_frame, key);
    if (last_widget) {
      ASSERTF(strcmp(last_widget->tag, tag) == 0,
              "Tag of is changed from %s to %s", last_widget->tag, tag);
      memory_copy(widget + 1, last_widget + 1, additional_bytes);
    }
  }

  frame->current = widget;

  return widget;
}

static void ui_widget_end(const char *tag) {
  UIFrame *frame = ui_frame_get();
  UIWidget *widget = frame->current;
  ASSERT(widget);
  ASSERTF(strcmp(widget->tag, tag) == 0,
          "Mismatched begin/end calls. Begin with %s, end with %s", widget->tag,
          tag);

  frame->current = widget->tree.parent;
}

static void ui_flex_render(void *widget) { UIFlex *flex = widget; }

static UIWidgetVTable ui_flex_vtable = (UIWidgetVTable){
    .render = &ui_flex_render,
};

void ui_flex_begin(UIFlexProps props) {
  UIFlex *flex = ui_widget_begin("Flex", sizeof(UIFlex), props.key);
  flex->widget.vtable = &ui_flex_vtable;
  flex->direction = props.direction;
  flex->main_axis_alignment = props.main_axis_alignment;
  flex->main_axis_size = props.main_axis_size;
  flex->cross_axis_alignment = props.cross_axis_alignment;
  flex->spacing = props.spacing;
}

void ui_flex_end(void) { ui_widget_end("Flex"); }
