#include "src/ui_widgets.h"

#include <inttypes.h>
#include <stdarg.h>

#include "src/assert.h"
#include "src/math.h"
#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"
#include "src/ui.h"

void BeginUIRow(UIRowProps props) {
  BeginUITag("Row", (UIProps){
                        .key = props.key,
                        .size = props.size,
                        .padding = props.padding,
                        .margin = props.margin,
                        .border = props.border,
                        .color = props.color,
                        .background_color = props.background_color,
                        .main_axis_align = props.main_axis_align,
                        .main_axis = kAxis2X,
                        .main_axis_size = kUIMainAxisSizeMax,
                        .cross_axis_align =
                            props.cross_axis_align != kUICrossAxisAlignUnknown
                                ? props.cross_axis_align
                                : kUICrossAxisAlignCenter,
                    });
}

void BeginUIColumn(UIColumnProps props) {
  BeginUITag("Column", (UIProps){
                           .key = props.key,
                           .size = props.size,
                           .padding = props.padding,
                           .margin = props.margin,
                           .border = props.border,
                           .color = props.color,
                           .background_color = props.background_color,
                           .main_axis_align = props.main_axis_align,
                           .main_axis = kAxis2Y,
                           .main_axis_size = kUIMainAxisSizeMax,
                           .cross_axis_align = props.cross_axis_align !=
                                                       kUICrossAxisAlignUnknown
                                                   ? props.cross_axis_align
                                                   : kUICrossAxisAlignCenter,
                       });
}

void DoUIText(UITextProps props) {
  BeginUITag("Text", (UIProps){
                         .key = props.key,
                         .size = props.size,
                         .text = props.text,
                         .padding = props.padding,
                         .margin = props.margin,
                         .border = props.border,
                         .color = props.color,
                         .background_color = props.background_color,
                     });
  EndUITag("Text");
}

typedef struct UIButtonState {
  ColorU32 background_color;
  ColorU32 target_background_color;
} UIButtonState;

bool BeginUIButton(UIButtonProps props) {
  UIEdgeInsets padding = props.padding;
  if (!IsUIEdgeInsetsSet(padding)) {
    // TODO: Use theme
    padding = UIEdgeInsetsSymmetric(6, 3);
  }

  bool clicked;
  BeginUITag("Button", (UIProps){
                           .size = props.size,
                           .position = props.position,
                           .offset = props.offset,
                           .padding = padding,
                           .text = props.text,
                       });
  {
    UIButtonState *state = PushUIBoxStruct(UIButtonState);

    if (IsUIMouseButtonDown(kUIMouseButtonLeft)) {
      state->target_background_color = ColorU32FromHex(0x4B6F9E);
    } else if (IsUIMouseHovering()) {
      state->target_background_color = ColorU32FromHex(0x4B7DB8);
    } else if (props.default_background_color) {
      state->target_background_color = ColorU32FromHex(0xB9D3F3);
    } else {
      state->target_background_color = ColorU32Zero();
    }

    if (props.hoverred) {
      *props.hoverred = IsUIMouseHovering();
    }

    clicked = IsUIMouseButtonClicked(kUIMouseButtonLeft);

    state->background_color = AnimateUIFastColorU32(
        state->background_color, state->target_background_color);

    GetCurrentUIBox()->props.background_color = state->background_color;
  }

  return clicked;
}

void EndUIButton(void) { EndUITag("Button"); }

typedef struct UICollapsingState {
  bool init;
  f32 open_t;
  UIBox *header;
} UICollapsingState;

