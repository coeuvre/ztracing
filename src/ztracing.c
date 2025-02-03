#include "src/ztracing.h"

#include <stdarg.h>

#include "src/draw.h"
#include "src/math.h"
#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"
#include "src/ui.h"
// #include "src/ui_widgets.h"

// static void build_ui(f32 dt, f32 frame_time) {
//   static bool debug_layer_open = true;
//   static Arena debug_layer_arena;
//
//   ui_column_begin((UIColumnProps){
//       .color = color_u32_from_srgb_not_premultiplied(0, 0, 0, 255),
//       .background_color = color_u32_from_hex(0xF0F0F0),
//   });
//   {
//     ui_row_begin((UIRowProps){
//         .border =
//             (UIBorder){
//                 .bottom =
//                     (UIBorderSide){
//                         .width = 1,
//                         .color = color_u32_from_hex(0x999999),
//                     },
//             },
//     });
//     {
//       DoUIButton((UIButtonProps){
//           .text = str8_lit("Load"),
//           .default_background_color = 1,
//       });
//
//       if (DoUIButton((UIButtonProps){
//               .text = str8_lit("Debug"),
//           })) {
//         debug_layer_open = true;
//       }
//
//       ui_box_begin((UIProps){.flex = 1});
//       ui_box_end();
//
//       ui_text((UITextProps){
//           .text = ui_push_str8f(
//               "%.0f %.1fMB %.1fms", 1.0f / dt,
//               (f32)((f64)memory_get_allocated_bytes() / 1024.0 / 1024.0),
//               frame_time * 1000.0f),
//           .padding = ui_edge_insets_symmetric(6, 3),
//       });
//     }
//     ui_row_end();
//
//     static f32 scroll_drag_started;
//     static f32 scroll;
//
//     ui_scrollable_begin((UIScrollableProps){.scroll = &scroll});
//     {
//       f32 item_size = 20.0f;
//       u32 item_count = 510;
//
//       // u32 item_index = FloorF32(state->scroll / item_size);
//       // f32 offset = item_index * item_size - state->scroll;
//       // for (; item_index < item_count && offset < state->scroll_area_size;
//       //      ++item_index, offset += item_size)
//       ui_column_begin((UIColumnProps){0});
//       {
//         if (ui_is_mouse_button_pressed(kUIMouseButtonLeft)) {
//           scroll_drag_started = scroll;
//         }
//         Vec2 drag_delta;
//         if (ui_is_mouse_button_dragging(kUIMouseButtonLeft, &drag_delta)) {
//           scroll = scroll_drag_started - drag_delta.y;
//         }
//
//         for (u32 item_index = 0; item_index < item_count; ++item_index) {
//           ui_row_begin((UIRowProps){
//               .size = v2(kUISizeUndefined, item_size),
//               .background_color = color_u32_from_srgb_not_premultiplied(
//                   0, 0, item_index % 255, 255),
//           });
//           ui_row_end();
//         }
//       }
//       ui_column_end();
//     }
//     ui_scrollable_end();
//   }
//   ui_column_end();
//
//   ui_debug_layer((UIDebugLayerProps){
//       .arena = &debug_layer_arena,
//       .open = &debug_layer_open,
//   });
// }

static void build_ui(f32 dt, f32 frame_time) {
  ui_flex_begin((UIFlexProps){});
  ui_flex_end();
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

  ui_set_viewport(vec2_zero(), get_screen_size());
  // ui_set_delta_time(dt);
  ui_begin_frame();
  build_ui(dt, last_frame_time);
  ui_end_frame();
  // ui_render();

  last_frame_time =
      (f32)((f64)(get_perf_counter() - last_counter) / (f64)get_perf_freq());

  present_draw();
}
