#include "src/draw_sdl3.h"

#include <SDL3/SDL.h>

#include "src/assert.h"
#include "src/draw.h"
#include "src/list.h"
#include "src/math.h"
#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "assets/JetBrainsMono-Regular.h"
#include "third_party/stb/stb_truetype.h"

stbtt_fontinfo g_font;

typedef struct DrawClipRect DrawClipRect;
struct DrawClipRect {
  DrawClipRect *prev;
  DrawClipRect *next;
  SDL_Rect rect;
};

typedef struct SDL3DrawState {
  Arena *arena;
  SDL_Window *window;
  SDL_Renderer *renderer;

  DrawClipRect *first_clip_rect;
  DrawClipRect *last_clip_rect;
  DrawClipRect *first_free_clip_rect;
} SDL3DrawState;

thread_local SDL3DrawState t_draw_state;

void InitDrawSDL3(SDL_Window *window, SDL_Renderer *renderer) {
  t_draw_state.arena = AllocArena();
  t_draw_state.window = window;
  t_draw_state.renderer = renderer;
}

f32 GetScreenContentScale(void) {
  f32 result = SDL_GetWindowDisplayScale(t_draw_state.window);
  return result;
}

Vec2 GetScreenSize(void) {
  Vec2I screen_size_in_pixel = {0};
  SDL_GetCurrentRenderOutputSize(t_draw_state.renderer, &screen_size_in_pixel.x,
                                 &screen_size_in_pixel.y);
  Vec2 result = MulVec2(Vec2FromVec2I(screen_size_in_pixel),
                        1.0f / GetScreenContentScale());
  return result;
}

void PushClipRect(Vec2 min, Vec2 max) {
  min = MulVec2(min, GetScreenContentScale());
  max = MulVec2(max, GetScreenContentScale());

  DrawClipRect *clip_rect;
  if (t_draw_state.first_free_clip_rect) {
    clip_rect = t_draw_state.first_free_clip_rect;
    t_draw_state.first_free_clip_rect = t_draw_state.first_free_clip_rect->next;
  } else {
    clip_rect = PushArray(t_draw_state.arena, DrawClipRect, 1);
  }
  SDL_Rect rect;
  rect.x = min.x;
  rect.y = min.y;
  rect.w = max.x - min.x;
  rect.h = max.y - min.y;
  clip_rect->rect = rect;
  APPEND_DOUBLY_LINKED_LIST(t_draw_state.first_clip_rect,
                            t_draw_state.last_clip_rect, clip_rect, prev, next);
  SDL_SetRenderClipRect(t_draw_state.renderer, &rect);
}

void PopClipRect(void) {
  ASSERT(t_draw_state.last_clip_rect);
  DrawClipRect *free_clip_rect = t_draw_state.last_clip_rect;
  REMOVE_DOUBLY_LINKED_LIST(t_draw_state.first_clip_rect,
                            t_draw_state.last_clip_rect, free_clip_rect, prev,
                            next);
  free_clip_rect->next = t_draw_state.first_free_clip_rect;
  t_draw_state.first_free_clip_rect = free_clip_rect;

  SDL_Rect *rect = 0;
  if (t_draw_state.last_clip_rect) {
    rect = &t_draw_state.last_clip_rect->rect;
  }
  SDL_SetRenderClipRect(t_draw_state.renderer, rect);
}

void ClearDraw(void) {
  SDL_SetRenderDrawColor(t_draw_state.renderer, 0, 0, 0, 0);
  SDL_RenderClear(t_draw_state.renderer);
}

void PresentDraw(void) { SDL_RenderPresent(t_draw_state.renderer); }

void DrawRect(Vec2 min, Vec2 max, ColorU32 color) {
  min = MulVec2(min, GetScreenContentScale());
  max = MulVec2(max, GetScreenContentScale());

  SDL_SetRenderDrawColor(t_draw_state.renderer, color.r, color.g, color.b,
                         color.a);
  SDL_FRect rect;
  rect.x = min.x;
  rect.y = min.y;
  rect.w = max.x - min.x;
  rect.h = max.y - min.y;
  SDL_RenderFillRect(t_draw_state.renderer, &rect);
}

static stbtt_fontinfo *GetFontInfo(void) {
  if (!g_font.data) {
    stbtt_InitFont(&g_font, JetBrainsMono_Regular_ttf,
                   stbtt_GetFontOffsetForIndex(JetBrainsMono_Regular_ttf, 0));
  }
  return &g_font;
}

