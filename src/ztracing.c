#include "src/ztracing.h"

#include "src/draw.h"
#include "src/log.h"
#include "src/math.h"
#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"
#include "src/ui.h"
#include "src/ui_widgets.h"

static void UIButton(Str8 label) {
  SetNextUIKey(label);
  BeginUIBox();
  {
    SetUIPadding(UIEdgeInsetsSymmetric(6, 4));

    UISignal signal = SetUISignal(kUISignalMouse);
    if (signal.hovering) {
      SetUIColor(ColorU32FromHex(0xFF0000));
    } else if (signal.pressed) {
      SetUIColor(ColorU32FromHex(0x00FF00));
    } else {
      SetUIColor(ColorU32FromHex(0x5EAC57));
    }

    BeginUIBox();
    { SetUIText(label); }
    EndUIBox();
  }
  EndUIBox();
}

static void UIText(Str8 text) {
  BeginUIBox();
  {
    SetUIPadding(UIEdgeInsetsSymmetric(6, 4));

    BeginUIBox();
    { SetUIText(text); }
    EndUIBox();
  }
  EndUIBox();
}

static void BuildUI(f32 dt) {
  TempMemory scratch = BeginScratch(0, 0);

  BeginUIColumn();
  {
    BeginUIRow();
    {
      SetUIColor(ColorU32FromHex(0xE6573F));

      UIButton(STR8_LIT("Load"));
      UIButton(STR8_LIT("About"));

      BeginUIBox();
      SetUIFlex(1.0f);
      EndUIBox();

      UIText(PushStr8F(scratch.arena, "%.0f", 1.0f / dt));
    }
    EndUIRow();

    BeginUIRow();
    {
      f32 scroll = 12345.0f;

      f32 item_size = 20.0f;
      u32 item_count = 10000;
      f32 total_size = item_size * item_count;

      SetNextUIKey(STR8_LIT("#ScrollArea"));
      BeginUIColumn();
      {
        SetUIFlex(1.0);
        // SetUIColor(ColorU32FromHex(0x5EAC57));

        Vec2 size = GetUIComputed().size;

        u32 item_index = FloorF32(scroll / item_size);
        f32 offset = item_index * item_size - scroll;
        for (; item_index < item_count && offset < size.y;
             ++item_index, offset += item_size) {
          BeginUIRow();
          SetUISize(V2(kUISizeUndefined, item_size));
          SetUIColor(ColorU32FromRGBA(0, 0, item_index % 256, 255));
          EndUIRow();
        }
      }
      EndUIColumn();

      SetNextUIKey(STR8_LIT("#ScrollBar"));
      BeginUIColumn();
      {
        Vec2 head_size = V2(16, 16);
        f32 min_control_size = 4;
        Vec2 size = GetUIComputed().size;
        f32 free_size = size.y - 2 * head_size.y;
        Vec2 control_size =
            V2(head_size.x,
               MinF32(MaxF32(size.y / total_size * free_size, min_control_size),
                      free_size));
        f32 scroll_max = total_size - size.y;
        f32 control_offset =
            (scroll / scroll_max) * (free_size - control_size.y);

        BeginUIBox();
        SetUISize(head_size);
        SetUIColor(ColorU32FromRGBA(255, 0, 0, 255));
        EndUIBox();

        BeginUIBox();
        SetUISize(V2(head_size.x, control_offset));
        EndUIBox();

        BeginUIBox();
        SetUISize(control_size);
        SetUIColor(ColorU32FromRGBA(0, 255, 0, 255));
        EndUIBox();

        BeginUIBox();
        SetUIFlex(1.0);
        EndUIBox();

        BeginUIBox();
        SetUISize(head_size);
        SetUIColor(ColorU32FromRGBA(255, 0, 0, 255));
        EndUIBox();
      }
      EndUIColumn();
    }
    EndUIRow();
  }
  EndUIColumn();

  EndScratch(scratch);
}

void DoFrame(f32 dt) {
  ClearDraw();

  BeginUIFrame(GetScreenSize(), GetScreenContentScale());
  BuildUI(dt);
  EndUIFrame();
  RenderUI();

  PresentDraw();
}
