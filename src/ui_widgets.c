#include "src/ui_widgets.h"

#include <inttypes.h>
#include <stdarg.h>

#include "src/assert.h"
#include "src/math.h"
#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"
#include "src/ui.h"

void ui_row_begin(UIRowProps props) {
  ui_tag_begin("Row", (UIProps){
                          .key = props.key,
                          .size = props.size,
                          .padding = props.padding,
                          .margin = props.margin,
                          .border = props.border,
                          .color = props.color,
                          .background_color = props.background_color,
                          .main_axis_align = props.main_axis_align,
                          .main_axis = kAxis2X,
                          .main_axis_size = kUIMainAxisSizeMax,
                          .cross_axis_align =
                              props.cross_axis_align != kUICrossAxisAlignUnknown
                                  ? props.cross_axis_align
                                  : kUICrossAxisAlignCenter,
                      });
}

void ui_column_begin(UIColumnProps props) {
  ui_tag_begin(
      "Column",
      (UIProps){
          .key = props.key,
          .size = props.size,
          .padding = props.padding,
          .margin = props.margin,
          .border = props.border,
          .color = props.color,
          .background_color = props.background_color,
          .main_axis_align = props.main_axis_align,
          .main_axis = kAxis2Y,
          .main_axis_size = kUIMainAxisSizeMax,
          .cross_axis_align = props.cross_axis_align != kUICrossAxisAlignUnknown
                                  ? props.cross_axis_align
                                  : kUICrossAxisAlignCenter,
      });
}

void ui_text(UITextProps props) {
  ui_tag_begin("Text", (UIProps){
                           .key = props.key,
                           .size = props.size,
                           .text = props.text,
                           .padding = props.padding,
                           .margin = props.margin,
                           .border = props.border,
                           .color = props.color,
                           .background_color = props.background_color,
                       });
  ui_tag_end("Text");
}

typedef struct UIButtonState {
  ColorU32 background_color;
  ColorU32 target_background_color;
} UIButtonState;

bool ui_button_begin(UIButtonProps props) {
  UIEdgeInsets padding = props.padding;
  if (!ui_edge_insets_is_set(padding)) {
    // TODO: Use theme
    padding = ui_edge_insets_symmetric(6, 3);
  }

  bool clicked;
  ui_tag_begin("Button", (UIProps){
                             .size = props.size,
                             .position = props.position,
                             .offset = props.offset,
                             .padding = padding,
                             .text = props.text,
                         });
  {
    UIButtonState *state = ui_box_push_struct(UIButtonState);

    if (ui_is_mouse_button_down(kUIMouseButtonLeft)) {
      state->target_background_color = color_u32_from_hex(0x4B6F9E);
    } else if (ui_is_mouse_hovering()) {
      state->target_background_color = color_u32_from_hex(0x4B7DB8);
    } else if (props.default_background_color) {
      state->target_background_color = color_u32_from_hex(0xB9D3F3);
    } else {
      state->target_background_color = color_u32_zero();
    }

    if (props.hoverred) {
      *props.hoverred = ui_is_mouse_hovering();
    }

    clicked = ui_is_mouse_button_clicked(kUIMouseButtonLeft);

    state->background_color = ui_animate_fast_color_u32(
        state->background_color, state->target_background_color);

    ui_box_get_current()->props.background_color = state->background_color;
  }

  return clicked;
}

void ui_button_end(void) { ui_tag_end("Button"); }

typedef struct UICollapsingState {
  bool init;
  f32 open_t;
  UIBox *header;
} UICollapsingState;

