#include "src/ui_widgets.h"

#include "src/math.h"
#include "src/types.h"
#include "src/ui.h"

void BeginUIScrollable(UIProps props, UIScrollableState *state) {
  if (IsZeroUIKey(props.key)) {
    props.key = PushUIKeyF("Scrollable@%p", state);
  }
  props.main_axis = kAxis2X;
  props.scrollable = 1;
  Vec2 wheel_delta;
  b32 scrolling = IsUIMouseScrolling(props.key, &wheel_delta);
  BeginUIBoxWithTag("Scrollable", props);
  {
    UIKey scroll_area_key = PushUIKeyF("ScrollArea");
    state->scroll_area_size = GetUIComputed(scroll_area_key).size.y;
    BeginUIBoxWithTag("ScrollArea",
                      (UIProps){
                          .key = scroll_area_key,
                          .flex = 1,
                          .main_axis = kAxis2Y,
                          .size = V2(kUISizeUndefined, kUISizeInfinity),
                      });
    {
      UIKey content_key = PushUIKeyF("ScrollContent");
      UIComputed computed = GetUIComputed(content_key);
      state->head_size = V2(10, 0);

      f32 total_item_size = computed.size.y;
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

      BeginUIBoxWithTag("ScrollContent", (UIProps){
                                             .key = content_key,
                                             .margin = UIEdgeInsetsFromSTEB(
                                                 0, -state->scroll, 0, 0),
                                         });
      // ...
    }
  }
}

