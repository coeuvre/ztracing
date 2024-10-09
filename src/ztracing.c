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

    if (IsUIClicked(kUIMouseButtonLeft)) {
      SetUIColor(ColorU32FromHex(0x0000FF));
    } else if (IsUIHolding(kUIMouseButtonLeft)) {
      SetUIColor(ColorU32FromHex(0x00FF00));
    } else if (IsUIHovering()) {
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

    BeginUIRow();
    {
      static f32 scroll = 0.0f;
      f32 scroll_step = 100.0f;

      f32 item_size = 20.0f;
      u32 item_count = 510;
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

      BeginUIColumn();
      {
        Vec2 head_size = V2(16, 16);

        SetNextUIKey(STR8_LIT("#Head"));
        BeginUIBox();
        {
          SetUISize(head_size);
          SetUIColor(ColorU32FromRGBA(255, 0, 0, 255));

          if (IsUIHolding(kUIMouseButtonLeft)) {
            scroll = scroll - scroll_step;
          }
        }
        EndUIBox();

        SetNextUIKey(STR8_LIT("#ScrollBar"));
        BeginUIColumn();
        {
          SetUIFlex(1);

          f32 min_control_size = 4;
          Vec2 size = GetUIComputed().size;
          Vec2 control_size =
              V2(size.x,
                 MinF32(MaxF32(size.y / total_size, min_control_size), size.y));
          f32 scroll_max = total_size - size.y;
          scroll = ClampF32(scroll, 0, scroll_max);
          f32 control_offset =
              (scroll / scroll_max) * (size.y - control_size.y);

          Vec2 mouse_pos = GetUIMouseRelPos();

          if (IsUIHolding(kUIMouseButtonLeft)) {
            if (ContainsF32(mouse_pos.x, 0, size.x)) {
              if (mouse_pos.y < control_offset) {
                scroll = MaxF32(scroll - scroll_step, 0);
              } else if (mouse_pos.y > control_offset + control_size.y) {
                scroll = MinF32(scroll + scroll_step, scroll_max);
              }
            }
          }

          ColorU32 background_color = ColorU32FromRGBA(128, 128, 128, 255);

          BeginUIBox();
          SetUISize(V2(head_size.x, control_offset));
          SetUIColor(background_color);
          EndUIBox();

          SetNextUIKey(STR8_LIT("#Control"));
          BeginUIBox();
          {
            SetUISize(control_size);
            SetUIColor(background_color);
            SetUIMainAxisAlign(kUIMainAxisAlignCenter);

            b32 hovering = IsUIHovering();
            if (IsUIHolding(kUIMouseButtonLeft)) {
              hovering = 1;
              scroll =
                  ClampF32(mouse_pos.y / (size.y - control_size.y) * scroll_max,
                           0, scroll_max);
            }

            BeginUIBox();
            SetUISize(V2(control_size.x * 0.9f, control_size.y));
            SetUIColor(hovering ? ColorU32FromRGBA(255, 255, 255, 255)
                                : ColorU32FromRGBA(200, 200, 200, 255));
            EndUIBox();
          }
          EndUIBox();

          BeginUIBox();
          SetUISize(V2(head_size.x, kUISizeUndefined));
          SetUIFlex(1);
          SetUIColor(background_color);
          EndUIBox();
        }
        EndUIColumn();

        SetNextUIKey(STR8_LIT("#Tail"));
        BeginUIBox();
        {
          SetUISize(head_size);
          SetUIColor(ColorU32FromRGBA(255, 0, 0, 255));

          if (IsUIHolding(kUIMouseButtonLeft)) {
            scroll = scroll + scroll_step;
          }
        }
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