bool ui_collapsing_begin(UICollapsingProps props) {
  ui_tag_begin("Collapsing", (UIProps){0});
  UICollapsingState *state = ui_box_push_struct(UICollapsingState);
  if (!state->init) {
    if (props.open && *props.open) {
      state->open_t = 1.0f;
    }
    state->init = true;
  }

  ui_column_begin((UIColumnProps){0});
  {
    bool clicked = ui_button_begin((UIButtonProps){
        .default_background_color = props.header.default_background_color,
        .padding = ui_edge_insets_all(0),
        .hoverred = props.header.hoverred,
    });
    state->header = ui_box_get_current();
    {
      ui_row_begin((UIRowProps){
          .padding = props.header.padding,
      });
      {
        if (props.open && clicked) {
          *props.open = !*props.open;
        }

        Str8 prefix = str8_lit("   ");
        if (props.open) {
          prefix = *props.open ? str8_lit(" - ") : str8_lit(" + ");
        }

        ui_box_begin((UIProps){
            .text = ui_push_str8f("%s%s", prefix.ptr, props.header.text.ptr),
        });
        ui_box_end();
      }
      ui_row_end();
    }
    ui_button_end();

    // Clip box
    ui_box_begin((UIProps){.isolate = true});

    ui_box_begin((UIProps){0});
    UIBox *content = ui_box_get_current();
    if (props.open && *props.open && content->computed.size.y == 0) {
      // For the first frame, the content size is unknown. Make margin -INF
      // effectively make it invisible.
      content->props.margin =
          ui_edge_insets_from_ltrb(0, -kUISizeInfinity, 0, 0);
    } else {
      content->props.margin = ui_edge_insets_from_ltrb(
          0, (1.0f - state->open_t) * -content->computed.size.y, 0, 0);
    }
  }

  state->open_t = ui_animate_fast_f32(
      state->open_t, (props.open && *props.open) ? 1.0f : 0.0f);

  bool result = state->open_t != 0.0f;
  return result;
}

void ui_collapsing_end(void) {
  {
    ui_box_end();
    ui_box_end();
  }
  ui_column_end();
  ui_tag_end("Collapsing");
}

typedef struct UIScrollableState {
  b32 init;

  // persistent info
  f32 scroll;
  f32 *target_scroll;
  f32 control_offset_drag_start;

  // per-frame info
  f32 scroll_area_size;
  f32 scroll_max;
  Vec2 head_size;
  f32 scroll_step;
  f32 control_max;
  f32 control_offset;
  f32 control_size;
} UIScrollableState;

void ui_scrollable_begin(UIScrollableProps props) {
  ui_tag_begin("Scrollable", (UIProps){
                                 .main_axis = kAxis2X,
                             });
  {
    UIScrollableState *state = ui_box_push_struct(UIScrollableState);
    state->target_scroll = props.scroll;

    Vec2 wheel_delta;
    b32 scrolling = ui_is_mouse_button_scrolling(&wheel_delta);

    ui_tag_begin("ScrollArea",
                 (UIProps){
                     .flex = 1,
                     .main_axis = kAxis2Y,
                     .size = v2(kUISizeUndefined, kUISizeInfinity),
                 });
    {
      UIBox *scroll_area = ui_box_get_current();
      state->scroll_area_size = scroll_area->computed.size.y;

      ui_tag_begin("ScrollContent",
                   (UIProps){
                       .margin = ui_edge_insets_from_lt(0, -state->scroll),
                       .isolate = true,
                   });
      UIBox *scroll_content = ui_box_get_current();
      f32 total_item_size = scroll_content->computed.size.y;

      state->head_size = v2(10, 0);
      state->scroll_max = f32_max(total_item_size - state->scroll_area_size, 0);
      // Assume first frame if scroll_max is 0
      if (state->scroll_max) {
        if (state->target_scroll) {
          *state->target_scroll =
              f32_clamp(*state->target_scroll, 0, state->scroll_max);
          state->scroll =
              ui_animate_fast_f32(state->scroll, *state->target_scroll);
        }
        // Only clamp scroll for non-first frame
        state->scroll = f32_clamp(state->scroll, 0, state->scroll_max);
      } else {
        if (state->target_scroll) {
          state->scroll = *state->target_scroll;
        }
      }

      f32 min_control_size = 4;
      f32 free_size =
          f32_max(state->scroll_area_size - 2 * state->head_size.y, 0.0f);
      state->control_size =
          f32_min(f32_max(state->scroll_area_size / total_item_size * free_size,
                          min_control_size),
                  free_size);

      state->scroll_step = 0.2f * state->scroll_area_size;

      state->control_max = free_size - state->control_size;
      state->control_offset =
          (state->scroll / state->scroll_max) * state->control_max;
      if (state->target_scroll && scrolling) {
        *state->target_scroll = f32_clamp(
            *state->target_scroll + wheel_delta.y * state->scroll_step, 0,
            state->scroll_max);
      }

      // ...
    }
  }
}

