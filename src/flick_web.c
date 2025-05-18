#include "flick_web.h"

#include <emscripten/emscripten.h>
#include <stdint.h>

#include "flick.h"

#ifndef EMSCRIPTEN_KEEPALIVE
#define EMSCRIPTEN_KEEPALIVE
#endif

EMSCRIPTEN_KEEPALIVE
void FLJS_OnMouseButtonDown(FL_f32 x, FL_f32 y, FL_u32 button) {
  FL_OnMouseButtonDown((FL_Vec2){x, y}, button);
}

EMSCRIPTEN_KEEPALIVE
void FLJS_OnMouseButtonUp(FL_f32 x, FL_f32 y, FL_u32 button) {
  FL_OnMouseButtonUp((FL_Vec2){x, y}, button);
}

EMSCRIPTEN_KEEPALIVE
void FLJS_OnMouseMove(FL_f32 x, FL_f32 y) { FL_OnMouseMove((FL_Vec2){x, y}); }

EMSCRIPTEN_KEEPALIVE
void FLJS_OnMouseScroll(FL_f32 pos_x, FL_f32 pos_y, FL_f32 delta_x,
                        FL_f32 delta_y) {
  FL_OnMouseScroll((FL_Vec2){pos_x, pos_y}, (FL_Vec2){delta_x, delta_y});
}

void FL_Canvas_Save_Impl(void *ctx) {
  (void)ctx;
  FLJS_Canvas_Save();
}

void FL_Canvas_Restore_Impl(void *ctx) {
  (void)ctx;
  FLJS_Canvas_Restore();
}

void FL_Canvas_ClipRect_Impl(void *ctx, FL_Rect rect) {
  (void)ctx;
  FLJS_Canvas_ClipRect(rect.left, rect.top, rect.right - rect.left,
                       rect.bottom - rect.top);
}

void FL_Canvas_FillRect_Impl(void *ctx, FL_Rect rect, FL_Color color) {
  (void)ctx;
  FLJS_Canvas_FillRect(rect.left, rect.top, rect.right - rect.left,
                       rect.bottom - rect.top, color);
}

void FL_Canvas_StrokeRect_Impl(void *ctx, FL_Rect rect, FL_Color color,
                               FL_f32 line_width) {
  (void)ctx;
  FLJS_Canvas_StrokeRect(rect.left, rect.top, rect.right - rect.left,
                         rect.bottom - rect.top, color, line_width);
}

FL_TextMetrics FL_Canvas_MeasureText_Impl(void *ctx, FL_Str text,
                                          FL_f32 font_size) {
  (void)ctx;
  FL_TextMetrics result = {0};
  FLJS_Canvas_MeasureText(text, font_size, &result);
  return result;
}

void FL_Canvas_FillText_Impl(void *ctx, FL_Str text, FL_f32 x, FL_f32 y,
                             FL_f32 font_size, FL_Color color) {
  (void)ctx;
  FLJS_Canvas_FillText(text, x, y, font_size, color);
}

FL_Canvas FLJS_Canvas_Get(void) {
  return (FL_Canvas){
      .save = FL_Canvas_Save_Impl,
      .restore = FL_Canvas_Restore_Impl,
      .clip_rect = FL_Canvas_ClipRect_Impl,
      .fill_rect = FL_Canvas_FillRect_Impl,
      .stroke_rect = FL_Canvas_StrokeRect_Impl,
      .measure_text = FL_Canvas_MeasureText_Impl,
      .fill_text = FL_Canvas_FillText_Impl,
  };
}
