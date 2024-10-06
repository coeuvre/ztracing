#include "src/ztracing.h"

#include "src/draw.h"
#include "src/math.h"
#include "src/string.h"
#include "src/ui.h"

static void BuildUI(void) {
  BeginUIBox(STR8_LIT("Root"));
  SetUIMainAxis(kAxis2Y);
  {
    BeginUIBox(STR8_LIT("Menu"));
    SetUISize(V2(kUISizeUndefined, 21.0f));
    SetUIColor(ColorU32FromHex(0x5EAC57));
    {
      BeginUIBox(STR8_LIT("Left"));
      SetUICrossAxisAlign(kUICrossAxisAlignStretch);
      {
        BeginUIBox(STR8_LIT("Load"));
        SetUIColor(ColorU32FromHex(0xE6573F));
        {
          BeginUIBox(STR8_LIT("Label"));
          SetUIText(STR8_LIT("Load"));
          EndUIBox();
        }
        EndUIBox();

        BeginUIBox(STR8_LIT("About"));
        SetUIColor(ColorU32FromHex(0xE6573F));
        {
          BeginUIBox(STR8_LIT("Label"));
          SetUIText(STR8_LIT("About"));
          EndUIBox();
        }
        EndUIBox();
      }
      EndUIBox();

      BeginUIBox(STR8_LIT("Space"));
      SetUIFlex(1.0f);
      EndUIBox();

      BeginUIBox(STR8_LIT("Right"));
      SetUICrossAxisAlign(kUICrossAxisAlignStretch);
      {
        BeginUIBox(STR8_LIT("Right"));
        {
          BeginUIBox(STR8_LIT("Label"));
          SetUIText(STR8_LIT("FPS"));
          EndUIBox();
        }
        EndUIBox();
      }
      EndUIBox();
    }
    EndUIBox();

    BeginUIBox(STR8_LIT("Main"));
    SetUIText(STR8_LIT("Main"));
    EndUIBox();
  }
  EndUIBox();
}

void DoFrame(void) {
  ClearDraw();

  BeginUIFrame(Vec2FromVec2I(GetScreenSizeInPixel()), GetScreenContentScale());
  BuildUI();
  EndUIFrame();
  RenderUI();

  PresentDraw();
}