static void ui_scrollable_scroll_bar(UIScrollableState *state) {
  ui_tag_begin("ScrollBar", (UIProps){0});
  ui_column_begin((UIColumnProps){0});
  {
    Vec2 mouse_pos = ui_get_mouse_rel_pos();
    if (ui_is_mouse_button_down(kUIMouseButtonLeft)) {
      if (f32_contains(mouse_pos.x, 0, state->head_size.x) &&
          state->target_scroll) {
        f32 offset = mouse_pos.y - state->head_size.y;
        if (offset < state->control_offset) {
          *state->target_scroll =
              f32_clamp(*state->target_scroll - 0.2f * state->scroll_step, 0,
                        state->scroll_max);
        } else if (offset > state->control_offset + state->control_size) {
          *state->target_scroll =
              f32_clamp(*state->target_scroll + 0.2f * state->scroll_step, 0,
                        state->scroll_max);
        }
      }
    }
    ColorU32 background_color = color_u32_from_hex(0xF5F5F5);

    ui_box_begin((UIProps){
        .size = v2(state->head_size.x, state->control_offset),
        .background_color = background_color,
    });
    ui_box_end();

    ui_box_begin((UIProps){0});
    {
      ColorU32 control_background_color = color_u32_from_hex(0xBEBEBE);
      if (ui_is_mouse_hovering()) {
        control_background_color = color_u32_from_hex(0x959595);
      }
      if (ui_is_mouse_button_pressed(kUIMouseButtonLeft)) {
        state->control_offset_drag_start = state->control_offset;
      }
      Vec2 drag_delta;
      if (ui_is_mouse_button_dragging(kUIMouseButtonLeft, &drag_delta) &&
          state->target_scroll) {
        f32 offset = state->control_offset_drag_start + drag_delta.y;
        *state->target_scroll =
            f32_clamp(offset / state->control_max * state->scroll_max, 0,
                      state->scroll_max);

        control_background_color = color_u32_from_hex(0x7D7D7D);
      }

      ui_box_begin((UIProps){
          .size = v2(state->head_size.x, state->control_size),
          .background_color = control_background_color,
      });
      ui_box_end();
    }
    ui_box_end();

    ui_box_begin((UIProps){
        .size = v2(state->head_size.x, kUISizeUndefined),
        .flex = 1,
        .background_color = background_color,
    });
    ui_box_end();
  }
  ui_column_end();
  ui_tag_end("ScrollBar");
}

void ui_scrollable_end(void) {
  {
    {
      // ...
      ui_tag_end("ScrollContent");
    }
    ui_tag_end("ScrollArea");

    UIScrollableState *state = ui_box_get_struct(UIScrollableState);
    if (state->scroll_max > 0) {
      ui_scrollable_scroll_bar(state);
    }
  }
  ui_tag_end("Scrollable");
}

typedef struct UIBoxDebugState UIBoxDebugState;
struct UIBoxDebugState {
  UIBoxDebugState *child[4];
  UIID id;

  bool open;
};

typedef struct UIDebugLayerState {
  bool init;
  bool open;
  Vec2 min;
  Vec2 max;
  Vec2 pressed_min;
  Vec2 pressed_max;
  f32 scroll;
  Rect2 hoverred_rect;
  UIBox *debug_layer;

  Arena *arena;
  UIBoxDebugState *root;
} UIDebugLayerState;

static UIBoxDebugState *push_ui_box_debug_state(UIDebugLayerState *state,
                                                UIID id, bool build_order) {
  id = uuid_from_u8(id, (u8)build_order);

  UIBoxDebugState **node = &state->root;
  for (u64 hash = id.hash; *node; hash <<= 2) {
    if (uuid_is_zero(id) || uuid_is_equal(id, (*node)->id)) {
      break;
    }
    node = &((*node)->child[hash >> 62]);
  }
  if (!*node) {
    UIBoxDebugState *debug_state =
        arena_push_array(state->arena, UIBoxDebugState, 1);
    debug_state->id = id;
    (*node) = debug_state;
  }
  return *node;
}

