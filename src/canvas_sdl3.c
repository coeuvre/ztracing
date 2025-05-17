#include "src/canvas_sdl3.h"

#include <SDL3/SDL.h>

#include "src/assert.h"
#include "src/flick.h"
#include "src/list.h"
#include "src/math.h"
#include "src/string.h"
#include "src/types.h"

#define STB_RECT_PACK_IMPLEMENTATION
#define STBRP_STATIC
#include "third_party/stb/stb_rect_pack.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "assets/JetBrainsMono-Regular.h"
#include "third_party/stb/stb_truetype.h"

typedef struct PackedFont PackedFont;
struct PackedFont {
  PackedFont *prev;
  PackedFont *next;

  stbtt_fontinfo *font;
  SDL_Texture *texture;
  i32 width;
  i32 height;
  stbtt_pack_range range;
  i32 *kern_cache;
};

typedef struct CanvasState CanvasState;
struct CanvasState {
  CanvasState *prev;
  CanvasState *next;

  bool enable_clip;
  SDL_Rect clip_rect;
};

typedef struct Canvas {
  FL_Arena *arena;
  SDL_Window *window;
  SDL_Renderer *renderer;

  CanvasState *first_state;
  CanvasState *last_state;
  CanvasState *first_free_state;

  PackedFont *first_packed_font;
  PackedFont *last_packed_font;
  stbtt_fontinfo font;
} Canvas;

static f32 GetScreenContentScale(Canvas *canvas) {
  f32 result = SDL_GetWindowDisplayScale(canvas->window);
  if (result == 0) {
    result = 1;
  }
  return result;
}

typedef struct ColorU32 {
  u8 a;
  u8 r;
  u8 g;
  u8 b;
} ColorU32;

static inline ColorU32 ColorU32_FromColor(FL_Color color) {
  ColorU32 result;
  result.r = f32_round(color.r * color.a * 255.0f);
  result.g = f32_round(color.g * color.a * 255.0f);
  result.b = f32_round(color.b * color.a * 255.0f);
  result.a = f32_round(color.a * 255.0f);
  return result;
}

static void Canvas_Save(void *ctx) {
  Canvas *canvas = ctx;
  CanvasState *state;
  if (canvas->first_free_state) {
    state = canvas->first_free_state;
    canvas->first_free_state = canvas->first_free_state->next;
  } else {
    state = FL_Arena_PushStruct(canvas->arena, CanvasState);
  }
  *state = *canvas->last_state;
  DLL_APPEND(canvas->first_state, canvas->last_state, state, prev, next);
}

static void Canvas_Restore(void *ctx) {
  Canvas *canvas = ctx;
  CanvasState *state = canvas->last_state;
  if (state->prev) {
    DLL_REMOVE(canvas->first_state, canvas->last_state, state, prev, next);
    state->next = canvas->first_free_state;
    canvas->first_free_state = state;

    state = canvas->last_state;
  }
  bool ok;
  if (state->enable_clip) {
    ok = SDL_SetRenderClipRect(canvas->renderer, &state->clip_rect);
  } else {
    ok = SDL_SetRenderClipRect(canvas->renderer, 0);
  }
  ASSERT(ok);
}

static void Canvas_ClipRect(void *ctx, FL_Rect rect) {
  Canvas *canvas = ctx;
  CanvasState *state = canvas->last_state;
  f32 content_scale = GetScreenContentScale(canvas);
  state->enable_clip = true;
  state->clip_rect = (SDL_Rect){
      .x = rect.left * content_scale,
      .y = rect.top * content_scale,
      .w = (rect.right - rect.left) * content_scale,
      .h = (rect.bottom - rect.top) * content_scale,
  };
  bool ok = SDL_SetRenderClipRect(canvas->renderer, &state->clip_rect);
  ASSERT(ok);
}

