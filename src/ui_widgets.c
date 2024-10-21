#include "src/ui_widgets.h"

#include <inttypes.h>
#include <stdarg.h>
#include <string.h>

#include "src/math.h"
#include "src/string.h"
#include "src/types.h"
#include "src/ui.h"

UIBox *BeginUIRow(UIRowProps props) {
  return BeginUITag(
      "Row",
      (UIProps){
          .key = props.key,
          .size = props.size,
          .padding = props.padding,
          .margin = props.margin,
          .background_color = props.background_color,
          .main_axis = kAxis2X,
          .main_axis_size = kUIMainAxisSizeMax,
          .main_axis_align = props.main_axis_align == kUIMainAxisAlignUnknown
                                 ? kUIMainAxisAlignStart
                                 : props.main_axis_align,
          .cross_axis_align = props.cross_axis_align == kUICrossAxisAlignUnknown
                                  ? kUICrossAxisAlignCenter
                                  : props.cross_axis_align,
      });
}

UIBox *BeginUIColumn(UIColumnProps props) {
  return BeginUITag(
      "Column",
      (UIProps){
          .key = props.key,
          .size = props.size,
          .padding = props.padding,
          .margin = props.margin,
          .background_color = props.background_color,
          .main_axis = kAxis2Y,
          .main_axis_align = props.main_axis_align == kUIMainAxisAlignUnknown
                                 ? kUIMainAxisAlignStart
                                 : props.main_axis_align,
          .main_axis_size = kUIMainAxisSizeMax,
          .cross_axis_align = props.cross_axis_align == kUICrossAxisAlignUnknown
                                  ? kUICrossAxisAlignCenter
                                  : props.cross_axis_align,
      });
}

UIBox *BeginUIStack(UIStackProps props) {
  return BeginUITag(
      "Stack",
      (UIProps){
          .key = props.key,
          .layout = kUILayoutStack,
          .size = props.size,
          .padding = props.padding,
          .margin = props.margin,
          .background_color = props.background_color,
          .main_axis_align = props.main_axis_align == kUIMainAxisAlignUnknown
                                 ? kUIMainAxisAlignStart
                                 : props.main_axis_align,
          .cross_axis_align = props.cross_axis_align == kUICrossAxisAlignUnknown
                                  ? kUICrossAxisAlignCenter
                                  : props.cross_axis_align,
      });
}

void DoUITextF(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  Str8 text = PushUIStr8FV(fmt, ap);
  va_end(ap);

  DoUIText(text);
}

void DoUIText(Str8 text) {
  BeginUITag("Text", (UIProps){.text = text});
  EndUITag("Text");
}

typedef struct UIButtonState {
  ColorU32 background_color;
  ColorU32 target_background_color;
} UIButtonState;

UIBox *BeginUIButton(UIButtonProps props, b32 *out_clicked) {
  UIEdgeInsets padding;
  if (props.padding) {
    padding = *props.padding;
  } else {
    padding = UIEdgeInsetsSymmetric(6, 3);
  }
  UIBox *button = BeginUITag("Button", (UIProps){
                                           .size = props.size,
                                           .padding = padding,
                                       });
  {
    UIButtonState *state = PushUIBoxStruct(button, UIButtonState);

    if (IsUIMouseButtonDown(button, kUIMouseButtonLeft)) {
      state->target_background_color = ColorU32FromHex(0x4B6F9E);
    } else if (IsUIMouseHovering(button)) {
      state->target_background_color = ColorU32FromHex(0x4B7DB8);
    } else if (props.default_background_color) {
      state->target_background_color = ColorU32FromHex(0xB9D3F3);
    } else {
      state->target_background_color = ColorU32Zero();
    }

    b32 clicked = IsUIMouseButtonClicked(button, kUIMouseButtonLeft);
    if (out_clicked) {
      *out_clicked = clicked;
    }

    state->background_color = AnimateUIFastColorU32(
        state->background_color, state->target_background_color);

    button->props.background_color = state->background_color;
  }

  return button;
}

void EndUIButton(void) { EndUITag("Button"); }

