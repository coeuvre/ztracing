#include "src/ztracing.h"

#include "src/draw.h"
#include "src/math.h"
#include "src/string.h"
#include "src/ui.h"
#include "src/ui_widgets.h"

static void BuildUI(void) {
  BeginUIColumn(STR8_LIT("Root"));
  {
    BeginUIRow(STR8_LIT("Menu"));
    SetUISize(V2(kUISizeUndefined, 21.0f));
    SetUIColor(ColorU32FromHex(0x5EAC57));
    {
      BeginUIRow(STR8_LIT("Left"));
      SetUICrossAxisAlign(kUICrossAxisAlignStretch);
      {
        BeginUIBox(STR8_LIT("Load"));
        SetUIColor(ColorU32FromHex(0xE6573F));
        SetUIText(STR8_LIT("Load"));
        EndUIBox();

        BeginUIBox(STR8_LIT("About"));
        SetUIColor(ColorU32FromHex(0xE6573F));
        SetUIText(STR8_LIT("About"));
        EndUIBox();
      }
      EndUIRow();

      BeginUIBox(STR8_LIT("Space"));
      SetUIFlex(1.0f);
      EndUIBox();

      BeginUIRow(STR8_LIT("Right"));
      SetUICrossAxisAlign(kUICrossAxisAlignStretch);
      {
        BeginUIBox(STR8_LIT("Right"));
        SetUIText(STR8_LIT("FPS"));
        EndUIBox();
      }
      EndUIRow();
    }
    EndUIRow();

    BeginUIBox(STR8_LIT("Main"));
    SetUIText(STR8_LIT("Main"));
    EndUIBox();
  }
  EndUIColumn();
}

void DoFrame(void) {
  ClearDraw();

  BeginUIFrame(Vec2FromVec2I(GetScreenSizeInPixel()), GetScreenContentScale());
  BuildUI();
  EndUIFrame();
  RenderUI();

  PresentDraw();
}
