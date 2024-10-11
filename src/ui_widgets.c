#include "src/ui_widgets.h"

#include "src/math.h"
#include "src/types.h"
#include "src/ui.h"

void BeginUIScrollable(UIScrollableState *state) {
  SetNextUITag("Scrollable");
  SetNextUIMainAxis(kAxis2X);
  SetNextUIKeyF("Scrollable@%p", state);
  Vec2 wheel_delta;
  b32 scrolling = IsNextUIMouseScrolling(&wheel_delta);
  BeginUIBox();
  {
    SetNextUIFlex(1.0);
    SetNextUIKeyF("Content");
    UIComputed computed = GetNextUIComputed();
    state->head_size = V2(16, 16);

    f32 total_item_size = 0;  // computed.content_size.y;
    state->scroll_area_size = computed.size.y;
    state->scroll_max = MaxF32(total_item_size - state->scroll_area_size, 0);
    state->scroll = ClampF32(state->scroll, 0, state->scroll_max);

    f32 min_control_size = 4;
    f32 free_size = MaxF32(computed.size.y - 2 * state->head_size.y, 0.0f);
    state->control_size =
        MinF32(MaxF32(state->scroll_area_size / total_item_size * free_size,
                      min_control_size),
               free_size);

    state->scroll_step = state->scroll_area_size;

    state->control_max = free_size - state->control_size;
    state->control_offset =
        (state->scroll / state->scroll_max) * state->control_max;
    if (scrolling) {
      state->scroll = ClampF32(
          state->scroll + wheel_delta.y * state->scroll_step * GetUIDeltaTime(),
          0, state->scroll_max);
    }
    BeginUIColumn();
  }
}

void EndUIScrollable(UIScrollableState *state) {
  {
    EndUIColumn();

    if (state->scroll_max > 0) {
      SetNextUIKeyF("ScrollBar");
      Vec2 mouse_pos = GetNextUIMouseRelPos();
      if (IsNextUIMouseButtonDown(kUIMouseButtonLeft)) {
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
      BeginUIColumn();
      {
        SetNextUIKeyF("Head");
        if (IsNextUIMouseButtonDown(kUIMouseButtonLeft)) {
          state->scroll =
              ClampF32(state->scroll - state->scroll_step * GetUIDeltaTime(), 0,
                       state->scroll_max);
        }
        SetNextUISize(state->head_size);
        SetNextUIColor(ColorU32FromRGBA(255, 0, 0, 255));
        BeginUIBox();
        EndUIBox();

        ColorU32 background_color = ColorU32FromRGBA(128, 128, 128, 255);

        SetNextUISize(V2(state->head_size.x, state->control_offset));
        SetNextUIColor(background_color);
        BeginUIBox();
        EndUIBox();

        SetNextUIKeyF("Control");
        SetNextUISize(V2(state->head_size.x, state->control_size));
        SetNextUIColor(background_color);
        SetNextUIMainAxisAlign(kUIMainAxisAlignCenter);
        ColorU32 control_background_color =
            ColorU32FromRGBA(192, 192, 192, 255);
        if (IsNextUIMouseHovering()) {
          control_background_color = ColorU32FromRGBA(224, 224, 224, 255);
        }
        if (IsNextUIMouseButtonPressed(kUIMouseButtonLeft)) {
          state->control_offset_drag_start = state->control_offset;
        }
        Vec2 drag_delta;
        if (IsNextUIMouseButtonDragging(kUIMouseButtonLeft, &drag_delta)) {
          f32 offset = state->control_offset_drag_start + drag_delta.y;
          state->scroll =
              ClampF32(offset / state->control_max * state->scroll_max, 0,
                       state->scroll_max);

          control_background_color = ColorU32FromRGBA(255, 255, 255, 255);
        }
        BeginUIBox();
        {
          SetNextUISize(V2(state->head_size.x * 0.8f, state->control_size));
          SetNextUIColor(control_background_color);
          BeginUIBox();
          EndUIBox();
        }
        EndUIBox();

        SetNextUISize(V2(state->head_size.x, kUISizeUndefined));
        SetNextUIFlex(1);
        SetNextUIColor(background_color);
        BeginUIBox();
        EndUIBox();

        SetNextUIKeyF("Tail");
        SetNextUISize(state->head_size);
        SetNextUIColor(ColorU32FromRGBA(255, 0, 0, 255));
        if (IsNextUIMouseButtonDown(kUIMouseButtonLeft)) {
          state->scroll =
              ClampF32(state->scroll + state->scroll_step * GetUIDeltaTime(), 0,
                       state->scroll_max);
        }
        BeginUIBox();
        EndUIBox();
      }
      EndUIColumn();
    }
  }
  EndUIBoxWithExpectedTag("Scrollable");
}
