#pragma once

#include "flick.h"

extern void FLJS_Init(void);

extern FL_f64 FLJS_PerformanceNow(void);

FL_Vec2 FLJS_BeginFrame(void);

void FLJS_EndFrame(FL_DrawList *draw_list);
