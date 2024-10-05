#include "src/ztracing.h"

#include "src/draw.h"
#include "src/math.h"
#include "src/string.h"
#include "src/ui.h"
#include "src/ui_widgets.h"

static void BuildUI(void) {
  UIBeginColumn(STR8_LIT("Root"));
  {
    UIBeginRow(STR8_LIT("Menu"));
    UISetSize(V2(kUISizeUndefined, 21.0f));
    UISetColor(DrawColorFromHex(0x5EAC57));
    {
      UIBeginRow(STR8_LIT("Left"));
      UISetCrossAxisAlignment(kUICrossAxisAlignStretch);
      {
        UIBeginBox(STR8_LIT("Load"));
        UISetColor(DrawColorFromHex(0xE6573F));
        UISetText(STR8_LIT("Load"));
        UIEndBox();

        UIBeginBox(STR8_LIT("About"));
        UISetColor(DrawColorFromHex(0xE6573F));
        UISetText(STR8_LIT("About"));
        UIEndBox();
      }
      UIEndRow();

      UIBeginBox(STR8_LIT("Space"));
      UISetFlex(1.0f);
      UIEndBox();

      UIBeginRow(STR8_LIT("Right"));
      UISetCrossAxisAlignment(kUICrossAxisAlignStretch);
      {
        UIBeginBox(STR8_LIT("Right"));
        UISetText(STR8_LIT("FPS"));
        UIEndBox();
      }
      UIEndRow();
    }
    UIEndRow();

    UIBeginBox(STR8_LIT("Main"));
    UISetText(STR8_LIT("Main"));
    UIEndBox();
  }
  UIEndColumn();
}

void DoFrame(void) {
  ClearDraw();

  UIBeginFrame();
  BuildUI();
  UIEndFrame();
  UIRender();

  PresentDraw();
}
