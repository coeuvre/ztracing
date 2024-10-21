#include "src/ztracing.h"

#include <stdarg.h>

#include "src/draw.h"
#include "src/math.h"
#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"
#include "src/ui.h"
#include "src/ui_widgets.h"

static void BuildUI(f32 dt, f32 frame_time) {
  UIBox *debug_layer = UIDebugLayer();

  BeginUILayer((UILayerProps){.key = STR8_LIT("Base")});
  BeginUIColumn((UIColumnProps){
      .color = ColorU32FromSRGBNotPremultiplied(0, 0, 0, 255),
      .background_color = ColorU32FromHex(0xF0F0F0),
  });
  {
    BeginUIRow((UIRowProps){
        .border =
            (UIBorder){
                .bottom =
                    (UIBorderSide){
                        .width = 1,
                        .color = ColorU32FromHex(0x999999),
                    },
            },
    });
    {
      {
        BeginUIButton((UIButtonProps){.default_background_color = 1}, 0);
        DoUIText(STR8_LIT("Load"));
        EndUIButton();
      }

      {
        b32 clicked;
        BeginUIButton((UIButtonProps){0}, &clicked);
        DoUIText(STR8_LIT("Debug"));
        EndUIButton();
        if (clicked) {
          OpenUIDebugLayer(debug_layer);
        }
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

    static f32 scroll_drag_started;
    static f32 scroll;

    BeginUIScrollable((UIScrollableProps){.scroll = &scroll});
    {
      f32 item_size = 20.0f;
      u32 item_count = 510;

      // u32 item_index = FloorF32(state->scroll / item_size);
      // f32 offset = item_index * item_size - state->scroll;
      // for (; item_index < item_count && offset < state->scroll_area_size;
      //      ++item_index, offset += item_size)
      UIBox *content = BeginUIColumn((UIColumnProps){0});
      {
        if (IsUIMouseButtonPressed(content, kUIMouseButtonLeft)) {
          scroll_drag_started = scroll;
        }
        Vec2 drag_delta;
        if (IsUIMouseButtonDragging(content, kUIMouseButtonLeft, &drag_delta)) {
          scroll = scroll_drag_started - drag_delta.y;
        }

        for (u32 item_index = 0; item_index < item_count; ++item_index) {
          BeginUIRow((UIRowProps){
              .size = V2(kUISizeUndefined, item_size),
          });
          EndUIRow();
        }
      }
      EndUIColumn();
    }
    EndUIScrollable();
  }
  EndUIColumn();
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
