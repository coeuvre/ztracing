#ifndef ZTRACING_SRC_DRAW_H_
#define ZTRACING_SRC_DRAW_H_

#include "src/flick.h"
#include "src/math.h"
#include "src/string.h"
#include "src/types.h"

// Get the DPI scale of the screen. It's the ratio for (pixels / point).
f32 get_screen_content_scale(void);
// Get the screen size in points.
Vec2 get_screen_size(void);

void push_clip_rect(Vec2 min, Vec2 max);
void pop_clip_rect(void);

typedef struct TextMetrics {
  // Text size in points
  Vec2 size;
} TextMetrics;

// height in points
TextMetrics layout_text_str8(Str8 text, f32 height, f32 min_width,
                             f32 max_width);
void draw_text_str8(Vec2 pos, Str8 text, f32 height, f32 min_width,
                    f32 max_width, FL_Color color);

void clear_draw(void);
void present_draw(void);

void fill_rect(Vec2 min, Vec2 max, FL_Color color);
void stroke_rect(Vec2 min, Vec2 max, FL_Color color, f32 thickness);

FL_Canvas get_canvas(void);

#endif  // ZTRACING_SRC_DRAW_H_
