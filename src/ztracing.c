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
      SetNextUIKey(STR8_LIT("Scroll"));
      BeginUIColumn();
      {
        SetUIFlex(1.0);
        // SetUIColor(ColorU32FromHex(0x5EAC57));

        Vec2 size = GetUIComputed().size;
        INFO("%d, %d", (int)size.x, (int)size.y);

        f32 kItemHeight = 20.0f;
        u32 item_count = 10000;
        f32 scroll = 0.0f;

        u32 item_index = FloorF32(scroll / kItemHeight);
        f32 offset = item_index * kItemHeight - scroll;
        for (; item_index < item_count && offset < size.y;
             ++item_index, offset += kItemHeight) {
          INFO("Build item %u", item_index);
          BeginUIRow();
          SetUISize(V2(kUISizeUndefined, kItemHeight));
          SetUIColor(ColorU32FromRGBA(0, 0, item_index % 256, 255));
          EndUIRow();
        }
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
