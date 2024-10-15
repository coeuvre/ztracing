#include "src/draw_sdl3.h"

#include <SDL3/SDL.h>

#include "src/assert.h"
#include "src/draw.h"
#include "src/list.h"
#include "src/math.h"
#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"

#define STB_RECT_PACK_IMPLEMENTATION
#define STBRP_STATIC
#include "third_party/stb/stb_rect_pack.h"

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

typedef struct PackedFont PackedFont;
struct PackedFont {
  PackedFont *prev;
  PackedFont *next;

  stbtt_fontinfo *font;
  SDL_Texture *texture;
  u8 *pixels_u8;
  i32 width;
  i32 height;
  stbtt_pack_range range;
};

typedef struct SDL3DrawState {
  Arena arena;
  SDL_Window *window;
  SDL_Renderer *renderer;

  DrawClipRect *first_clip_rect;
  DrawClipRect *last_clip_rect;
  DrawClipRect *first_free_clip_rect;

  PackedFont *first_packed_font;
  PackedFont *last_packed_font;
} SDL3DrawState;

thread_local SDL3DrawState t_draw_state;

static PackedFont PackFont(Arena *arena, stbtt_fontinfo *info, f32 font_size) {
  TempMemory scratch = BeginScratch(&arena, 1);
  PackedFont result = {0};
  result.font = info;
  result.width = 1024;
  result.height = 1024;
  result.pixels_u8 = (u8 *)AllocMemory(result.width * result.height);
  stbtt_pack_context spc;
  ASSERT(stbtt_PackBegin(&spc, result.pixels_u8, result.width, result.height, 0,
                         1, 0) == 1);
  stbtt_PackSetOversampling(&spc, 2, 2);
  result.range.font_size = font_size;
  result.range.first_unicode_codepoint_in_range = 1;
  result.range.num_chars = 254;
  result.range.chardata_for_range =
      PushArray(arena, stbtt_packedchar, result.range.num_chars);
  {
    stbrp_rect *rects =
        PushArray(scratch.arena, stbrp_rect, result.range.num_chars);
    int n =
        stbtt_PackFontRangesGatherRects(&spc, info, &result.range, 1, rects);
    stbtt_PackFontRangesPackRects(&spc, rects, n);
    ASSERT(stbtt_PackFontRangesRenderIntoRects(&spc, info, &result.range, 1,
                                               rects) == 1);
  }
  stbtt_PackEnd(&spc);
  EndScratch(scratch);
  return result;
}

static PackedFont *GetOrPackFont(stbtt_fontinfo *font, f32 font_size) {
  PackedFont *result = 0;
  for (PackedFont *packed_font = t_draw_state.first_packed_font; packed_font;
       packed_font = packed_font->next) {
    if (packed_font->font == font &&
        packed_font->range.font_size == font_size) {
      result = packed_font;
      break;
    }
  }
  if (!result) {
    result = PushArray(&t_draw_state.arena, PackedFont, 1);
    *result = PackFont(&t_draw_state.arena, font, font_size);
    APPEND_DOUBLY_LINKED_LIST(t_draw_state.first_packed_font,
                              t_draw_state.last_packed_font, result, prev,
                              next);
  }

  return result;
}

void InitDrawSDL3(SDL_Window *window, SDL_Renderer *renderer) {
  t_draw_state.window = window;
  t_draw_state.renderer = renderer;
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
}