static void Canvas_FillRect(void *ctx, FL_Rect rect, FL_Color color) {
  Canvas *canvas = ctx;

  Vec2 min = {rect.left, rect.top};
  Vec2 max = {rect.right, rect.bottom};
  min = vec2_mul(min, GetScreenContentScale(canvas));
  max = vec2_mul(max, GetScreenContentScale(canvas));

  ColorU32 c = ColorU32_FromColor(color);
  SDL_SetRenderDrawColor(canvas->renderer, c.r, c.g, c.b, c.a);
  SDL_RenderFillRect(canvas->renderer, &(SDL_FRect){
                                           .x = min.x,
                                           .y = min.y,
                                           .w = max.x - min.x,
                                           .h = max.y - min.y,
                                       });
}

static void Canvas_StrokeRect(void *ctx, FL_Rect rect, FL_Color color,
                              FL_f32 line_width) {
  Canvas *canvas = ctx;

  f32 left = rect.left;
  f32 right = rect.right;
  f32 top = rect.top;
  f32 bottom = rect.bottom;
  f32 half_width = line_width / 2.0f;

  Canvas_FillRect(canvas,
                  (FL_Rect){left - half_width, right + half_width,
                            top - half_width, top + half_width},
                  color);
  Canvas_FillRect(canvas,
                  (FL_Rect){left - half_width, left + half_width,
                            top + half_width, bottom + half_width},
                  color);
  Canvas_FillRect(canvas,
                  (FL_Rect){right - half_width, right + half_width,
                            top + half_width, bottom + half_width},
                  color);
  Canvas_FillRect(canvas,
                  (FL_Rect){left + half_width, right - half_width,
                            bottom - half_width, bottom + half_width},
                  color);
}

static PackedFont PackFont(Canvas *canvas, stbtt_fontinfo *info,
                           f32 font_size) {
  FL_Arena scratch = *canvas->arena;
  PackedFont result = {0};
  result.font = info;
  result.width = 1024;
  result.height = 1024;
  u8 *pixels_u8 =
      FL_Arena_PushArray(&scratch, u8, result.width * result.height);
  stbtt_pack_context spc;
  ASSERT(stbtt_PackBegin(&spc, pixels_u8, result.width, result.height, 0, 1,
                         0) == 1);
  stbtt_PackSetOversampling(&spc, 2, 2);
  result.range.font_size = font_size;
  result.range.first_unicode_codepoint_in_range = 1;
  result.range.num_chars = 254;
  result.range.chardata_for_range = FL_Arena_PushArray(
      canvas->arena, stbtt_packedchar, result.range.num_chars);
  result.kern_cache = FL_Arena_PushArray(
      canvas->arena, i32, result.range.num_chars * result.range.num_chars);
  {
    stbrp_rect *rects =
        FL_Arena_PushArray(&scratch, stbrp_rect, result.range.num_chars);
    int n =
        stbtt_PackFontRangesGatherRects(&spc, info, &result.range, 1, rects);
    stbtt_PackFontRangesPackRects(&spc, rects, n);
    ASSERT(stbtt_PackFontRangesRenderIntoRects(&spc, info, &result.range, 1,
                                               rects) == 1);
  }
  stbtt_PackEnd(&spc);

  int pw = result.width;
  int ph = result.height;
  u32 *pixels_u32 = FL_Arena_PushArray(&scratch, u32, pw * ph * sizeof(u32));
  u32 *dst_row = pixels_u32;
  u8 *src_row = pixels_u8;
  for (i32 y = 0; y < ph; ++y) {
    u32 *dst = dst_row;
    u8 *src = src_row;
    for (i32 x = 0; x < pw; ++x) {
      u8 alpha = *src++;
      (*dst++) = (((u32)alpha << 24) | ((u32)alpha << 16) | ((u32)alpha << 8) |
                  ((u32)alpha << 0));
    }
    dst_row += pw;
    src_row += pw;
  }

  SDL_Surface *surface =
      SDL_CreateSurfaceFrom(pw, ph, SDL_PIXELFORMAT_ARGB32, pixels_u32, pw * 4);
  result.texture = SDL_CreateTextureFromSurface(canvas->renderer, surface);
  ASSERT(result.texture);
  SDL_DestroySurface(surface);

  return result;
}

