#include "src/ztracing.h"

#include <stdarg.h>

#include "src/draw.h"
#include "src/math.h"
#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"
#include "src/ui.h"
#include "src/ui_widgets.h"

static b32 DoUIButton(Str8 label) {
  b32 result;
  UIBox *button =
      BeginUITag("Button", (UIProps){
                               .padding = UIEdgeInsetsSymmetric(6, 3),
                           });
  {
    if (IsUIMouseButtonClicked(button, kUIMouseButtonLeft)) {
      button->props.background_color = ColorU32FromHex(0x0000FF);
    } else if (IsUIMouseButtonDown(button, kUIMouseButtonLeft)) {
      button->props.background_color = ColorU32FromHex(0x00FF00);
    } else if (IsUIMouseHovering(button)) {
      button->props.background_color = ColorU32FromHex(0xFF0000);
    }
    result = IsUIMouseButtonClicked(button, kUIMouseButtonLeft);

    DoUIText(label);
  }
  EndUITag("Button");
  return result;
}

static void BuildUI(f32 dt, f32 frame_time) {
  UIBox *debug_layer = UIDebugLayer();

  BeginUILayer((UILayerProps){.key = STR8_LIT("Base")});
  BeginUIBox((UIProps){
      .color = ColorU32FromSRGBNotPremultiplied(255, 255, 255, 255),
  });
  BeginUIColumn((UIColumnProps){0});
  {
    BeginUIStack((UIStackProps){.background_color = ColorU32FromHex(0xE6573F)});
    BeginUIRow((UIRowProps){0});
    {
      DoUIButton(STR8_LIT("Load"));
      if (DoUIButton(STR8_LIT("Debug"))) {
        OpenUIDebugLayer(debug_layer);
      }

      BeginUIBox((UIProps){.flex = 1});
      EndUIBox();

      BeginUIBox((UIProps){
          .padding = UIEdgeInsetsSymmetric(6, 3),
      });
      DoUITextF("%.0f %.1fMB %.1fms", 1.0f / dt,
                (f32)((f64)GetAllocatedBytes() / 1024.0 / 1024.0),
                frame_time * 1000.0f);
      EndUIBox();
    }
    EndUIRow();

    BeginUIRow((UIRowProps){.main_axis_align = kUIMainAxisAlignCenter});
    DoUITextF("Some File");
    EndUIRow();
    EndUIStack();

    static f32 scroll_drag_started;

    UIBox *scrollable = BeginUIScrollable();
    {
      f32 item_size = 20.0f;
      u32 item_count = 510;

      // u32 item_index = FloorF32(state->scroll / item_size);
      // f32 offset = item_index * item_size - state->scroll;
      // for (; item_index < item_count && offset < state->scroll_area_size;
      //      ++item_index, offset += item_size)
      UIBox *content = BeginUIBox((UIProps){0});
      if (IsUIMouseButtonPressed(content, kUIMouseButtonLeft)) {
        scroll_drag_started = GetUIScrollableScroll(scrollable);
      }
      Vec2 drag_delta;
      if (IsUIMouseButtonDragging(content, kUIMouseButtonLeft, &drag_delta)) {
        SetUIScrollableScroll(scrollable, scroll_drag_started - drag_delta.y);
      }

      BeginUIColumn((UIColumnProps){0});
      {
        for (u32 item_index = 0; item_index < item_count; ++item_index) {
          BeginUIRow((UIRowProps){
              .size = V2(kUISizeUndefined, item_size),
              .background_color =
                  ColorU32FromSRGBNotPremultiplied(0, 0, item_index % 256, 255),
          });
          EndUIRow();
        }
      }
      EndUIColumn();
      EndUIBox();
    }
    EndUIScrollable();
  }
  EndUIColumn();
  EndUIBox();
  EndUILayer();
}

void DoFrame(void) {
  static u64 last_counter;
  static f32 last_frame_time;

  f32 dt = 0.0f;
  u64 current_counter = GetPerformanceCounter();
  if (last_counter) {
    dt = (f32)((f64)(current_counter - last_counter) /
               (f64)GetPerformanceFrequency());
  }
  last_counter = current_counter;

  ClearDraw();

  SetUIDeltaTime(dt);
  SetUICanvasSize(GetScreenSize());
  BeginUIFrame();
  BuildUI(dt, last_frame_time);
  EndUIFrame();
  RenderUI();

  last_frame_time = (f32)((f64)(GetPerformanceCounter() - last_counter) /
                          (f64)GetPerformanceFrequency());

  PresentDraw();
}