bool BeginUICollapsing(UICollapsingProps props) {
  BeginUITag("Collapsing", (UIProps){0});
  UICollapsingState *state = PushUIBoxStruct(UICollapsingState);
  if (!state->init) {
    if (props.open && *props.open) {
      state->open_t = 1.0f;
    }
    state->init = true;
  }

  BeginUIColumn((UIColumnProps){0});
  {
    bool clicked = BeginUIButton((UIButtonProps){
        .default_background_color = props.header.default_background_color,
        .padding = UIEdgeInsetsAll(0),
        .hoverred = props.header.hoverred,
    });
    state->header = GetCurrentUIBox();
    {
      BeginUIRow((UIRowProps){
          .padding = props.header.padding,
      });
      {
        if (props.open && clicked) {
          *props.open = !*props.open;
        }

        Str8 prefix = STR8_LIT("   ");
        if (props.open) {
          prefix = *props.open ? STR8_LIT(" - ") : STR8_LIT(" + ");
        }

        BeginUIBox((UIProps){
            .text = PushUIStr8F("%s%s", prefix.ptr, props.header.text.ptr),
        });
        EndUIBox();
      }
      EndUIRow();
    }
    EndUIButton();

    // Clip box
    BeginUIBox((UIProps){0});

    BeginUIBox((UIProps){0});
    UIBox *content = GetCurrentUIBox();
    if (props.open && *props.open && content->computed.size.y == 0) {
      // For the first frame, the content size is unknown. Make margin -INF
      // effectively make it invisible.
      content->props.margin = UIEdgeInsetsFromLTRB(0, -kUISizeInfinity, 0, 0);
    } else {
      content->props.margin = UIEdgeInsetsFromLTRB(
          0, (1.0f - state->open_t) * -content->computed.size.y, 0, 0);
    }
  }

  state->open_t = AnimateUIFastF32(state->open_t,
                                   (props.open && *props.open) ? 1.0f : 0.0f);

  bool result = state->open_t != 0.0f;
  return result;
}

void EndUICollapsing(void) {
  {
    EndUIBox();
    EndUIBox();
  }
  EndUIColumn();
  EndUITag("Collapsing");
}

typedef struct UIScrollableState {
  b32 init;

  // persistent info
  f32 scroll;
  f32 *target_scroll;
  f32 control_offset_drag_start;

  // per-frame info
  f32 scroll_area_size;
  f32 scroll_max;
  Vec2 head_size;
  f32 scroll_step;
  f32 control_max;
  f32 control_offset;
  f32 control_size;
} UIScrollableState;

void BeginUIScrollable(UIScrollableProps props) {
  BeginUITag("Scrollable", (UIProps){
                               .main_axis = kAxis2X,
                           });
  {
    UIScrollableState *state = PushUIBoxStruct(UIScrollableState);
    state->target_scroll = props.scroll;

    Vec2 wheel_delta;
    b32 scrolling = IsUIMouseScrolling(&wheel_delta);

    BeginUITag("ScrollArea", (UIProps){
                                 .flex = 1,
                                 .main_axis = kAxis2Y,
                                 .size = V2(kUISizeUndefined, kUISizeInfinity),
                             });
    {
      UIBox *scroll_area = GetCurrentUIBox();
      state->scroll_area_size = scroll_area->computed.size.y;

      BeginUITag("ScrollContent",
                 (UIProps){
                     .margin = UIEdgeInsetsFromLTRB(0, -state->scroll, 0, 0),
                 });
      UIBox *scroll_content = GetCurrentUIBox();
      f32 total_item_size = scroll_content->computed.size.y;

      state->head_size = V2(10, 0);
      state->scroll_max = MaxF32(total_item_size - state->scroll_area_size, 0);
      // Assume first frame if scroll_max is 0
      if (state->scroll_max) {
        if (state->target_scroll) {
          *state->target_scroll =
              ClampF32(*state->target_scroll, 0, state->scroll_max);
          state->scroll =
              AnimateUIFastF32(state->scroll, *state->target_scroll);
        }
        // Only clamp scroll for non-first frame
        state->scroll = ClampF32(state->scroll, 0, state->scroll_max);
      } else {
        if (state->target_scroll) {
          state->scroll = *state->target_scroll;
        }
      }

      f32 min_control_size = 4;
      f32 free_size =
          MaxF32(state->scroll_area_size - 2 * state->head_size.y, 0.0f);
      state->control_size =
          MinF32(MaxF32(state->scroll_area_size / total_item_size * free_size,
                        min_control_size),
                 free_size);

      state->scroll_step = 0.2f * state->scroll_area_size;

      state->control_max = free_size - state->control_size;
      state->control_offset =
          (state->scroll / state->scroll_max) * state->control_max;
      if (state->target_scroll && scrolling) {
        *state->target_scroll =
            ClampF32(*state->target_scroll + wheel_delta.y * state->scroll_step,
                     0, state->scroll_max);
      }

      // ...
    }
  }
}

