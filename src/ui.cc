#include "src/ui.h"

#include "src/assert.h"
#include "src/list.h"

struct WidgetHashSlot {
  Widget *first;
  Widget *last;
};

struct UIState {
  Arena *arena;

  // widget cache
  Widget *first_free_widget;
  u32 widget_hash_slot_size;
  WidgetHashSlot *widget_hash_slots;

  Widget *root;
  u64 build_index;

  // per-frame info
  Widget *current;
};

UIState g_ui_state;

WidgetKey WidgetKeyZero(void) {
  WidgetKey result = {0};
  return result;
}

WidgetKey WidgetKeyFromStr8(WidgetKey seed, Str8 str) {
  // djb2 hash function
  u64 hash = seed.hash ? seed.hash : 5381;
  for (usize i = 0; i < str.len; i += 1) {
    // hash * 33 + c
    hash = ((hash << 5) + hash) + str.ptr[i];
  }
  WidgetKey result = {hash};
  return result;
}

bool EqualWidgetKey(WidgetKey a, WidgetKey b) {
  b32 result = a.hash == b.hash;
  return result;
}

static Widget *PushWidget(Arena *arena) {
  Widget *result = PushArray(arena, Widget, 1);
  return result;
}

static UIState *GetUIState() {
  UIState *state = &g_ui_state;
  if (!state->arena) {
    state->arena = AllocArena();
    state->widget_hash_slot_size = 4096;
    state->widget_hash_slots =
        PushArray(state->arena, WidgetHashSlot, state->widget_hash_slot_size);

    state->root = PushWidget(state->arena);
    state->current = state->root;
  }
  return state;
}

static Widget *GetOrPushWidget(UIState *state) {
  Widget *result;
  if (state->first_free_widget) {
    result = state->first_free_widget;
    state->first_free_widget = result->next;
    ZeroMemory(result, sizeof(*result));
  } else {
    result = PushWidget(state->arena);
  }
  return result;
}

void BeginUI() {
  UIState *state = GetUIState();

  Widget *root = state->root;
  root->first = root->last = 0;

  state->current = state->root;
}

void EndUI() {
  UIState *state = GetUIState();
  state->build_index++;
}

static Widget *GetWidgetByKey(UIState *state, WidgetKey key) {
  Widget *result = 0;
  if (!EqualWidgetKey(key, WidgetKeyZero())) {
    WidgetHashSlot *slot =
        &state->widget_hash_slots[key.hash % state->widget_hash_slot_size];
    for (Widget *widget = slot->first; widget; widget = widget->hash_next) {
      if (EqualWidgetKey(widget->key, key)) {
        result = widget;
        break;
      }
    }
  }
  return result;
}

void BeginWidget(Str8 str) {
  UIState *state = GetUIState();

  Widget *parent = state->current;
  WidgetKey key = WidgetKeyFromStr8(parent->key, str);
  Widget *widget = GetWidgetByKey(state, key);
  if (!widget) {
    widget = GetOrPushWidget(state);
    widget->key = key;
    WidgetHashSlot *slot =
        &state->widget_hash_slots[key.hash % state->widget_hash_slot_size];
    APPEND_DOUBLY_LINKED_LIST(slot->first, slot->last, widget, hash_prev,
                              hash_next);
  }

  APPEND_DOUBLY_LINKED_LIST(parent->first, parent->last, widget, prev, next);
  widget->parent = parent;

  // Clear per frame state
  widget->first = widget->last = 0;

  state->current = widget;
}

void EndWidget() {
  UIState *state = GetUIState();

  ASSERT(state->current && state->current != state->root &&
         "Unmatched begin_widget and end_widget calls");

  state->current = state->current->parent;
}
