#include "src/draw.h"

#include "src/flick.h"
#include "src/math.h"
#include "src/types.h"

void stroke_rect(Vec2 min, Vec2 max, FL_Color color, f32 thickness) {
  fill_rect(min, vec2(max.x, min.y + thickness), color);
  fill_rect(vec2(min.x, min.y + thickness), vec2(min.x + thickness, max.y),
            color);
  fill_rect(vec2(max.x - thickness, min.y + thickness), vec2(max.x, max.y),
            color);
  fill_rect(vec2(min.x + thickness, max.y - thickness),
            vec2(max.x - thickness, max.y), color);
}

static void fl_canvas_fill_rect_impl(void *ctx, FL_Rect rect, FL_Color color) {
  (void)ctx;
  fill_rect((Vec2){rect.left, rect.top}, (Vec2){rect.right, rect.bottom},
            color);
}

static void fl_canvas_fill_text_impl(void *ctx, FL_Str text, float x, float y,
                                     float font_size, FL_Color color) {
  (void)ctx;
  draw_text_str8(vec2(x, y), text, font_size, 0, 10000, color);
}

static FL_TextMetrics fl_canvas_measure_text_impl(void *ctx, FL_Str text,
                                                  float font_size) {
  (void)ctx;
  TextMetrics metrics = layout_text_str8(text, font_size, 0, 10000);
  return (FL_TextMetrics){
      .font_bounding_box_ascent = 0,
      .font_bounding_box_descent = metrics.size.y,
      .width = metrics.size.x,
  };
}

FL_Canvas get_canvas(void) {
  return (FL_Canvas){
      .fill_rect = fl_canvas_fill_rect_impl,
      .fill_text = fl_canvas_fill_text_impl,
      .measure_text = fl_canvas_measure_text_impl,
  };
}