static void DoUIScrollableScrollBar(UIScrollableState *state) {
  BeginUITag("ScrollBar", (UIProps){0});
  BeginUIColumn((UIColumnProps){0});
  {
    Vec2 mouse_pos = GetUIMouseRelPos();
    if (IsUIMouseButtonDown(kUIMouseButtonLeft)) {
      if (ContainsF32(mouse_pos.x, 0, state->head_size.x) &&
          state->target_scroll) {
        f32 offset = mouse_pos.y - state->head_size.y;
        if (offset < state->control_offset) {
          *state->target_scroll =
              ClampF32(*state->target_scroll - 0.2f * state->scroll_step, 0,
                       state->scroll_max);
        } else if (offset > state->control_offset + state->control_size) {
          *state->target_scroll =
              ClampF32(*state->target_scroll + 0.2f * state->scroll_step, 0,
                       state->scroll_max);
        }
      }
    }
    ColorU32 background_color = ColorU32FromHex(0xF5F5F5);

    BeginUIBox((UIProps){
        .size = V2(state->head_size.x, state->control_offset),
        .background_color = background_color,
    });
    EndUIBox();

    BeginUIBox((UIProps){0});
    {
      ColorU32 control_background_color = ColorU32FromHex(0xBEBEBE);
      if (IsUIMouseHovering()) {
        control_background_color = ColorU32FromHex(0x959595);
      }
      if (IsUIMouseButtonPressed(kUIMouseButtonLeft)) {
        state->control_offset_drag_start = state->control_offset;
      }
      Vec2 drag_delta;
      if (IsUIMouseButtonDragging(kUIMouseButtonLeft, &drag_delta) &&
          state->target_scroll) {
        f32 offset = state->control_offset_drag_start + drag_delta.y;
        *state->target_scroll =
            ClampF32(offset / state->control_max * state->scroll_max, 0,
                     state->scroll_max);

        control_background_color = ColorU32FromHex(0x7D7D7D);
      }

      BeginUIBox((UIProps){
          .size = V2(state->head_size.x, state->control_size),
          .background_color = control_background_color,
      });
      EndUIBox();
    }
    EndUIBox();

    BeginUIBox((UIProps){
        .size = V2(state->head_size.x, kUISizeUndefined),
        .flex = 1,
        .background_color = background_color,
    });
    EndUIBox();
  }
  EndUIColumn();
  EndUITag("ScrollBar");
}

void EndUIScrollable(void) {
  {
    {
      // ...
      EndUITag("ScrollContent");
    }
    EndUITag("ScrollArea");

    UIScrollableState *state = GetUIBoxStruct(UIScrollableState);
    if (state->scroll_max > 0) {
      DoUIScrollableScrollBar(state);
    }
  }
  EndUITag("Scrollable");
}

typedef struct UIBoxDebugState UIBoxDebugState;
struct UIBoxDebugState {
  UIBoxDebugState *child[4];
  UIID id;

  bool open;
};

typedef struct UIDebugLayerState {
  bool init;
  bool open;
  Vec2 min;
  Vec2 max;
  Vec2 pressed_min;
  Vec2 pressed_max;
  f32 scroll;
  Rect2 hoverred_rect;
  UIBox *debug_layer;

  Arena *arena;
  UIBoxDebugState *root;
} UIDebugLayerState;

static UIBoxDebugState *PushUIBoxDebugState(UIDebugLayerState *state, UIID id,
                                            bool build_order) {
  id = UIIDFromU8(id, (u8)build_order);

  UIBoxDebugState **node = &state->root;
  for (u64 hash = id.hash; *node; hash <<= 2) {
    if (IsZeroUIID(id) || IsEqualUIID(id, (*node)->id)) {
      break;
    }
    node = &((*node)->child[hash >> 62]);
  }
  if (!*node) {
    UIBoxDebugState *debug_state = PushArray(state->arena, UIBoxDebugState, 1);
    debug_state->id = id;
    (*node) = debug_state;
  }
  return *node;
}

