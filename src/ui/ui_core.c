typedef struct WidgetHashSlot WidgetHashSlot;
struct WidgetHashSlot {
    Widget *first;
    Widget *last;
};

typedef struct UIState UIState;
struct UIState {
    Arena *arena;

    // widget cache
    Widget *first_free_widget;
    u32 widget_hash_slot_size;
    WidgetHashSlot *widget_hash_slots;

    Widget *root;
    u64 frame_index;

    // per-frame info
    Widget *current;
};

UIState g_ui_context;
Widget g_nil_widget;

static WidgetKey
widget_key_zero(void) {
    WidgetKey result = {0};
    return result;
}

static WidgetKey
widget_key_from_str8(WidgetKey seed, Str8 str) {
    u64 hash = seed.hash;
    for (usize i = 0; i < str.len; i += 1) {
        hash = ((hash << 5) + hash) + str.ptr[i];
    }
    WidgetKey result = {hash};
    return result;
}

static b32
equal_widget_key(WidgetKey a, WidgetKey b) {
    b32 result = a.hash == b.hash;
    return result;
}

static inline b32
is_nil_widget(Widget *widget) {
    b32 result = widget == 0 || widget == &g_nil_widget;
    return result;
}

static Widget *
push_widget(Arena *arena) {
    Widget *result = push_array(arena, Widget, 1);
    return result;
}

static UIState *
get_ui_state() {
    UIState *state = &g_ui_context;
    if (!state->arena) {
        state->arena = alloc_arena();
        state->widget_hash_slot_size = 4096;
        state->widget_hash_slots = push_array(
            state->arena, WidgetHashSlot, state->widget_hash_slot_size
        );

        state->root = push_widget(state->arena);
        state->current = state->root;
    }
    return state;
}

static Widget *
get_or_push_widget(UIState *state) {
    Widget *result;
    if (state->first_free_widget) {
        result = state->first_free_widget;
        state->first_free_widget = result->next;
        zero_memory(result, sizeof(*result));
    } else {
        result = push_widget(state->arena);
    }
    return result;
}

static void
begin_ui_frame() {
    UIState *state = get_ui_state();
    state->current = state->root;
}

static void
end_ui_frame() {
    UIState *state = get_ui_state();
    state->frame_index++;
}

static Widget *
get_widget_by_key(UIState *state, WidgetKey key) {
    Widget *result = &g_nil_widget;
    if (!equal_widget_key(key, widget_key_zero())) {
        u64 slot = key.hash % state->widget_hash_slot_size;
        for (Widget *widget = state->widget_hash_slots[slot].first;
             !is_nil_widget(widget);
             widget = widget->hash_next) {
            if (equal_widget_key(widget->key, key)) {
                result = widget;
                break;
            }
        }
    }
    return result;
}

static void
begin_widget(WidgetKey key) {
    UIState *state = get_ui_state();

    Widget *parent = state->current;
    Widget *widget = get_widget_by_key(state, key);
    if (is_nil_widget(widget)) {
        widget = get_or_push_widget(state);
        widget->key = key;

        WidgetHashSlot *slot =
            &state->widget_hash_slots[key.hash % state->widget_hash_slot_size];
        // TODO: insert into hash slot.
    }
}

static void
end_widget() {}