static PackedFont *GetOrPackFont(Canvas *canvas, stbtt_fontinfo *font,
                                 f32 font_size) {
  PackedFont *result = 0;
  for (PackedFont *packed_font = canvas->first_packed_font; packed_font;
       packed_font = packed_font->next) {
    if (packed_font->font == font &&
        packed_font->range.font_size == font_size) {
      result = packed_font;
      break;
    }
  }
  if (!result) {
    result = FL_Arena_PushStruct(canvas->arena, PackedFont);
    *result = PackFont(canvas, font, font_size);
    DLL_APPEND(canvas->first_packed_font, canvas->last_packed_font, result,
               prev, next);
  }
  return result;
}

static inline i32 GetCharIndex(PackedFont *packed_font, u32 ch) {
  i32 result =
      i32_clamp(ch - packed_font->range.first_unicode_codepoint_in_range, 0,
                packed_font->range.num_chars - 1);
  return result;
}

static stbtt_aligned_quad inline GetPackedQuadAndAdvancePos(
    PackedFont *packed_font, u32 ch, f32 *pos_x, f32 *baseline) {
  i32 char_index = GetCharIndex(packed_font, ch);
  stbtt_aligned_quad quad;
  stbtt_GetPackedQuad(packed_font->range.chardata_for_range, packed_font->width,
                      packed_font->height, char_index, pos_x, baseline, &quad,
                      0);
  return quad;
}

static i32 GetKernAdvance(PackedFont *packed_font, u32 a, u32 b) {
  i32 char_index_a = GetCharIndex(packed_font, a);
  i32 char_index_b = GetCharIndex(packed_font, b);
  i32 *kern_ptr = packed_font->kern_cache +
                  (char_index_a * packed_font->range.num_chars + char_index_b);
  if (*kern_ptr == 0) {
    i32 kern = stbtt_GetCodepointKernAdvance(packed_font->font, a, b);
    if (kern) {
      *kern_ptr = kern;
    } else {
      *kern_ptr = -1;
    }
  }
  i32 result = i32_max(*kern_ptr, 0);
  return result;
}

static void Canvas_FillText(void *ctx, FL_Str text, FL_f32 x, FL_f32 y,
                            FL_f32 font_size, FL_Color color) {
  Canvas *canvas = ctx;

  f32 content_scale = GetScreenContentScale(canvas);
  Vec2 pos = {x, y};
  pos = vec2_mul(pos, content_scale);

  font_size = font_size * content_scale;
  stbtt_fontinfo *font = &canvas->font;
  PackedFont *packed_font = GetOrPackFont(canvas, font, font_size);
  f32 scale = stbtt_ScaleForPixelHeight(font, font_size);
  i32 ascent, descent, line_gap;
  stbtt_GetFontVMetrics(font, &ascent, &descent, &line_gap);
  f32 line_height = (ascent - descent) * scale;

  FL_Arena scratch = *canvas->arena;
  Str32 text32 = Arena_PushStr32FromStr8(&scratch, text);
  f32 baseline = pos.y;  // + (f32)ascent * scale;
  f32 pos_x = pos.x;
  for (u32 i = 0; i < text32.len; ++i) {
    u32 ch = text32.ptr[i];
    if (ch == '\n') {
      pos_x = pos.x;
      baseline += line_height;
    } else {
      stbtt_aligned_quad quad =
          GetPackedQuadAndAdvancePos(packed_font, ch, &pos_x, &baseline);
      if (i + 1 < text32.len) {
        i32 kern = GetKernAdvance(packed_font, ch, text32.ptr[i + 1]);
        pos_x += scale * kern;
      }

      f32 quad_w = quad.x1 - quad.x0;
      f32 quad_h = quad.y1 - quad.y0;
      if (quad_w > 0 && quad_h > 0) {
        SDL_FRect src_rect;
        src_rect.x = quad.s0 * packed_font->width;
        src_rect.y = quad.t0 * packed_font->height;
        src_rect.w = (quad.s1 - quad.s0) * packed_font->width;
        src_rect.h = (quad.t1 - quad.t0) * packed_font->height;

        SDL_FRect dst_rect;
        dst_rect.x = quad.x0;
        dst_rect.y = quad.y0;
        dst_rect.w = quad_w;
        dst_rect.h = quad_h;

        SDL_SetTextureBlendMode(packed_font->texture,
                                SDL_BLENDMODE_BLEND_PREMULTIPLIED);
        SDL_SetTextureColorModFloat(packed_font->texture, color.r, color.g,
                                    color.b);
        SDL_SetTextureAlphaModFloat(packed_font->texture, color.a);
        SDL_RenderTexture(canvas->renderer, packed_font->texture, &src_rect,
                          &dst_rect);
      }
    }
  }
}