typedef struct UICollapsingState {
  b32 init;
  b32 open;
  f32 open_t;
  UIBox *header;
} UICollapsingState;

UIBox *BeginUICollapsing(UICollapsingProps props, b32 *out_open) {
  UIBox *collapsing = BeginUITag("Collapse", (UIProps){0});
  UICollapsingState *state = PushUIBoxStruct(collapsing, UICollapsingState);
  if (!state->init) {
    state->open = props.default_open;
    state->open_t = state->open;
    state->init = 1;
  }
  state->open = !props.disabled && state->open;

  BeginUIColumn((UIColumnProps){0});
  {
    b32 clicked;
    UIEdgeInsets padding = UIEdgeInsetsAll(0);
    state->header =
        BeginUIButton((UIButtonProps){.default_background_color =
                                          props.default_background_color,
                                      .padding = &padding},
                      &clicked);
    {
      BeginUIRow((UIRowProps){
          .padding = props.header.padding,
      });
      {
        if (!props.disabled && clicked) {
          state->open = !state->open;
        }

        Str8 prefix = STR8_LIT("   ");
        if (!props.disabled) {
          prefix = state->open ? STR8_LIT(" - ") : STR8_LIT(" + ");
        }

        BeginUIBox((UIProps){
            .text = PushUIStr8F("%s%s", prefix.ptr, props.header.text.ptr),
        });
        EndUIBox();
      }
      EndUIRow();
    }
    EndUIButton();

    // Clip box
    BeginUIBox((UIProps){0});

    UIBox *content = BeginUIBox((UIProps){0});
    if (state->open && content->computed.size.y == 0) {
      // For the first frame, the content size is unknown. Make margin -INF
      // effectively make it invisible.
      content->props.margin = UIEdgeInsetsFromLTRB(0, -kUISizeInfinity, 0, 0);
    } else {
      content->props.margin = UIEdgeInsetsFromLTRB(
          0, (1.0f - state->open_t) * -content->computed.size.y, 0, 0);
    }
  }

  state->open_t = AnimateUIFastF32(state->open_t, !!state->open);

  if (out_open) {
    *out_open = state->open_t != 0.0f;
  }

  return collapsing;
}

void EndUICollapsing(void) {
  {
    EndUIBox();
    EndUIBox();
  }
  EndUIColumn();
  EndUITag("Collapse");
}

UIBox *GetUICollapsingHeader(UIBox *box) {
  UICollapsingState *state = GetUIBoxStruct(box, UICollapsingState);
  return state->header;
}

