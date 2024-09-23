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
    u64 build_index;

    // per-frame info
    Widget *current;
};

UIState g_ui_state;

static WidgetKey
widget_key_zero(void) {
    WidgetKey result = {0};
    return result;
}

static WidgetKey
widget_key_from_str8(WidgetKey seed, Str8 str) {
    // djb2 hash function
    u64 hash = seed.hash ? seed.hash : 5381;
    for (usize i = 0; i < str.len; i += 1) {
        // hash * 33 + c
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

static Widget *
push_widget(Arena *arena) {
    Widget *result = push_array(arena, Widget, 1);
    return result;
}

static UIState *
get_ui_state() {
    UIState *state = &g_ui_state;
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
begin_ui() {
    UIState *state = get_ui_state();

    Widget *root = state->root;
    root->first = root->last = 0;

    state->current = state->root;
}

static void
end_ui() {
    UIState *state = get_ui_state();
    state->build_index++;
}

static Widget *
get_widget_by_key(UIState *state, WidgetKey key) {
    Widget *result = 0;
    if (!equal_widget_key(key, widget_key_zero())) {
        WidgetHashSlot *slot =
            &state->widget_hash_slots[key.hash % state->widget_hash_slot_size];
        for (Widget *widget = slot->first; widget; widget = widget->hash_next) {
            if (equal_widget_key(widget->key, key)) {
                result = widget;
                break;
            }
        }
    }
    return result;
}

static void
begin_widget_str8(Str8 str) {
    UIState *state = get_ui_state();

    Widget *parent = state->current;
    WidgetKey key = widget_key_from_str8(parent->key, str);
    Widget *widget = get_widget_by_key(state, key);
    if (!widget) {
        widget = get_or_push_widget(state);
        widget->key = key;
        WidgetHashSlot *slot =
            &state->widget_hash_slots[key.hash % state->widget_hash_slot_size];
        append_doubly_linked_list(
            slot->first, slot->last, widget, hash_prev, hash_next
        );
    }

    append_doubly_linked_list(parent->first, parent->last, widget, prev, next);
    widget->parent = parent;

    // Clear per frame state
    widget->first = widget->last = 0;

    state->current = widget;
}

static void
end_widget() {
    UIState *state = get_ui_state();

    assert(
        state->current && state->current != state->root &&
        "Unmatched begin_widget and end_widget calls"
    );

    state->current = state->current->parent;
}
