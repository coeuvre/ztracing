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
        ColorU32 background_color = ColorU32FromRGBA(128, 128, 128, 255);

        BeginUIBox((UIProps){
            .size = V2(state->head_size.x, state->control_offset),
            .color = background_color,
        });
        EndUIBox();

        UIKey control_key = PushUIKeyF("Control");
        ColorU32 control_background_color =
            ColorU32FromRGBA(192, 192, 192, 255);
        if (IsUIMouseHovering(control_key)) {
          control_background_color = ColorU32FromRGBA(224, 224, 224, 255);
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

          control_background_color = ColorU32FromRGBA(255, 255, 255, 255);
        }

        BeginUIBox((UIProps){
            .key = control_key,
            .size = V2(state->head_size.x, state->control_size),
            .color = background_color,
            .main_axis_align = kUIMainAxisAlignCenter,
            .hoverable = 1,
            .clickable[kUIMouseButtonLeft] = 1,
        });
        {
          BeginUIBox((UIProps){
              .size = V2(
                  is_dragging ? state->head_size.x : state->head_size.x * 0.8f,
                  state->control_size),
              .color = control_background_color,
          });
          EndUIBox();
        }
        EndUIBox();

        BeginUIBox((UIProps){
            .size = V2(state->head_size.x, kUISizeUndefined),
            .flex = 1,
            .color = background_color,
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
