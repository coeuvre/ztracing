#include "src/ztracing.h"

#include <stdarg.h>

#include "src/draw.h"
#include "src/log.h"
#include "src/math.h"
#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"
#include "src/ui.h"

static UITextStyleO default_text_style(void) {
  return ui_text_style_some((UITextStyle){
      .color = ui_color_some(ui_color(0, 0, 0, 1)),
  });
}

typedef enum ButtonType {
  BUTTON_PRIMARY,
  BUTTON_SECONDARY,
} ButtonType;

static bool do_button(Str8 text, ButtonType type) {
  bool pressed;
  ui_button(&(UIButtonProps){
      .text = text,
      .text_style = default_text_style(),

      .pressed = &pressed,
      .fill_color = (type == BUTTON_PRIMARY)
                        ? ui_color_some(ui_color(0.73, 0.83, 0.95, 1))
                        : ui_color_none(),
      .hover_color = ui_color_some(ui_color(0.29, 0.49, 0.72, 1)),
      .splash_color = ui_color_some(ui_color(0.29, 0.44, 0.62, 1)),
      .padding = ui_edge_insets_some(ui_edge_insets_symmetric(6, 3)),
  });
  return pressed;
}

static void build_ui(f32 dt, f32 frame_time) {
  ui_colored_box_begin(&(UIColoredBoxProps){
      .color = ui_color(0.94, 0.94, 0.94, 1.0),
  });
  ui_column_begin(&(UIColumnProps){0});
  {
    ui_row_begin(&(UIRowProps){0});
    {
      if (do_button(STR8_LIT("Load"), BUTTON_PRIMARY)) {
        INFO("Load");
      }
      if (do_button(STR8_LIT("About"), BUTTON_SECONDARY)) {
        INFO("About");
      }
      if (do_button(STR8_LIT("DEBUG"), BUTTON_SECONDARY)) {
        INFO("DEBUG");
      }

      ui_expanded_begin(&(UIExpandedProps){.flex = 1});
      ui_expanded_end();

      ui_padding_begin(&(UIPaddingProps){
          .padding = ui_edge_insets_symmetric(6, 3),
      });
      ui_text(&(UITextProps){
          .text = ui_push_str8f(
              "%.0f %.1fMB %.1fms", 1.0f / dt,
              (f32)((f64)memory_get_allocated_bytes() / 1024.0 / 1024.0),
              frame_time * 1000.0f),
          .style = default_text_style(),
      });
      ui_padding_end();
    }
    ui_row_end();

    // Simulate a bottom border.
    // TODO: Impl DecorationBox.
    ui_container_begin(&(UIContainerProps){
        .height = f32_some(1),
        .color = ui_color_some(ui_color(0.6, 0.6, 0.6, 1.0)),
    });
    ui_container_end();

    ui_expanded_begin(&(UIExpandedProps){
        .flex = 1,
    });

    UIListBuilder builder;
    ui_list_view_begin(&(UIListViewProps){
        .item_extent = 20,
        .item_count = 512,
        .builder = &builder,
    });
    for (i32 item_index = builder.first_index; item_index <= builder.last_index;
         ++item_index) {
      ui_row_begin(&(UIRowProps){0});
      ui_container_begin(&(UIContainerProps){
          .width = f32_some(200.0f),
      });
      ui_text(&(UITextProps){
          .text = ui_push_str8f("Row %u", item_index),
          .style = default_text_style(),
      });
      ui_container_end();

      ui_expanded_begin(&(UIExpandedProps){
          .flex = 1,
      });
      ui_container_begin(&(UIContainerProps){
          .color =
              ui_color_some(ui_color(0, (item_index % 255) / 255.0f, 0, 1)),
      });
      ui_container_end();
      ui_expanded_end();
      ui_row_end();
    }
    ui_list_view_end();
    ui_expanded_end();
  }
  ui_column_end();
  ui_colored_box_end();
}

void do_frame(void) {
  static u64 last_counter;
  static f32 last_frame_time;

  f32 dt = 0.0f;
  u64 current_counter = get_perf_counter();
  if (last_counter) {
    dt = (f32)((f64)(current_counter - last_counter) / (f64)get_perf_freq());
  }
  last_counter = current_counter;

  clear_draw();

  ui_set_delta_time(dt);
  do {
    ui_begin_frame();
    build_ui(dt, last_frame_time);
    ui_end_frame();
  } while (ui_should_rebuild());
  ui_paint();

  last_frame_time =
      (f32)((f64)(get_perf_counter() - last_counter) / (f64)get_perf_freq());

  present_draw();
}
