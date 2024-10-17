#include "src/ui_widgets.h"

#include <inttypes.h>
#include <stdarg.h>
#include <string.h>

#include "src/math.h"
#include "src/string.h"
#include "src/types.h"
#include "src/ui.h"

UIKey BeginUIRow(UIRowProps props) {
  return BeginUITag(
      "Row",
      (UIProps){
          .key = props.key,
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

UIKey BeginUIColumn(UIColumnProps props) {
  return BeginUITag(
      "Column",
      (UIProps){
          .key = props.key,
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

UIKey BeginUIStack(UIStackProps props) {
  return BeginUITag("Stack", (UIProps){
                                 .key = props.key,
                                 .layout = kUILayoutStack,
                             });
}

void UITextF(UIProps props, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  props.text = PushUIStr8FV(fmt, ap);
  va_end(ap);

  BeginUITag("Text", props);
  EndUITag("Text");
}

void UIText(UIProps props, Str8 text) {
  props.text = PushUIStr8(text);
  BeginUITag("Text", props);
  EndUITag("Text");
}

typedef struct UIScrollableState {
  // persistent info
  f32 scroll;
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

UIKey BeginUIScrollable(void) {
  UIKey scrollable = BeginUITag("Scrollable", (UIProps){
                                                  .main_axis = kAxis2X,
                                                  .scrollable = 1,
                                              });
  {
    UIScrollableState *state = PushUIBoxStruct(scrollable, UIScrollableState);

    Vec2 wheel_delta;
    b32 scrolling = IsUIMouseScrolling(scrollable, &wheel_delta);

    UIKey scroll_area = BeginUITag(
        "ScrollArea", (UIProps){
                          .flex = 1,
                          .main_axis = kAxis2Y,
                          .size = V2(kUISizeUndefined, kUISizeInfinity),
                      });
    {
      state->scroll_area_size = GetUIComputed(scroll_area).size.y;

      UIKey scroll_content = BeginUITag(
          "ScrollContent",
          (UIProps){
              .margin = UIEdgeInsetsFromLTRB(0, -state->scroll, 0, 0),
          });
      f32 total_item_size = GetUIComputed(scroll_content).size.y;

      state->head_size = V2(10, 0);
      state->scroll_max = MaxF32(total_item_size - state->scroll_area_size, 0);
      state->scroll = ClampF32(state->scroll, 0, state->scroll_max);

      f32 min_control_size = 4;
      f32 free_size =
          MaxF32(state->scroll_area_size - 2 * state->head_size.y, 0.0f);
      state->control_size =
          MinF32(MaxF32(state->scroll_area_size / total_item_size * free_size,
                        min_control_size),
                 free_size);

      state->scroll_step = 0.2f * state->scroll_area_size / GetUIDeltaTime();

      state->control_max = free_size - state->control_size;
      state->control_offset =
          (state->scroll / state->scroll_max) * state->control_max;
      if (scrolling) {
        state->scroll =
            ClampF32(state->scroll +
                         wheel_delta.y * state->scroll_step * GetUIDeltaTime(),
                     0, state->scroll_max);
      }

      // ...
    }
  }
  return scrollable;
}

void EndUIScrollable(void) {
  {
    {
      // ...
      EndUITag("ScrollContent");
    }
    EndUITag("ScrollArea");

    UIScrollableState *state =
        GetUIBoxStruct(GetCurrentUIBoxKey(), UIScrollableState);
    if (state->scroll_max > 0) {
      UIKey scroll_bar = BeginUIBox((UIProps){
          .clickable[kUIMouseButtonLeft] = 1,
      });
      BeginUIColumn((UIColumnProps){0});
      {
        Vec2 mouse_pos = GetUIMouseRelPos(scroll_bar);
        if (IsUIMouseButtonDown(scroll_bar, kUIMouseButtonLeft)) {
          if (ContainsF32(mouse_pos.x, 0, state->head_size.x)) {
            f32 offset = mouse_pos.y - state->head_size.y;
            if (offset < state->control_offset) {
              state->scroll = MaxF32(
                  state->scroll - state->scroll_step * GetUIDeltaTime(), 0);
            } else if (offset > state->control_offset + state->control_size) {
              state->scroll =
                  MinF32(state->scroll + state->scroll_step * GetUIDeltaTime(),
                         state->scroll_max);
            }
          }
        }
        ColorU32 background_color =
            ColorU32FromSRGBNotPremultiplied(128, 128, 128, 255);

        BeginUIBox((UIProps){
            .size = V2(state->head_size.x, state->control_offset),
            .background_color = background_color,
        });
        EndUIBox();

        UIKey scroll_control = BeginUIBox((UIProps){
            .hoverable = 1,
            .clickable[kUIMouseButtonLeft] = 1,
        });
        {
          ColorU32 control_background_color =
              ColorU32FromSRGBNotPremultiplied(192, 192, 192, 255);
          if (IsUIMouseHovering(scroll_control)) {
            control_background_color =
                ColorU32FromSRGBNotPremultiplied(224, 224, 224, 255);
          }
          if (IsUIMouseButtonPressed(scroll_control, kUIMouseButtonLeft)) {
            state->control_offset_drag_start = state->control_offset;
          }
          Vec2 drag_delta;
          if (IsUIMouseButtonDragging(scroll_control, kUIMouseButtonLeft,
                                      &drag_delta)) {
            f32 offset = state->control_offset_drag_start + drag_delta.y;
            state->scroll =
                ClampF32(offset / state->control_max * state->scroll_max, 0,
                         state->scroll_max);

            control_background_color =
                ColorU32FromSRGBNotPremultiplied(255, 255, 255, 255);
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
      EndUIBox();
    }
  }
  EndUITag("Scrollable");
}

f32 GetUIScrollableScroll(UIKey key) {
  UIScrollableState *state = GetUIBoxStruct(key, UIScrollableState);
  f32 result = state->scroll;
  return result;
}

void SetUIScrollableScroll(UIKey key, f32 scroll) {
  UIScrollableState *state = GetUIBoxStruct(key, UIScrollableState);
  state->scroll = ClampF32(scroll, 0, state->scroll_max);
}

typedef struct UIDebugLayerState {
  b8 open;
  Vec2 min;
  Vec2 max;
  Vec2 pressed_min;
  Vec2 pressed_max;
} UIDebugLayerState;

static void UIDebugLayerBoxR(UIDebugLayerState *state, UIBox *box, u32 level) {
  UIKey hoverable = BeginUIBox((UIProps){
      .hoverable = 1,
  });
  ColorU32 background_color = ColorU32Zero();
  if (IsUIMouseHovering(hoverable)) {
    background_color = ColorU32FromSRGBNotPremultiplied(53, 119, 197, 255);

    Rect2 hovered_clip_rect = box->computed.clip_rect;
    BeginUILayer((UILayerProps){
        .key = STR8_LIT("__UIDebug__Overlay"),
        .z_index = kUIDebugLayerZIndex - 1,
    });
    BeginUIBox((UIProps){0});
    BeginUIBox((UIProps){
        .margin = UIEdgeInsetsFromLTRB(hovered_clip_rect.min.x,
                                       hovered_clip_rect.min.y, 0, 0),
        .size = SubVec2(hovered_clip_rect.max, hovered_clip_rect.min),
        .background_color = ColorU32FromSRGBNotPremultiplied(255, 0, 255, 64),
    });
    EndUIBox();
    EndUIBox();
    EndUILayer();
  }
  BeginUIRow((UIRowProps){0});
  {
    BeginUIBox((UIProps){
        .background_color = background_color,
        .padding = UIEdgeInsetsFromLTRB(level * 20, 0, 0, 0),
        .main_axis_size = kUIMainAxisSizeMax,
    });
    Str8 seq_str = PushUIStr8F("%u", box->seq);
    UITextF((UIProps){0}, "%s%s%s", box->tag, "#",
            IsEmptyStr8(box->props.key) ? seq_str.ptr : box->props.key.ptr);
    EndUIBox();
  }
  EndUIRow();
  EndUIBox();

  for (UIBox *child = box->first; child; child = child->next) {
    UIDebugLayerBoxR(state, child, level + 1);
  }
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
    UITextF((UIProps){0}, "Boxes: %" PRIu64 " / %" PRIu64,
            frame->cache.total_box_count, frame->cache.box_hash_slots_count);
    EndUIRow();

    EndUIColumn();
  }
  EndUIBox();

  for (UILayer *layer = frame->last_layer; layer; layer = layer->prev) {
    if (strstr((char *)layer->props.key.ptr, "__UIDebug__") == 0) {
      BeginUIRow((UIRowProps){0});
      UITextF((UIProps){0}, "Layer - %s", layer->props.key.ptr);
      EndUIRow();
      if (layer->root) {
        UIDebugLayerBoxR(state, layer->root, 1);
      }
    }
  }
  EndUIColumn();
  EndUIBox();
}

UIKey UIDebugLayer(void) {
  f32 resize_handle_size = 16;
  Vec2 default_frame_size = V2(400, 500);
  Vec2 min_frame_size = V2(resize_handle_size * 2, resize_handle_size * 2);

  BeginUILayer((UILayerProps){
      .key = STR8_LIT("__UIDebug__"),
      .z_index = kUIDebugLayerZIndex,
  });
  UIKey debug_layer = BeginUIBox((UIProps){0});
  UIDebugLayerState *state = PushUIBoxStruct(debug_layer, UIDebugLayerState);
  if (IsZeroVec2(SubVec2(state->max, state->min))) {
    state->max =
        AddVec2(state->min, V2(default_frame_size.x + resize_handle_size,
                               default_frame_size.y + resize_handle_size));
  }
  if (state->open) {
    UIKey frame = BeginUIBox((UIProps){
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
        .hoverable = 1,
        .clickable = {1},
        .scrollable = 1,
    });
    {
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
          UITextF((UIProps){.padding = UIEdgeInsetsSymmetric(6, 3)}, "Debug");

          BeginUIBox((UIProps){.flex = 1});
          EndUIBox();

          UIKey close = BeginUIBox((UIProps){
              .hoverable = 1,
              .clickable[kUIMouseButtonLeft] = 1,
          });
          {
            ColorU32 background_color = ColorU32Zero();
            if (IsUIMouseButtonDown(close, kUIMouseButtonLeft)) {
              background_color = ColorU32FromHex(0x2D69AE);
            } else if (IsUIMouseHovering(close)) {
              background_color =
                  ColorU32FromSRGBNotPremultiplied(53, 119, 197, 255);
            }

            if (IsUIMouseButtonClicked(close, kUIMouseButtonLeft)) {
              state->open = 0;
            }

            BeginUIBox((UIProps){
                .padding = UIEdgeInsetsSymmetric(6, 3),
                .background_color = background_color,
                .text = PushUIStr8F("X"),
            });
            EndUIBox();
          }
          EndUIBox();
        }
        EndUIRow();
        EndUIBox();

        BeginUIScrollable();
        UIDebugLayerInternal(state);
        EndUIScrollable();
      }
      EndUIColumn();
      EndUIBox();

      UIKey resize_handle = BeginUIBox((UIProps){
          .hoverable = 1,
          .clickable[kUIMouseButtonLeft] = 1,
      });
      {
        ColorU32 resize_handle_color;
        if (IsUIMouseButtonDown(resize_handle, kUIMouseButtonLeft)) {
          resize_handle_color = ColorU32FromHex(0x4B6F9E);
        } else if (IsUIMouseHovering(resize_handle)) {
          resize_handle_color = ColorU32FromHex(0x618FC5);
        } else {
          resize_handle_color = ColorU32FromHex(0xD1D1D1);
        }
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

        BeginUIBox((UIProps){
            .size = V2(resize_handle_size, resize_handle_size),
            .background_color = resize_handle_color,
        });
        EndUIBox();
      }
      EndUIBox();
    }
    EndUIBox();
  }
  EndUIBox();
  EndUILayer();
  return debug_layer;
}

void OpenUIDebugLayer(UIKey key) {
  UIDebugLayerState *state = GetUIBoxStruct(key, UIDebugLayerState);
  state->open = 1;
}