static void UIDebugLayerBoxR(UIDebugLayerState *state, UIBox *box, u32 level,
                             bool build_order) {
  if (box == state->debug_layer) {
    return;
  }

  UIBoxDebugState *box_debug_state =
      PushUIBoxDebugState(state, box->id, build_order);

  bool has_key = !IsEmptyStr8(box->props.key);
  Str8 text = PushUIStr8F("%s%s%s", box->tag, has_key ? "#" : "",
                          has_key ? (char *)box->props.key.ptr : "");

  bool header_hovered;
  if (BeginUICollapsing((UICollapsingProps){
          .open = (build_order ? box->build.first : box->stack.first)
                      ? &box_debug_state->open
                      : 0,
          .header =
              (UICollapsingHeaderProps){
                  .text = text,
                  .padding = UIEdgeInsetsFromLTRB(level * 15, 0, 0, 0),
                  .hoverred = &header_hovered,
              },
      })) {
    BeginUIColumn((UIColumnProps){0});
    for (UIBox *child = (build_order ? box->build.first : box->stack.first);
         child; child = (build_order ? child->build.next : child->stack.next)) {
      UIDebugLayerBoxR(state, child, level + 1, build_order);
    }
    EndUIColumn();
  }
  EndUICollapsing();

  if (header_hovered) {
    Rect2 hoverred_rect = box->computed.screen_rect;
    {
      Vec2 size = SubVec2(hoverred_rect.max, hoverred_rect.min);
      // Make zero area box visible.
      if (size.x == 0 && size.y != 0) {
        hoverred_rect.max.x = hoverred_rect.min.x + 1;
        hoverred_rect.min.x -= 1;
      } else if (size.y == 0 && size.x != 0) {
        hoverred_rect.max.y = hoverred_rect.min.y + 1;
        hoverred_rect.min.y -= 1;
      }
    }
    state->hoverred_rect = hoverred_rect;
  }
}

static void UIDebugLayerInternal(UIDebugLayerState *state) {
  UIState *ui_state = GetUIState();
  UIFrame *frame = ui_state->frames + ((ui_state->frame_index - 1) %
                                       ARRAY_COUNT(ui_state->frames));

  BeginUIBox((UIProps){
      .padding = UIEdgeInsetsSymmetric(6, 3),
  });
  BeginUIColumn((UIColumnProps){0});
  BeginUIBox((UIProps){0});
  {
    BeginUIColumn((UIColumnProps){0});

    BeginUIRow((UIRowProps){0});
    DoUIText((UITextProps){
        .text = PushUIStr8F("Boxes: %" PRIu64 " / %" PRIu64,
                            frame->cache.total_box_count,
                            frame->cache.box_hash_slots_count),
    });
    EndUIRow();

    EndUIColumn();
  }
  EndUIBox();

  if (frame->root) {
    UIDebugLayerBoxR(state, frame->root, 0, /* build_order= */ true);
    UIDebugLayerBoxR(state, frame->root, 0, /* build_order= */ false);
  }

  EndUIColumn();
  EndUIBox();
}