TextMetrics GetTextMetricsStr8(Str8 text, f32 height) {
  TempMemory scratch = BeginScratch(0, 0);
  TextMetrics result = {0};
  f32 content_scale = GetScreenContentScale();

  stbtt_fontinfo *font = GetFontInfo();
  f32 scale = stbtt_ScaleForPixelHeight(font, height * content_scale);
  i32 ascent, descent, line_gap;
  stbtt_GetFontVMetrics(font, &ascent, &descent, &line_gap);
  result.size.y = (ascent - descent) * scale;

  Str32 text32 = PushStr32FromStr8(scratch.arena, text);
  f32 pos_x = 0.0f;
  for (u32 i = 0; i < text32.len; ++i) {
    Vec2I min, max;
    i32 advance, lsb;
    u32 ch = text32.ptr[i];
    i32 glyph = stbtt_FindGlyphIndex(font, ch);
    stbtt_GetGlyphHMetrics(font, glyph, &advance, &lsb);
    stbtt_GetGlyphBitmapBox(font, glyph, scale, scale, &min.x, &min.y, &max.x,
                            &max.y);
    pos_x += advance * scale;
    if (i + 1 < text32.len) {
      i32 kern = stbtt_GetCodepointKernAdvance(font, ch, text32.ptr[i + 1]);
      pos_x += scale * kern;
    }
  }
  result.size.x = pos_x;
  result.size = MulVec2(result.size, 1.0f / content_scale);

  EndScratch(scratch);
  return result;
}

void DrawTextStr8(Vec2 pos, Str8 text, f32 height) {
  TempMemory scratch = BeginScratch(0, 0);

  f32 content_scale = GetScreenContentScale();
  pos = MulVec2(pos, content_scale);

  stbtt_fontinfo *font = GetFontInfo();
  f32 scale = stbtt_ScaleForPixelHeight(font, height * content_scale);
  i32 ascent, descent, line_gap;
  stbtt_GetFontVMetrics(font, &ascent, &descent, &line_gap);

  Str32 text32 = PushStr32FromStr8(scratch.arena, text);
  i32 baseline = (i32)(pos.y + ascent * scale);
  f32 pos_x = pos.x;
  for (u32 i = 0; i < text32.len; ++i) {
    Vec2I min, max;
    i32 advance, lsb;
    u32 ch = text32.ptr[i];
    i32 glyph = stbtt_FindGlyphIndex(font, ch);
    stbtt_GetGlyphHMetrics(font, glyph, &advance, &lsb);
    stbtt_GetGlyphBitmapBox(font, glyph, scale, scale, &min.x, &min.y, &max.x,
                            &max.y);
    Vec2I glyph_size = SubVec2I(max, min);
    u8 *pixels_a8 = PushArray(scratch.arena, u8, glyph_size.x * glyph_size.y);
    u32 *pixels_argb8888 =
        PushArray(scratch.arena, u32, glyph_size.x * glyph_size.y);
    ASSERT(pixels_a8 && pixels_argb8888);
    stbtt_MakeGlyphBitmap(font, pixels_a8, glyph_size.x, glyph_size.y,
                          glyph_size.x, scale, scale, glyph);

    u32 *dst_row = pixels_argb8888;
    u8 *src_row = pixels_a8;
    for (i32 y = 0; y < glyph_size.y; ++y) {
      u32 *dst = dst_row;
      u8 *src = src_row;
      for (i32 x = 0; x < glyph_size.x; ++x) {
        u8 alpha = *src++;
        (*dst++) = (((u32)alpha << 24) | ((u32)alpha << 16) |
                    ((u32)alpha << 8) | ((u32)alpha << 0));
      }
      dst_row += glyph_size.x;
      src_row += glyph_size.x;
    }

    // TODO: font cache/atlas
    if (glyph_size.x > 0 && glyph_size.y > 0) {
      SDL_Surface *surface = SDL_CreateSurfaceFrom(
          glyph_size.x, glyph_size.y, SDL_PIXELFORMAT_ARGB8888, pixels_argb8888,
          glyph_size.x * 4);
      SDL_Texture *texture =
          SDL_CreateTextureFromSurface(t_draw_state.renderer, surface);
      ASSERT(texture);
      SDL_DestroySurface(surface);

      SDL_FRect src_rect;
      src_rect.x = 0;
      src_rect.y = 0;
      src_rect.w = glyph_size.x;
      src_rect.h = glyph_size.y;

      SDL_FRect dst_rect;
      dst_rect.x = pos_x + min.x;
      dst_rect.y = (f32)baseline + min.y;
      dst_rect.w = glyph_size.x;
      dst_rect.h = glyph_size.y;

      SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
      SDL_RenderTexture(t_draw_state.renderer, texture, &src_rect, &dst_rect);
      SDL_DestroyTexture(texture);
    }

    pos_x += advance * scale;
    if (i + 1 < text32.len) {
      i32 kern = stbtt_GetCodepointKernAdvance(font, ch, text32.ptr[i + 1]);
      pos_x += scale * kern;
    }
  }

  EndScratch(scratch);
}