typedef struct UIScrollableState {
  b32 init;

  // persistent info
  f32 scroll;
  f32 target_scroll;
  f32 *target_scroll_ptr;
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

static inline void SetUIScrollableScrollInternal(UIScrollableState *state,
                                                 f32 scroll) {
  *state->target_scroll_ptr = ClampF32(scroll, 0, state->scroll_max);
}

UIBox *BeginUIScrollable(UIScrollableProps props) {
  UIBox *scrollable = BeginUITag("Scrollable", (UIProps){
                                                   .main_axis = kAxis2X,
                                               });
  {
    UIScrollableState *state = PushUIBoxStruct(scrollable, UIScrollableState);
    if (props.scroll) {
      if (!state->target_scroll_ptr) {
        state->scroll = *props.scroll;
      }
      state->target_scroll_ptr = props.scroll;
    } else {
      state->target_scroll_ptr = &state->target_scroll;
    }

    Vec2 wheel_delta;
    b32 scrolling = IsUIMouseScrolling(scrollable, &wheel_delta);

    UIBox *scroll_area = BeginUITag(
        "ScrollArea", (UIProps){
                          .flex = 1,
                          .main_axis = kAxis2Y,
                          .size = V2(kUISizeUndefined, kUISizeInfinity),
                      });
    {
      state->scroll_area_size = scroll_area->computed.size.y;

      UIBox *scroll_content = BeginUITag(
          "ScrollContent",
          (UIProps){
              .margin = UIEdgeInsetsFromLTRB(0, -state->scroll, 0, 0),
          });
      f32 total_item_size = scroll_content->computed.size.y;

      state->head_size = V2(10, 0);
      state->scroll_max = MaxF32(total_item_size - state->scroll_area_size, 0);
      if (state->scroll_max) {
        state->scroll = ClampF32(state->scroll, 0, state->scroll_max);
        *state->target_scroll_ptr =
            ClampF32(*state->target_scroll_ptr, 0, state->scroll_max);
      }

      f32 min_control_size = 4;
      f32 free_size =
          MaxF32(state->scroll_area_size - 2 * state->head_size.y, 0.0f);
      state->control_size =
          MinF32(MaxF32(state->scroll_area_size / total_item_size * free_size,
                        min_control_size),
                 free_size);

      state->scroll_step = 0.2f * state->scroll_area_size;

      state->control_max = free_size - state->control_size;
      state->control_offset =
          (state->scroll / state->scroll_max) * state->control_max;
      if (scrolling) {
        SetUIScrollableScrollInternal(
            state,
            *state->target_scroll_ptr + wheel_delta.y * state->scroll_step);
      }

      // ...
    }
  }
  return scrollable;
}

static void DoUIScrollableScrollBar(UIScrollableState *state) {
  UIBox *scroll_bar = BeginUITag("ScrollBar", (UIProps){0});
  BeginUIColumn((UIColumnProps){0});
  {
    Vec2 mouse_pos = GetUIMouseRelPos(scroll_bar);
    if (IsUIMouseButtonDown(scroll_bar, kUIMouseButtonLeft)) {
      if (ContainsF32(mouse_pos.x, 0, state->head_size.x)) {
        f32 offset = mouse_pos.y - state->head_size.y;
        if (offset < state->control_offset) {
          SetUIScrollableScrollInternal(
              state, *state->target_scroll_ptr - 0.2f * state->scroll_step);
        } else if (offset > state->control_offset + state->control_size) {
          SetUIScrollableScrollInternal(
              state, *state->target_scroll_ptr + 0.2f * state->scroll_step);
        }
      }
    }
    ColorU32 background_color = ColorU32FromHex(0xF5F5F5);

    BeginUIBox((UIProps){
        .size = V2(state->head_size.x, state->control_offset),
        .background_color = background_color,
    });
    EndUIBox();

    UIBox *scroll_control = BeginUIBox((UIProps){0});
    {
      ColorU32 control_background_color = ColorU32FromHex(0xBEBEBE);
      if (IsUIMouseHovering(scroll_control)) {
        control_background_color = ColorU32FromHex(0x959595);
      }
      if (IsUIMouseButtonPressed(scroll_control, kUIMouseButtonLeft)) {
        state->control_offset_drag_start = state->control_offset;
      }
      Vec2 drag_delta;
      if (IsUIMouseButtonDragging(scroll_control, kUIMouseButtonLeft,
                                  &drag_delta)) {
        f32 offset = state->control_offset_drag_start + drag_delta.y;
        SetUIScrollableScrollInternal(
            state, offset / state->control_max * state->scroll_max);

        control_background_color = ColorU32FromHex(0x7D7D7D);
      }

      BeginUIBox((UIProps){
          .size = V2(state->head_size.x, state->control_size),
          .background_color = control_background_color,
      });
      EndUIBox();
    }
    EndUIBox();

    BeginUIBox((UIProps){
        .size = V2(state->head_size.x, kUISizeUndefined),
        .flex = 1,
        .background_color = background_color,
    });
    EndUIBox();
  }
  EndUIColumn();
  EndUITag("ScrollBar");
}

void EndUIScrollable(void) {
  {
    {
      // ...
      EndUITag("ScrollContent");
    }
    EndUITag("ScrollArea");

    UIScrollableState *state =
        GetUIBoxStruct(GetCurrentUIBox(), UIScrollableState);
    if (state->scroll_max > 0) {
      DoUIScrollableScrollBar(state);
    }

    state->scroll = AnimateUIFastF32(state->scroll, *state->target_scroll_ptr);
  }
  EndUITag("Scrollable");
}

typedef struct UIDebugLayerState {
  b8 init;
  b8 open;
  Vec2 min;
  Vec2 max;
  Vec2 pressed_min;
  Vec2 pressed_max;
  f32 scroll;
} UIDebugLayerState;

static void UIDebugLayerBoxR(UIDebugLayerState *state, UIBox *box, u32 level) {
  Str8 seq_str = PushUIStr8F("%u", box->seq);
  Str8 text = PushUIStr8F(
      "%s%s%s", box->tag, "#",
      IsEmptyStr8(box->props.key) ? seq_str.ptr : box->props.key.ptr);

  b32 open;
  UIBox *collapsing = BeginUICollapsing(
      (UICollapsingProps){
          .disabled = !box->first,
          .default_open = 1,
          .header =
              (UICollapsingHeaderProps){
                  .text = text,
                  .padding = UIEdgeInsetsFromLTRB(level * 15, 0, 0, 0),

              },
      },
      &open);
  if (IsUIMouseHovering(GetUICollapsingHeader(collapsing))) {
    Rect2 hovered_rect = box->computed.screen_rect;
    {
      Vec2 size = SubVec2(hovered_rect.max, hovered_rect.min);
      // Make zero area box visible.
      if (size.x == 0 && size.y != 0) {
        hovered_rect.max.x = hovered_rect.min.x + 1;
        hovered_rect.min.x -= 1;
      } else if (size.y == 0 && size.x != 0) {
        hovered_rect.max.y = hovered_rect.min.y + 1;
        hovered_rect.min.y -= 1;
      }
    }

    BeginUILayer((UILayerProps){
        .key = STR8_LIT("__UIDebug__Overlay"),
        .z_index = kUIDebugLayerZIndex - 1,
    });
    BeginUIBox((UIProps){0});
    BeginUIBox((UIProps){
        .margin =
            UIEdgeInsetsFromLTRB(hovered_rect.min.x, hovered_rect.min.y, 0, 0),
        .size = SubVec2(hovered_rect.max, hovered_rect.min),
        .background_color = ColorU32FromSRGBNotPremultiplied(255, 0, 255, 64),
    });
    EndUIBox();
    EndUIBox();
    EndUILayer();
  }
  if (open) {
    BeginUIColumn((UIColumnProps){0});
    for (UIBox *child = box->first; child; child = child->next) {
      UIDebugLayerBoxR(state, child, level + 1);
    }
    EndUIColumn();
  }
  EndUICollapsing();
}

static void UIDebugLayerInternal(UIDebugLayerState *state) {
  UIState *ui_state = GetUIState();
  UIFrame *frame = ui_state->frames + ((ui_state->frame_index - 1) %
                                       ARRAY_COUNT(ui_state->frames));

  BeginUIBox((UIProps){
      .padding = UIEdgeInsetsSymmetric(6, 3),
  });
  BeginUIColumn((UIColumnProps){0});
  BeginUIBox((UIProps){0});
  {
    BeginUIColumn((UIColumnProps){0});

    BeginUIRow((UIRowProps){0});
    DoUITextF("Boxes: %" PRIu64 " / %" PRIu64, frame->cache.total_box_count,
              frame->cache.box_hash_slots_count);
    EndUIRow();

    EndUIColumn();
  }
  EndUIBox();

  for (UILayer *layer = frame->last_layer; layer; layer = layer->prev) {
    if (strstr((char *)layer->props.key.ptr, "__UIDebug__") == 0) {
      b32 open;
      BeginUICollapsing(
          (UICollapsingProps){
              .default_background_color = 1,
              .default_open = 1,
              .header =
                  (UICollapsingHeaderProps){
                      .text = layer->props.key,
                  },
          },
          &open);
      if (open) {
        if (layer->root) {
          UIDebugLayerBoxR(state, layer->root, 1);
        }
      }
      EndUICollapsing();
    }
  }
  EndUIColumn();
  EndUIBox();
}

UIBox *UIDebugLayer(void) {
  f32 resize_handle_size = 16;
  Vec2 default_frame_size = V2(400, 500);
  Vec2 min_frame_size = V2(resize_handle_size * 2, resize_handle_size * 2);

  BeginUILayer((UILayerProps){
      .key = STR8_LIT("__UIDebug__"),
      .z_index = kUIDebugLayerZIndex,
  });
  UIBox *debug_layer = BeginUIBox((UIProps){0});
  UIDebugLayerState *state = PushUIBoxStruct(debug_layer, UIDebugLayerState);
  if (!state->init) {
    if (IsZeroVec2(SubVec2(state->max, state->min))) {
      state->max =
          AddVec2(state->min, V2(default_frame_size.x + resize_handle_size,
                                 default_frame_size.y + resize_handle_size));
    }
    state->init = 1;
  }

  if (state->open) {
    UIBox *frame = BeginUIBox((UIProps){
        .layout = kUILayoutStack,
        .color = ColorU32FromHex(0x000000),
        .border = UIBorderFromBorderSide((UIBorderSide){
            .color = ColorU32FromHex(0xA8A8A8),
            .width = 1,
        }),
        .margin = UIEdgeInsetsFromLTRB(state->min.x, state->min.y, 0, 0),
        .size = SubVec2(state->max, state->min),
        .main_axis_align = kUIMainAxisAlignEnd,
        .cross_axis_align = kUICrossAxisAlignEnd,
    });
    {
      SetUIBoxBlockMouseInput(frame);

      if (IsUIMouseButtonPressed(frame, kUIMouseButtonLeft)) {
        state->pressed_min = state->min;
        state->pressed_max = state->max;
      }
      Vec2 drag_delta;
      if (IsUIMouseButtonDragging(frame, kUIMouseButtonLeft, &drag_delta)) {
        Vec2 size = SubVec2(state->max, state->min);
        state->min = RoundVec2(AddVec2(state->pressed_min, drag_delta));
        state->max = AddVec2(state->min, size);
      }

      BeginUIBox((UIProps){
          .background_color = ColorU32FromHex(0xF0F0F0),
      });
      BeginUIColumn((UIColumnProps){0});
      {
        BeginUIBox((UIProps){
            .background_color = ColorU32FromHex(0xD1D1D1),
        });
        BeginUIRow((UIRowProps){0});
        {
          UIEdgeInsets padding = UIEdgeInsetsSymmetric(6, 3);
          BeginUIBox((UIProps){.padding = padding});
          DoUITextF("Debug");
          EndUIBox();

          BeginUIBox((UIProps){.flex = 1});
          EndUIBox();

          b32 clicked;
          BeginUIButton(
              (UIButtonProps){
                  .padding = &padding,
              },
              &clicked);
          DoUIText(STR8_LIT("X"));
          EndUIButton();

          if (clicked) {
            state->open = 0;
          }
        }
        EndUIRow();
        EndUIBox();

        BeginUIScrollable((UIScrollableProps){.scroll = &state->scroll});
        UIDebugLayerInternal(state);
        EndUIScrollable();
      }
      EndUIColumn();
      EndUIBox();

      UIBox *resize_handle = BeginUIButton(
          (UIButtonProps){
              .default_background_color = 1,
              .size = V2(resize_handle_size, resize_handle_size),
          },
          0);
      {
        if (IsUIMouseButtonPressed(resize_handle, kUIMouseButtonLeft)) {
          state->pressed_min = state->min;
          state->pressed_max = state->max;
        }
        Vec2 drag_delta;
        if (IsUIMouseButtonDragging(resize_handle, kUIMouseButtonLeft,
                                    &drag_delta)) {
          state->max = RoundVec2(AddVec2(state->pressed_max, drag_delta));
          state->max = MaxVec2(state->max, AddVec2(state->min, min_frame_size));
        }
      }
      EndUIButton();
    }
    EndUIBox();
  }
  EndUIBox();
  EndUILayer();
  return debug_layer;
}

void OpenUIDebugLayer(UIBox *box) {
  UIDebugLayerState *state = GetUIBoxStruct(box, UIDebugLayerState);
  state->open = 1;
}