static void ui_debug_layer_box_r(UIDebugLayerState *state, UIBox *box,
                                 u32 level, bool build_order) {
  if (box == state->debug_layer) {
    return;
  }

  UIBoxDebugState *box_debug_state =
      push_ui_box_debug_state(state, box->id, build_order);

  bool has_key = !str8_is_empty(box->props.key);
  Str8 text = ui_push_str8f("%s%s%s", box->tag, has_key ? "#" : "",
                            has_key ? (char *)box->props.key.ptr : "");

  bool header_hovered;
  if (ui_collapsing_begin((UICollapsingProps){
          .open = (build_order ? box->build.first : box->stack.first)
                      ? &box_debug_state->open
                      : 0,
          .header =
              (UICollapsingHeaderProps){
                  .text = text,
                  .padding = ui_edge_insets_from_ltrb(level * 15, 0, 0, 0),
                  .hoverred = &header_hovered,
              },
      })) {
    ui_column_begin((UIColumnProps){0});
    for (UIBox *child = (build_order ? box->build.first : box->stack.first);
         child; child = (build_order ? child->build.next : child->stack.next)) {
      ui_debug_layer_box_r(state, child, level + 1, build_order);
    }
    ui_column_end();
  }
  ui_collapsing_end();

  if (header_hovered) {
    Rect2 hoverred_rect = box->computed.screen_rect;
    {
      Vec2 size = vec2_sub(hoverred_rect.max, hoverred_rect.min);
      // Make zero area box visible.
      if (size.x == 0 && size.y != 0) {
        hoverred_rect.max.x = hoverred_rect.min.x + 1;
        hoverred_rect.min.x -= 1;
      } else if (size.y == 0 && size.x != 0) {
        hoverred_rect.max.y = hoverred_rect.min.y + 1;
        hoverred_rect.min.y -= 1;
      }
    }
    state->hoverred_rect = hoverred_rect;
  }
}

static void ui_debug_layer_internal(UIDebugLayerState *state) {
  UIState *ui_state = ui_state_get();
  UIFrame *frame = ui_state->frames + ((ui_state->frame_index - 1) %
                                       ARRAY_COUNT(ui_state->frames));

  ui_box_begin((UIProps){
      .padding = ui_edge_insets_symmetric(6, 3),
  });
  ui_column_begin((UIColumnProps){0});
  ui_box_begin((UIProps){0});
  {
    ui_column_begin((UIColumnProps){0});

    ui_row_begin((UIRowProps){0});
    ui_text((UITextProps){
        .text = ui_push_str8f("Boxes: %" PRIu64 " / %" PRIu64,
                              frame->cache.total_box_count,
                              frame->cache.box_hash_slots_count),
    });
    ui_row_end();

    ui_column_end();
  }
  ui_box_end();

  if (frame->root) {
    ui_debug_layer_box_r(state, frame->root, 0, /* build_order= */ true);
    ui_debug_layer_box_r(state, frame->root, 0, /* build_order= */ false);
  }

  ui_column_end();
  ui_box_end();
}

