#include "src/ztracing.h"

#include "src/draw.h"
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

    BeginUIBox();
    {
      SetUIColor(ColorU32FromHex(0x5EAC57));
      SetUIText(STR8_LIT("Main"));
    }
    EndUIBox();
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
