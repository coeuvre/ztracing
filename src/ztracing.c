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

static void UITextF(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  BeginUIBox((UIProps){.padding = UIEdgeInsetsSymmetric(6, 4)});
  {
    BeginUIBox((UIProps){.text = PushUITextFV(fmt, ap)});
    EndUIBox();
  }
  EndUIBox();
  va_end(ap);
}

static void BuildUI(f32 dt, f32 frame_time) {
  TempMemory scratch = BeginScratch(0, 0);

  BeginUIColumn((UIProps){0});
  {
    BeginUIRow((UIProps){.color = ColorU32FromHex(0xE6573F)});
    {
      UIButton(STR8_LIT("Load"));
      UIButton(STR8_LIT("About"));

      BeginUIBox((UIProps){.flex = 1});
      EndUIBox();

      UITextF("%.0f %.1fms", 1.0f / dt, frame_time * 1000.0f);
    }
    EndUIRow();

    static UIScrollableState state;

    BeginUIScrollable((UIProps){.flex = 1}, &state);
    {
      f32 item_size = 20.0f;
      u32 item_count = 510;

      // u32 item_index = FloorF32(state->scroll / item_size);
      // f32 offset = item_index * item_size - state->scroll;
      // for (; item_index < item_count && offset < state->scroll_area_size;
      //      ++item_index, offset += item_size) {
      BeginUIColumn((UIProps){0});
      for (u32 item_index = 0; item_index < item_count; ++item_index) {
        BeginUIRow(
            (UIProps){.size = V2(kUISizeUndefined, item_size),
                      .color = ColorU32FromRGBA(0, 0, item_index % 256, 255)});
        EndUIRow();
      }
      EndUIColumn();
    }
    EndUIScrollable(&state);
  }
  EndUIColumn();

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
  BeginUIFrame(GetScreenSize(), GetScreenContentScale());
  BuildUI(dt, last_frame_time);
  EndUIFrame();
  RenderUI();

  last_frame_time = (f32)((f64)(GetPerformanceCounter() - last_counter) /
                          (f64)GetPerformanceFrequency());

  PresentDraw();
}
