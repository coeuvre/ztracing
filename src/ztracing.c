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
      static f32 controll_offset_drag_start = 0.0f;
      f32 scroll_step = 100.0f;

      f32 item_size = 20.0f;
      u32 item_count = 510;
      f32 total_item_size = item_size * item_count;
      f32 scroll_area_size;

      SetNextUIKey(STR8_LIT("#ScrollArea"));
      BeginUIColumn();
      {
        SetUIFlex(1.0);
        // SetUIColor(ColorU32FromHex(0x5EAC57));

        scroll_area_size = GetUIComputed().size.y;

        u32 item_index = FloorF32(scroll / item_size);
        f32 offset = item_index * item_size - scroll;
        for (; item_index < item_count && offset < scroll_area_size;
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
        Vec2 size = GetUIComputed().size;

        f32 min_control_size = 4;
        f32 free_size = MaxF32(size.y - 2 * head_size.y, 0.0f);
        f32 control_size =
            MinF32(MaxF32(scroll_area_size / total_item_size * free_size,
                          min_control_size),
                   free_size);

        f32 scroll_max = total_item_size - scroll_area_size;
        scroll = ClampF32(scroll, 0, scroll_max);

        f32 control_max = free_size - control_size;
        f32 control_offset = (scroll / scroll_max) * control_max;

        Vec2 mouse_pos = GetUIMouseRelPos();
        if (IsUIHolding(kUIMouseButtonLeft)) {
          if (ContainsF32(mouse_pos.x, 0, size.x)) {
            f32 offset = mouse_pos.y - head_size.y;
            if (offset < control_offset) {
              scroll = MaxF32(scroll - scroll_step, 0);
            } else if (offset > control_offset + control_size) {
              scroll = MinF32(scroll + scroll_step, scroll_max);
            }
          }
        }

        SetNextUIKey(STR8_LIT("#Head"));
        BeginUIBox();
        {
          SetUISize(head_size);
          SetUIColor(ColorU32FromRGBA(255, 0, 0, 255));

          if (IsUIHolding(kUIMouseButtonLeft)) {
            scroll = ClampF32(scroll - scroll_step, 0, scroll_max);
          }
        }
        EndUIBox();

        ColorU32 background_color = ColorU32FromRGBA(128, 128, 128, 255);

        BeginUIBox();
        SetUISize(V2(head_size.x, control_offset));
        SetUIColor(background_color);
        EndUIBox();

        SetNextUIKey(STR8_LIT("#Control"));
        BeginUIBox();
        {
          SetUISize(V2(size.x, control_size));
          SetUIColor(background_color);
          SetUIMainAxisAlign(kUIMainAxisAlignCenter);

          ColorU32 control_background_color =
              ColorU32FromRGBA(192, 192, 192, 255);
          if (IsUIHovering()) {
            control_background_color = ColorU32FromRGBA(224, 224, 224, 255);
          }

          if (IsUIPressed(kUIMouseButtonLeft)) {
            controll_offset_drag_start = control_offset;
          }

          if (IsUIHolding(kUIMouseButtonLeft)) {
            Vec2 delta = GetUIMouseDragDelta(kUIMouseButtonLeft);
            f32 offset = controll_offset_drag_start + delta.y;
            scroll = ClampF32(offset / control_max * scroll_max, 0, scroll_max);

            control_background_color = ColorU32FromRGBA(255, 255, 255, 255);
          }

          BeginUIBox();
          SetUISize(V2(size.x * 0.8f, control_size));
          SetUIColor(control_background_color);
          EndUIBox();
        }
        EndUIBox();

        BeginUIBox();
        SetUISize(V2(head_size.x, kUISizeUndefined));
        SetUIFlex(1);
        SetUIColor(background_color);
        EndUIBox();

        SetNextUIKey(STR8_LIT("#Tail"));
        BeginUIBox();
        {
          SetUISize(head_size);
          SetUIColor(ColorU32FromRGBA(255, 0, 0, 255));

          if (IsUIHolding(kUIMouseButtonLeft)) {
            scroll = ClampF32(scroll + scroll_step, 0, scroll_max);
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