FL_TextMetrics Canvas_MeasureText(void *ctx, FL_Str text, FL_f32 font_size) {
  Canvas *canvas = ctx;

  FL_TextMetrics result = {0};
  f32 content_scale = GetScreenContentScale(canvas);

  font_size = font_size * content_scale;
  stbtt_fontinfo *font = &canvas->font;
  PackedFont *packed_font = GetOrPackFont(canvas, font, font_size);

  f32 scale = stbtt_ScaleForPixelHeight(font, font_size * content_scale);
  i32 ascent, descent, line_gap;
  stbtt_GetFontVMetrics(font, &ascent, &descent, &line_gap);
  f32 line_height = (ascent - descent) * scale;

  FL_Arena scratch = *canvas->arena;
  Str32 text32 = Arena_PushStr32FromStr8(&scratch, text);
  f32 baseline = (f32)ascent * scale;
  f32 pos_x = 0.0f;
  f32 max_pos_x = 0.0f;
  f32 pos_y = 0.0f;
  for (u32 i = 0; i < text32.len; ++i) {
    u32 ch = text32.ptr[i];
    if (ch == '\n') {
      max_pos_x = f32_max(max_pos_x, pos_x);
      pos_x = 0;
      pos_y += line_height;
    } else {
      GetPackedQuadAndAdvancePos(packed_font, ch, &pos_x, &baseline);
      if (i + 1 < text32.len) {
        i32 kern = GetKernAdvance(packed_font, ch, text32.ptr[i + 1]);
        pos_x += scale * kern;
      }
      max_pos_x = f32_max(max_pos_x, pos_x);
    }
  }
  result.width = max_pos_x / content_scale;
  result.font_bounding_box_ascent = baseline / content_scale;
  result.font_bounding_box_descent =
      (pos_y + line_height - baseline) / content_scale;
  return result;
}

FL_Canvas Canvas_Init(SDL_Window *window, SDL_Renderer *renderer) {
  FL_Arena *arena = FL_Arena_Create(&(FL_ArenaOptions){0});
  Canvas *canvas = FL_Arena_PushStruct(arena, Canvas);
  *canvas = (Canvas){
      .arena = arena,
      .window = window,
      .renderer = renderer,
  };
  stbtt_InitFont(&canvas->font, JetBrainsMono_Regular_ttf,
                 stbtt_GetFontOffsetForIndex(JetBrainsMono_Regular_ttf, 0));

  CanvasState *state = FL_Arena_PushStruct(arena, CanvasState);
  *state = (CanvasState){0};
  DLL_APPEND(canvas->first_state, canvas->last_state, state, prev, next);

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
  return (FL_Canvas){
      .ctx = canvas,
      .save = Canvas_Save,
      .restore = Canvas_Restore,
      .clip_rect = Canvas_ClipRect,
      .fill_rect = Canvas_FillRect,
      .stroke_rect = Canvas_StrokeRect,
      .fill_text = Canvas_FillText,
      .measure_text = Canvas_MeasureText,
  };
}
