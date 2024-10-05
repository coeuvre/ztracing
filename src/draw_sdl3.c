#include "src/draw_sdl3.h"

#include <SDL3/SDL.h>

#include "src/assert.h"
#include "src/draw.h"
#include "src/math.h"
#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "assets/JetBrainsMono-Regular.h"
#include "third_party/stb/stb_truetype.h"

stbtt_fontinfo g_font;

static SDL_Window *g_window;
static SDL_Renderer *g_renderer;

void InitDrawSDL3(SDL_Window *window, SDL_Renderer *renderer) {
  g_window = window;
  g_renderer = renderer;
  stbtt_InitFont(&g_font, JetBrainsMono_Regular_ttf,
                 stbtt_GetFontOffsetForIndex(JetBrainsMono_Regular_ttf, 0));
}

f32 GetDrawContentScale(void) {
  f32 result = SDL_GetWindowDisplayScale(g_window);
  return result;
}

Vec2I GetDrawOutputSize(void) {
  Vec2I result = {0};
  SDL_GetCurrentRenderOutputSize(g_renderer, &result.x, &result.y);
  return result;
}

void ClearDraw(void) {
  SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 0);
  SDL_RenderClear(g_renderer);
}

void PresentDraw(void) { SDL_RenderPresent(g_renderer); }

void DrawRect(Vec2 min, Vec2 max, DrawColor color) {
  SDL_SetRenderDrawColor(g_renderer, color.r, color.g, color.b, color.a);
  SDL_FRect rect;
  rect.x = min.x;
  rect.y = min.y;
  rect.w = max.x - min.x;
  rect.h = max.y - min.y;
  SDL_RenderFillRect(g_renderer, &rect);
}

TextMetrics GetTextMetricsStr8(Str8 text, f32 height) {
  TempMemory scratch = BeginScratch(0, 0);
  TextMetrics result = {0};

  f32 scale = stbtt_ScaleForPixelHeight(&g_font, height);
  i32 ascent, descent, line_gap;
  stbtt_GetFontVMetrics(&g_font, &ascent, &descent, &line_gap);
  result.size.y = (ascent - descent) * scale;

  Str32 text32 = PushStr32FromStr8(scratch.arena, text);
  f32 pos_x = 0.0f;
  for (u32 i = 0; i < text32.len; ++i) {
    Vec2I min, max;
    i32 advance, lsb;
    u32 ch = text32.ptr[i];
    i32 glyph = stbtt_FindGlyphIndex(&g_font, ch);
    stbtt_GetGlyphHMetrics(&g_font, glyph, &advance, &lsb);
    stbtt_GetGlyphBitmapBox(&g_font, glyph, scale, scale, &min.x, &min.y,
                            &max.x, &max.y);
    pos_x += advance * scale;
    if (i + 1 < text32.len) {
      i32 kern = stbtt_GetCodepointKernAdvance(&g_font, ch, text32.ptr[i + 1]);
      pos_x += scale * kern;
    }
  }
  result.size.x = pos_x;

  EndScratch(scratch);
  return result;
}

void DrawTextStr8(Vec2 pos, Str8 text, f32 height) {
  TempMemory scratch = BeginScratch(0, 0);

  f32 scale = stbtt_ScaleForPixelHeight(&g_font, height);
  i32 ascent, descent, line_gap;
  stbtt_GetFontVMetrics(&g_font, &ascent, &descent, &line_gap);

  Str32 text32 = PushStr32FromStr8(scratch.arena, text);
  i32 baseline = (i32)(pos.y + ascent * scale);
  f32 pos_x = pos.x;
  for (u32 i = 0; i < text32.len; ++i) {
    Vec2I min, max;
    i32 advance, lsb;
    u32 ch = text32.ptr[i];
    i32 glyph = stbtt_FindGlyphIndex(&g_font, ch);
    stbtt_GetGlyphHMetrics(&g_font, glyph, &advance, &lsb);
    stbtt_GetGlyphBitmapBox(&g_font, glyph, scale, scale, &min.x, &min.y,
                            &max.x, &max.y);
    Vec2I glyph_size = SubVec2I(max, min);
    u8 *pixels_a8 = PushArray(scratch.arena, u8, glyph_size.x * glyph_size.y);
    u32 *pixels_argb8888 =
        PushArray(scratch.arena, u32, glyph_size.x * glyph_size.y);
    ASSERT(pixels_a8 && pixels_argb8888);
    stbtt_MakeGlyphBitmap(&g_font, pixels_a8, glyph_size.x, glyph_size.y,
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
    SDL_Surface *surface = SDL_CreateSurfaceFrom(
        glyph_size.x, glyph_size.y, SDL_PIXELFORMAT_ARGB8888, pixels_argb8888,
        glyph_size.x * 4);
    SDL_Texture *texture = SDL_CreateTextureFromSurface(g_renderer, surface);
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
    SDL_RenderTexture(g_renderer, texture, &src_rect, &dst_rect);
    SDL_DestroyTexture(texture);

    pos_x += advance * scale;
    if (i + 1 < text32.len) {
      i32 kern = stbtt_GetCodepointKernAdvance(&g_font, ch, text32.ptr[i + 1]);
      pos_x += scale * kern;
    }
  }

  EndScratch(scratch);
}
