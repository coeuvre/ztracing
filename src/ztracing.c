#include "src/ztracing.h"

#include "src/draw.h"
#include "src/math.h"
#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"
#include "src/ui.h"
#include "src/ui_widgets.h"

static void BuildUI(f32 dt) {
  TempMemory scratch = BeginScratch(0, 0);

  BeginUIColumn(STR8_LIT("Root"));
  {
    BeginUIRow(STR8_LIT("Menu"));
    {
      SetUIColor(ColorU32FromHex(0xE6573F));

      BeginUIBox(STR8_LIT("Load"));
      {
        SetUIColor(ColorU32FromHex(0x5EAC57));

        BeginUIBox(STR8_LIT("Label"));
        SetUIText(STR8_LIT("Load"));
        EndUIBox();
      }
      EndUIBox();

      BeginUIBox(STR8_LIT("About"));
      {
        SetUIColor(ColorU32FromHex(0x5EAC57));

        BeginUIBox(STR8_LIT("Label"));
        SetUIText(STR8_LIT("About"));
        EndUIBox();
      }
      EndUIBox();

      BeginUIBox(STR8_LIT("Space"));
      SetUIFlex(1.0f);
      EndUIBox();

      BeginUIBox(STR8_LIT("FPS"));
      {
        SetUIColor(ColorU32FromHex(0x5EAC57));

        BeginUIBox(STR8_LIT("Label"));
        SetUIText(PushStr8F(scratch.arena, "%.0f", 1.0f / dt));
        EndUIBox();
      }
      EndUIBox();
    }
    EndUIRow();

    BeginUIBox(STR8_LIT("Main"));
    SetUIColor(ColorU32FromHex(0x5EAC57));
    SetUIText(STR8_LIT("Main"));
    EndUIBox();
  }
  EndUIColumn();

  EndScratch(scratch);
}

void DoFrame(f32 dt) {
  ClearDraw();

  BeginUIFrame(Vec2FromVec2I(GetScreenSizeInPixel()), GetScreenContentScale());
  BuildUI(dt);
  EndUIFrame();
  RenderUI();

  PresentDraw();
}