void DoUIDebugLayer(UIDebugLayerProps props) {
  ASSERTF(props.arena, "Must provide an arena");

  f32 resize_handle_size = 16;
  Vec2 default_frame_size = V2(400, 500);
  Vec2 min_frame_size = V2(resize_handle_size * 2, resize_handle_size * 2);

  BeginUITag("DebugLayer", (UIProps){
                               .z_index = kUIDebugLayerZIndex,
                           });
  UIDebugLayerState *state = PushUIBoxStruct(UIDebugLayerState);
  if (!state->init) {
    if (IsZeroVec2(SubVec2(state->max, state->min))) {
      state->max =
          AddVec2(state->min, V2(default_frame_size.x + resize_handle_size,
                                 default_frame_size.y + resize_handle_size));
    }
    state->arena = props.arena;
    state->init = 1;
  }

  UIBox *debug_layer = GetCurrentUIBox();
  state->debug_layer = GetUIBoxFromFrame(GetLastUIFrame(), debug_layer->id);

  if (props.open) {
    state->open = *props.open;
  }

  state->hoverred_rect = Rect2Zero();
  if (state->open) {
    BeginUITag("Float",
               (UIProps){
                   .size = SubVec2(state->max, state->min),
                   .color = ColorU32FromHex(0x000000),
                   .position = kUIPositionFixed,
                   .offset = UIEdgeInsetsFromLT(state->min.x, state->min.y),
                   .border = UIBorderFromBorderSide((UIBorderSide){
                       .color = ColorU32FromHex(0xA8A8A8),
                       .width = 1,
                   }),
               });
    {
      SetUIBoxBlockMouseInput();

      if (IsUIMouseButtonPressed(kUIMouseButtonLeft)) {
        state->pressed_min = state->min;
        state->pressed_max = state->max;
      }
      Vec2 drag_delta;
      if (IsUIMouseButtonDragging(kUIMouseButtonLeft, &drag_delta)) {
        Vec2 size = SubVec2(state->max, state->min);
        state->min = RoundVec2(AddVec2(state->pressed_min, drag_delta));
        state->max = AddVec2(state->min, size);
      }

      BeginUIBox((UIProps){
          .background_color = ColorU32FromHex(0xF0F0F0),
      });
      BeginUIColumn((UIColumnProps){0});
      {
        BeginUIBox((UIProps){
            .background_color = ColorU32FromHex(0xD1D1D1),
        });
        BeginUIRow((UIRowProps){0});
        {
          UIEdgeInsets padding = UIEdgeInsetsSymmetric(6, 3);
          DoUIText((UITextProps){
              .text = STR8_LIT("Debug"),
              .padding = padding,
          });

          BeginUIBox((UIProps){.flex = 1});
          EndUIBox();

          if (DoUIButton((UIButtonProps){
                  .text = STR8_LIT("X"),
                  .padding = padding,
              })) {
            state->open = false;
            if (props.open) {
              *props.open = false;
            }
          }
        }
        EndUIRow();
        EndUIBox();

        BeginUIScrollable((UIScrollableProps){.scroll = &state->scroll});
        UIDebugLayerInternal(state);
        EndUIScrollable();
      }
      EndUIColumn();
      EndUIBox();

      BeginUIButton((UIButtonProps){
          .default_background_color = 1,
          .position = kUIPositionAbsolute,
          .offset = UIEdgeInsetsFromRB(1, 1),
          .size = V2(resize_handle_size, resize_handle_size),
      });
      {
        if (IsUIMouseButtonPressed(kUIMouseButtonLeft)) {
          state->pressed_min = state->min;
          state->pressed_max = state->max;
        }
        Vec2 drag_delta;
        if (IsUIMouseButtonDragging(kUIMouseButtonLeft, &drag_delta)) {
          state->max = RoundVec2(AddVec2(state->pressed_max, drag_delta));
          state->max = MaxVec2(state->max, AddVec2(state->min, min_frame_size));
        }
      }
      EndUIButton();
    }
    EndUITag("Float");

    BeginUITag("Highlight", (UIProps){0});
    if (GetRect2Area(state->hoverred_rect) > 0) {
      Rect2 hoverred_rect = state->hoverred_rect;

      GetCurrentUIBox()->props = (UIProps){
          .background_color = ColorU32FromSRGBNotPremultiplied(255, 0, 255, 64),
          .size = SubVec2(hoverred_rect.max, hoverred_rect.min),
          .z_index = -1,
          .position = kUIPositionFixed,
          .offset =
              UIEdgeInsetsFromLT(hoverred_rect.min.x, hoverred_rect.min.y),
      };
    }
    EndUITag("Highlight");
  }
  EndUITag("DebugLayer");
}