void EndUIScrollable(UIScrollableState *state) {
  {
    {
      // ...
      EndUIBoxWithExpectedTag("ScrollContent");
    }
    EndUIBoxWithExpectedTag("ScrollArea");

    if (state->scroll_max > 0) {
      UIKey scroll_bar_key = PushUIKeyF("ScrollBar");
      Vec2 mouse_pos = GetUIMouseRelPos(scroll_bar_key);
      if (IsUIMouseButtonDown(scroll_bar_key, kUIMouseButtonLeft)) {
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
      BeginUIColumn((UIProps){
          .key = scroll_bar_key,
          .clickable[kUIMouseButtonLeft] = 1,
      });
      {
        ColorU32 background_color =
            ColorU32FromSRGBNotPremultiplied(128, 128, 128, 255);

        BeginUIBox((UIProps){
            .size = V2(state->head_size.x, state->control_offset),
            .background_color = background_color,
        });
        EndUIBox();

        UIKey control_key = PushUIKeyF("Control");
        ColorU32 control_background_color =
            ColorU32FromSRGBNotPremultiplied(192, 192, 192, 255);
        if (IsUIMouseHovering(control_key)) {
          control_background_color =
              ColorU32FromSRGBNotPremultiplied(224, 224, 224, 255);
        }
        if (IsUIMouseButtonPressed(control_key, kUIMouseButtonLeft)) {
          state->control_offset_drag_start = state->control_offset;
        }
        b32 is_dragging = 0;
        Vec2 drag_delta;
        if (IsUIMouseButtonDragging(control_key, kUIMouseButtonLeft,
                                    &drag_delta)) {
          is_dragging = 1;
          f32 offset = state->control_offset_drag_start + drag_delta.y;
          state->scroll =
              ClampF32(offset / state->control_max * state->scroll_max, 0,
                       state->scroll_max);

          control_background_color =
              ColorU32FromSRGBNotPremultiplied(255, 255, 255, 255);
        }

        BeginUIBox((UIProps){
            .key = control_key,
            .size = V2(state->head_size.x, state->control_size),
            .background_color = background_color,
            .main_axis_align = kUIMainAxisAlignCenter,
            .hoverable = 1,
            .clickable[kUIMouseButtonLeft] = 1,
        });
        {
          BeginUIBox((UIProps){
              .size = V2(
                  is_dragging ? state->head_size.x : state->head_size.x * 0.8f,
                  state->control_size),
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
    }
  }
  EndUIBoxWithExpectedTag("Scrollable");
}

f32 GetUIScrollableScroll(UIScrollableState *state) {
  f32 result = state->scroll;
  return result;
}

void SetUIScrollableScroll(UIScrollableState *state, f32 scroll) {
  state->scroll = ClampF32(scroll, 0, state->scroll_max);
}

void UIDebugLayer(UIDebugLayerState *state) {
  f32 resize_handle_size = 16;
  Vec2 default_frame_size = V2(800, 600);
  Vec2 min_frame_size = V2(resize_handle_size * 2, resize_handle_size * 2);

  if (IsZeroVec2(SubVec2(state->max, state->min))) {
    state->max =
        AddVec2(state->min, V2(default_frame_size.x + resize_handle_size,
                               default_frame_size.y + resize_handle_size));
  }

  if (state->open) {
    BeginUILayer(
        (UILayerProps){
            .min = state->min,
            .max = state->max,
        },
        "Debug@%p", state);
    UIKey frame_key = PushUIKeyF("Frame");
    if (IsUIMouseButtonPressed(frame_key, kUIMouseButtonLeft)) {
      state->pressed_min = state->min;
      state->pressed_max = state->max;
    }
    Vec2 drag_delta;
    if (IsUIMouseButtonDragging(frame_key, kUIMouseButtonLeft, &drag_delta)) {
      Vec2 size = SubVec2(state->max, state->min);
      state->min = RoundVec2(AddVec2(state->pressed_min, drag_delta));
      state->max = AddVec2(state->min, size);
    }
    BeginUIBox((UIProps){
        .key = frame_key,
        .layout = kUILayoutStack,
        .color = ColorU32FromHex(0x000000),
        .border = UIBorderFromBorderSide((UIBorderSide){
            .color = ColorU32FromHex(0xA8A8A8),
            .width = 1,
        }),
        .main_axis_align = kUIMainAxisAlignEnd,
        .cross_axis_align = kUICrossAxisAlignEnd,
        .hoverable = 1,
        .clickable = {1},
        .scrollable = 1,
    });
    {
      BeginUIColumn((UIProps){
          .background_color = ColorU32FromHex(0xF0F0F0),
      });
      {
        BeginUIRow((UIProps){
            .background_color = ColorU32FromHex(0xD1D1D1),
        });
        {
          BeginUIBox((UIProps){
              .padding = UIEdgeInsetsSymmetric(6, 3),
              .text = PushUITextF("UI Debug"),
          });
          EndUIBox();

          BeginUIBox((UIProps){.flex = 1});
          EndUIBox();

          UIKey close_key = PushUIKeyF("Close");
          ColorU32 background_color = ColorU32Zero();
          if (IsUIMouseButtonDown(close_key, kUIMouseButtonLeft)) {
            background_color = ColorU32FromHex(0x2D69AE);
          } else if (IsUIMouseHovering(close_key)) {
            background_color =
                ColorU32FromSRGBNotPremultiplied(53, 119, 197, 255);
          }

          if (IsUIMouseButtonClicked(close_key, kUIMouseButtonLeft)) {
            state->open = 0;
          }

          BeginUIBox((UIProps){
              .key = close_key,
              .padding = UIEdgeInsetsSymmetric(6, 3),
              .background_color = background_color,
              .text = PushUITextF("X"),
              .hoverable = 1,
              .clickable[kUIMouseButtonLeft] = 1,
          });
          EndUIBox();
        }
        EndUIRow();

        BeginUIScrollable((UIProps){0}, &state->scrollable);
        {
        }
        EndUIScrollable(&state->scrollable);
      }
      EndUIColumn();

      UIKey resize_handle = PushUIKeyF("ResizeHandle");
      ColorU32 resize_handle_color;
      if (IsUIMouseButtonDown(resize_handle, kUIMouseButtonLeft)) {
        resize_handle_color = ColorU32FromHex(0x4B6F9E);
      } else if (IsUIMouseHovering(resize_handle)) {
        resize_handle_color = ColorU32FromHex(0x618FC5);
      } else {
        resize_handle_color = ColorU32FromHex(0xD1D1D1);
      }
      BeginUIBox((UIProps){
          .key = resize_handle,
          .size = V2(resize_handle_size, resize_handle_size),
          .background_color = resize_handle_color,
          .hoverable = 1,
          .clickable[kUIMouseButtonLeft] = 1,
      });
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
      EndUIBox();
    }
    EndUIBox();
    EndUILayer();
  }
}
