#include "src/ztracing.h"

#include <stdarg.h>

#include "src/draw.h"
#include "src/math.h"
#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"
#include "src/ui.h"
#include "src/ui_widgets.h"

static void UIButton(Str8 label) {
  UIKey key = PushUIKey(label);
  ColorU32 color = ColorU32Zero();
  if (IsUIMouseButtonClicked(key, kUIMouseButtonLeft)) {
    color = ColorU32FromHex(0x0000FF);
  } else if (IsUIMouseButtonDown(key, kUIMouseButtonLeft)) {
    color = ColorU32FromHex(0x00FF00);
  } else if (IsUIMouseHovering(key)) {
    color = ColorU32FromHex(0xFF0000);
  } else {
    // color = ColorU32FromHex(0x5EAC57);
  }
  BeginUIBox((UIProps){
      .key = key,
      .padding = UIEdgeInsetsSymmetric(6, 4),
      .hoverable = 1,
      .clickable[kUIMouseButtonLeft] = 1,
      .color = color,
  });
  {
    BeginUIBox((UIProps){
        .text = PushUIText(label),
    });
    EndUIBox();
  }
  EndUIBox();
}

static void UITextF(UIProps props, const char *fmt, ...) {
  props.padding = UIEdgeInsetsSymmetric(6, 4);
  va_list ap;
  va_start(ap, fmt);
  BeginUIBox(props);
  {
    BeginUIBox((UIProps){.text = PushUITextFV(fmt, ap)});
    EndUIBox();
  }
  EndUIBox();
  va_end(ap);
}

static void BuildUI(f32 dt, f32 frame_time) {
  TempMemory scratch = BeginScratch(0, 0);

  BeginUILayer();
  BeginUIColumn((UIProps){0});
  {
    BeginUIRow((UIProps){.color = ColorU32FromHex(0xE6573F)});
    {
      UIButton(STR8_LIT("Load"));
      UIButton(STR8_LIT("About"));

      BeginUIBox((UIProps){.flex = 1});
      EndUIBox();

      UITextF((UIProps){0}, "%.0f %.1fms", 1.0f / dt, frame_time * 1000.0f);
    }
    EndUIRow();

    static UIScrollableState state;
    static f32 scroll_drag_started;

    BeginUIScrollable((UIProps){.flex = 1}, &state);
    {
      f32 item_size = 20.0f;
      u32 item_count = 510;

      // u32 item_index = FloorF32(state->scroll / item_size);
      // f32 offset = item_index * item_size - state->scroll;
      // for (; item_index < item_count && offset < state->scroll_area_size;
      //      ++item_index, offset += item_size)
      UIKey key = PushUIKeyF("Content");
      if (IsUIMouseButtonPressed(key, kUIMouseButtonLeft)) {
        scroll_drag_started = GetUIScrollableScroll(&state);
      }
      Vec2 drag_delta;
      if (IsUIMouseButtonDragging(key, kUIMouseButtonLeft, &drag_delta)) {
        SetUIScrollableScroll(&state, scroll_drag_started - drag_delta.y);
      }
      BeginUIColumn((UIProps){.key = key, .clickable = 1});
      for (u32 item_index = 0; item_index < item_count; ++item_index) {
        BeginUIRow((UIProps){.size = V2(kUISizeUndefined, item_size),
                             .color = ColorU32RGBANotPremultiplied(
                                 0, 0, item_index % 256, 255)});
        EndUIRow();
      }
      EndUIColumn();
    }
    EndUIScrollable(&state);

    BeginUILayer();
    {
      BeginUIBox((UIProps){0});
      BeginUIBox((UIProps){
          .margin = UIEdgeInsetsFromSTEB(100, 100, 0, 0),
          .size = V2(100, 100),
          .color = ColorU32RGBANotPremultiplied(255, 0, 0, 255),
      });
      EndUIBox();
      EndUIBox();
    }
    EndUILayer();
  }
  EndUIColumn();
  EndUILayer();

  static b32 enable_this_layer = 1;
  if (enable_this_layer) {
    BeginUILayer();
    {
      UIKey root_key = PushUIKeyF("Root");
      BeginUIBox((UIProps){
          .key = root_key,
          .hoverable = 1,
          .clickable = {1},
          .scrollable = 1,
          .color = ColorU32RGBANotPremultiplied(0, 0, 0, 32),
      });
      {
        UIKey container_key = PushUIKeyF("Container");
        if (IsUIMouseButtonClicked(container_key, kUIMouseButtonLeft)) {
          enable_this_layer = 0;
        }
        BeginUIBox((UIProps){
            .key = container_key,
            .margin = UIEdgeInsetsFromSTEB(500, 200, 0, 0),
            .size = V2(100, 100),
            .color = ColorU32RGBANotPremultiplied(0, 255, 0, 255),
            .clickable[kUIMouseButtonLeft] = 1,
        });
        EndUIBox();
      }
      EndUIBox();
    }
    EndUILayer();
  }

  EndScratch(scratch);
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
  BeginUIFrame(GetScreenSize());
  BuildUI(dt, last_frame_time);
  EndUIFrame();
  RenderUI();

  last_frame_time = (f32)((f64)(GetPerformanceCounter() - last_counter) /
                          (f64)GetPerformanceFrequency());

  PresentDraw();
}