void ui_debug_layer(UIDebugLayerProps props) {
  ASSERTF(props.arena, "Must provide an arena");

  f32 resize_handle_size = 16;
  Vec2 default_frame_size = v2(400, 500);
  Vec2 min_frame_size = v2(resize_handle_size * 2, resize_handle_size * 2);

  ui_tag_begin("DebugLayer", (UIProps){
                                 .z_index = kUIDebugLayerZIndex,
                             });
  UIDebugLayerState *state = ui_box_push_struct(UIDebugLayerState);
  if (!state->init) {
    if (vec2_is_zero(vec2_sub(state->max, state->min))) {
      state->max =
          vec2_add(state->min, v2(default_frame_size.x + resize_handle_size,
                                  default_frame_size.y + resize_handle_size));
    }
    state->arena = props.arena;
    state->init = 1;
  }

  UIBox *debug_layer = ui_box_get_current();
  state->debug_layer = ui_box_get(ui_frame_get_last(), debug_layer->id);

  if (props.open) {
    state->open = *props.open;
  }

  state->hoverred_rect = rect2_zero();
  if (state->open) {
    ui_tag_begin(
        "Float",
        (UIProps){
            .size = vec2_sub(state->max, state->min),
            .color = color_u32_from_hex(0x000000),
            .position = kUIPositionFixed,
            .offset = ui_edge_insets_from_lt(state->min.x, state->min.y),
            .border = ui_border_from_border_side((UIBorderSide){
                .color = color_u32_from_hex(0xA8A8A8),
                .width = 1,
            }),
        });
    {
      ui_set_block_mouse_input();

      if (ui_is_mouse_button_pressed(kUIMouseButtonLeft)) {
        state->pressed_min = state->min;
        state->pressed_max = state->max;
      }
      Vec2 drag_delta;
      if (ui_is_mouse_button_dragging(kUIMouseButtonLeft, &drag_delta)) {
        Vec2 size = vec2_sub(state->max, state->min);
        state->min = vec2_round(vec2_add(state->pressed_min, drag_delta));
        state->max = vec2_add(state->min, size);
      }

      ui_box_begin((UIProps){
          .background_color = color_u32_from_hex(0xF0F0F0),
      });
      ui_column_begin((UIColumnProps){0});
      {
        ui_box_begin((UIProps){
            .background_color = color_u32_from_hex(0xD1D1D1),
        });
        ui_row_begin((UIRowProps){0});
        {
          UIEdgeInsets padding = ui_edge_insets_symmetric(6, 3);
          ui_text((UITextProps){
              .text = str8_lit("Debug"),
              .padding = padding,
          });

          ui_box_begin((UIProps){.flex = 1});
          ui_box_end();

          if (DoUIButton((UIButtonProps){
                  .text = str8_lit("X"),
                  .padding = padding,
              })) {
            state->open = false;
            if (props.open) {
              *props.open = false;
            }
          }
        }
        ui_row_end();
        ui_box_end();

        ui_scrollable_begin((UIScrollableProps){.scroll = &state->scroll});
        ui_debug_layer_internal(state);
        ui_scrollable_end();
      }
      ui_column_end();
      ui_box_end();

      ui_button_begin((UIButtonProps){
          .default_background_color = 1,
          .position = kUIposition_absolute,
          .offset = ui_edge_insets_from_rb(1, 1),
          .size = v2(resize_handle_size, resize_handle_size),
      });
      {
        if (ui_is_mouse_button_pressed(kUIMouseButtonLeft)) {
          state->pressed_min = state->min;
          state->pressed_max = state->max;
        }
        Vec2 drag_delta;
        if (ui_is_mouse_button_dragging(kUIMouseButtonLeft, &drag_delta)) {
          state->max = vec2_round(vec2_add(state->pressed_max, drag_delta));
          state->max =
              vec2_max(state->max, vec2_add(state->min, min_frame_size));
        }
      }
      ui_button_end();
    }
    ui_tag_end("Float");

    ui_tag_begin("Highlight", (UIProps){
                                  .z_index = -1,
                                  .position = kUIPositionFixed,
                                  .size = v2(kUISizeInfinity, kUISizeInfinity),
                              });
    if (rect2_get_area(state->hoverred_rect) > 0) {
      Rect2 hoverred_rect = state->hoverred_rect;

      ui_box_begin((UIProps){
          .background_color =
              color_u32_from_srgb_not_premultiplied(255, 0, 255, 64),
          .size = vec2_sub(hoverred_rect.max, hoverred_rect.min),
          .margin =
              ui_edge_insets_from_lt(hoverred_rect.min.x, hoverred_rect.min.y),
      });
      ui_box_end();
    }
    ui_tag_end("Highlight");
  }
  ui_tag_end("DebugLayer");
}
