#pragma once

#include "flick.h"

extern void FLJS_Init(void);
extern FL_f64 FLJS_PerformanceNow(void);
extern void FLJS_ResizeCanvas(FL_f32 w, FL_f32 h, FL_Vec2 *size);
extern void FLJS_Canvas_Save(void);
extern void FLJS_Canvas_Restore(void);
extern void FLJS_Canvas_ClipRect(FL_f32 x, FL_f32 y, FL_f32 width,
                                 FL_f32 height);
extern void FLJS_Canvas_FillRect(FL_f32 x, FL_f32 y, FL_f32 width,
                                 FL_f32 height, FL_Color color);
extern void FLJS_Canvas_StrokeRect(FL_f32 x, FL_f32 y, FL_f32 width,
                                   FL_f32 height, FL_Color color,
                                   FL_f32 line_width);
extern void FLJS_Canvas_MeasureText(FL_Str text, FL_f32 font_size,
                                    FL_TextMetrics *text_metrics);
extern void FLJS_Canvas_FillText(FL_Str text, FL_f32 x, FL_f32 y,
                                 FL_f32 font_size, FL_Color color);

FL_Canvas FLJS_Canvas_Get(void);
