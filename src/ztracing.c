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

    if (IsUIMouseButtonClicked(kUIMouseButtonLeft)) {
      SetUIColor(ColorU32FromHex(0x0000FF));
    } else if (IsUIMouseButtonDown(kUIMouseButtonLeft)) {
      SetUIColor(ColorU32FromHex(0x00FF00));
    } else if (IsUIMouseHovering()) {
      SetUIColor(ColorU32FromHex(0xFF0000));
    } else {
      // SetUIColor(ColorU32FromHex(0x5EAC57));
    }

    BeginUIBox();
    {
      SetUIText(label);
    }
    EndUIBox();
  }
  EndUIBox();
}

static void UIText(Str8 text) {
  BeginUIBox();
  {
    SetUIPadding(UIEdgeInsetsSymmetric(6, 4));

    BeginUIBox();
    {
      SetUIText(text);
    }
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

    static UIScrollableState state;

    BeginUIScrollable(&state);
    {
      f32 item_size = 20.0f;
      u32 item_count = 510;

      // u32 item_index = FloorF32(state->scroll / item_size);
      // f32 offset = item_index * item_size - state->scroll;
      // for (; item_index < item_count && offset < state->scroll_area_size;
      //      ++item_index, offset += item_size) {
      for (u32 item_index = 0; item_index < item_count; ++item_index) {
        BeginUIRow();
        SetUISize(V2(kUISizeUndefined, item_size));
        SetUIColor(ColorU32FromRGBA(0, 0, item_index % 256, 255));
        EndUIRow();
      }
    }
    EndUIScrollable(&state);
  }
  EndUIColumn();

  EndScratch(scratch);
}

void DoFrame(f32 dt) {
  ClearDraw();

  SetUIDeltaTime(dt);
  BeginUIFrame(GetScreenSize(), GetScreenContentScale());
  BuildUI(dt);
  EndUIFrame();
  RenderUI();

  PresentDraw();
}