f32 GetScreenContentScale(void) {
  f32 result = SDL_GetWindowDisplayScale(t_draw_state.window);
  if (result == 0) {
    result = 1;
  }
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
  min = MulVec2(MaxVec2(min, Vec2Zero()), GetScreenContentScale());
  max = MulVec2(max, GetScreenContentScale());

  DrawClipRect *clip_rect;
  if (t_draw_state.first_free_clip_rect) {
    clip_rect = t_draw_state.first_free_clip_rect;
    t_draw_state.first_free_clip_rect = t_draw_state.first_free_clip_rect->next;
  } else {
    clip_rect = PushArray(&t_draw_state.arena, DrawClipRect, 1);
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

static stbtt_aligned_quad GetPackedQuadAndAdvancePos(PackedFont *packed_font,
                                                     u32 ch, f32 *pos_x,
                                                     f32 *baseline) {
  int char_index =
      ClampI32(ch - packed_font->range.first_unicode_codepoint_in_range, 0,
               packed_font->range.num_chars - 1);
  stbtt_aligned_quad quad;
  stbtt_GetPackedQuad(packed_font->range.chardata_for_range, packed_font->width,
                      packed_font->height, char_index, pos_x, baseline, &quad,
                      0);
  return quad;
}

TextMetrics GetTextMetricsStr8(Str8 text, f32 height) {
  TempMemory scratch = BeginScratch(0, 0);
  TextMetrics result = {0};
  f32 content_scale = GetScreenContentScale();

  f32 font_size = height * content_scale;
  stbtt_fontinfo *font = GetFontInfo();
  PackedFont *packed_font = GetOrPackFont(font, font_size);

  f32 scale = stbtt_ScaleForPixelHeight(font, height * content_scale);
  i32 ascent, descent, line_gap;
  stbtt_GetFontVMetrics(font, &ascent, &descent, &line_gap);
  result.size.y = (ascent - descent) * scale;

  Str32 text32 = PushStr32FromStr8(scratch.arena, text);
  f32 baseline = (f32)ascent * scale;
  f32 pos_x = 0.0f;
  for (u32 i = 0; i < text32.len; ++i) {
    u32 ch = text32.ptr[i];
    GetPackedQuadAndAdvancePos(packed_font, ch, &pos_x, &baseline);
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

void DrawTextStr8(Vec2 pos, Str8 text, f32 height, ColorU32 color) {
  TempMemory scratch = BeginScratch(0, 0);

  Vec4 colorf = LinearColorFromSRGB(color);

  f32 content_scale = GetScreenContentScale();
  pos = MulVec2(pos, content_scale);

  f32 font_size = height * content_scale;
  stbtt_fontinfo *font = GetFontInfo();
  PackedFont *packed_font = GetOrPackFont(font, font_size);
  if (!packed_font->texture) {
    ASSERT(packed_font->pixels_u8);
    int pw = packed_font->width;
    int ph = packed_font->height;
    u32 *pixels_u32 =
        PushArrayNoZero(scratch.arena, u32, pw * ph * sizeof(u32));
    u32 *dst_row = pixels_u32;
    u8 *src_row = packed_font->pixels_u8;
    for (i32 y = 0; y < ph; ++y) {
      u32 *dst = dst_row;
      u8 *src = src_row;
      for (i32 x = 0; x < pw; ++x) {
        u8 alpha = *src++;
        (*dst++) = (((u32)alpha << 24) | ((u32)alpha << 16) |
                    ((u32)alpha << 8) | ((u32)alpha << 0));
      }
      dst_row += pw;
      src_row += pw;
    }

    SDL_Surface *surface = SDL_CreateSurfaceFrom(pw, ph, SDL_PIXELFORMAT_ARGB32,
                                                 pixels_u32, pw * 4);
    packed_font->texture =
        SDL_CreateTextureFromSurface(t_draw_state.renderer, surface);
    ASSERT(packed_font->texture);
    SDL_DestroySurface(surface);
    FreeMemory(packed_font->pixels_u8,
               packed_font->width * packed_font->height);
    packed_font->pixels_u8 = 0;
  }

  f32 scale = stbtt_ScaleForPixelHeight(font, height * content_scale);
  i32 ascent, descent, line_gap;
  stbtt_GetFontVMetrics(font, &ascent, &descent, &line_gap);

  Str32 text32 = PushStr32FromStr8(scratch.arena, text);
  f32 baseline = pos.y + (f32)ascent * scale;
  f32 pos_x = pos.x;
  for (u32 i = 0; i < text32.len; ++i) {
    u32 ch = text32.ptr[i];
    stbtt_aligned_quad quad =
        GetPackedQuadAndAdvancePos(packed_font, ch, &pos_x, &baseline);
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
      SDL_SetTextureColorModFloat(packed_font->texture, colorf.x, colorf.y,
                                  colorf.z);
      SDL_SetTextureAlphaModFloat(packed_font->texture, colorf.w);
      SDL_RenderTexture(t_draw_state.renderer, packed_font->texture, &src_rect,
                        &dst_rect);
    }
    if (i + 1 < text32.len) {
      i32 kern = stbtt_GetCodepointKernAdvance(font, ch, text32.ptr[i + 1]);
      pos_x += scale * kern;
    }
  }

  EndScratch(scratch);
}
