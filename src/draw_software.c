#include "src/draw_software.h"

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

Arena *framebuffer_arena;
Bitmap *framebuffer;
stbtt_fontinfo font;

static Bitmap *PushBitmap(Arena *arena, Vec2I size) {
  Bitmap *bitmap = PushArray(arena, Bitmap, 1);
  if (size.x > 0 && size.y > 0) {
    bitmap->pixels = PushArray(arena, u32, size.x * size.y);
    bitmap->size = size;
  }
  return bitmap;
}

Bitmap *InitSoftwareRenderer(void) {
  i32 ret =
      stbtt_InitFont(&font, JetBrainsMono_Regular_ttf,
                     stbtt_GetFontOffsetForIndex(JetBrainsMono_Regular_ttf, 0));
  ASSERT(ret != 0);

  framebuffer_arena = AllocArena();
  framebuffer = PushBitmap(framebuffer_arena, GetCanvasSize());
  return framebuffer;
}

Bitmap *ResizeSoftwareRenderer(Vec2I size) {
  FreeArena(framebuffer_arena);
  framebuffer_arena = AllocArena();
  framebuffer = PushBitmap(framebuffer_arena, size);
  return framebuffer;
}

void ClearCanvas(void) {
  ZeroMemory(framebuffer->pixels,
             framebuffer->size.x * framebuffer->size.y * 4);
}

static void CopyBitmap(Bitmap *dst, Vec2 pos, Bitmap *src) {
  Vec2I offset = Vec2IFromVec2(RoundVec2(pos));
  Vec2I src_min = MaxVec2I(NegVec2I(offset), (Vec2I){0, 0});
  Vec2I src_max =
      SubVec2I(MinVec2I(AddVec2I(offset, src->size), dst->size), offset);

  Vec2I dst_min = MaxVec2I(offset, (Vec2I){0, 0});
  Vec2I dst_max = AddVec2I(dst_min, SubVec2I(src_max, src_min));

  u32 *dst_row = dst->pixels + dst->size.x * dst_min.y + dst_min.x;
  u32 *src_row = src->pixels + src->size.x * src_min.y + src_min.x;
  for (i32 y = dst_min.y; y < dst_max.y; ++y) {
    u32 *dst_col = dst_row;
    u32 *src_col = src_row;
    for (i32 x = dst_min.x; x < dst_max.x; ++x) {
      // u32 dst_pixel = *dst_col;
      // u32 src_pixel = *src_col;
      // *dst_col = blend_color(dst, src_col);
      *dst_col = *src_col;
      dst_col++;
      src_col++;
    }
    dst_row += dst->size.x;
    src_row += src->size.x;
  }
}

TextMetrics GetTextMetricsStr8(Str8 text, f32 height) {
  TempMemory scratch = BeginScratch(0, 0);
  TextMetrics result = {0};

  f32 scale = stbtt_ScaleForPixelHeight(&font, height);
  i32 ascent, descent, line_gap;
  stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);
  result.size.y = (ascent - descent) * scale;

  Str32 text32 = PushStr32FromStr8(scratch.arena, text);
  f32 pos_x = 0.0f;
  for (u32 i = 0; i < text32.len; ++i) {
    Vec2I min, max;
    i32 advance, lsb;
    u32 ch = text32.ptr[i];
    i32 glyph = stbtt_FindGlyphIndex(&font, ch);
    stbtt_GetGlyphHMetrics(&font, glyph, &advance, &lsb);
    stbtt_GetGlyphBitmapBox(&font, glyph, scale, scale, &min.x, &min.y, &max.x,
                            &max.y);
    pos_x += advance * scale;
    if (i + 1 < text32.len) {
      i32 kern = stbtt_GetCodepointKernAdvance(&font, ch, text32.ptr[i + 1]);
      pos_x += scale * kern;
    }
  }
  result.size.x = pos_x;

  EndScratch(scratch);
  return result;
}

void DrawTextStr8(Vec2 pos, Str8 text, f32 height) {
  TempMemory scratch = BeginScratch(0, 0);

  f32 scale = stbtt_ScaleForPixelHeight(&font, height);
  i32 ascent, descent, line_gap;
  stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);

  Str32 text32 = PushStr32FromStr8(scratch.arena, text);
  i32 baseline = (i32)(pos.y + ascent * scale);
  f32 pos_x = pos.x;
  for (u32 i = 0; i < text32.len; ++i) {
    Vec2I min, max;
    i32 advance, lsb;
    u32 ch = text32.ptr[i];
    i32 glyph = stbtt_FindGlyphIndex(&font, ch);
    stbtt_GetGlyphHMetrics(&font, glyph, &advance, &lsb);
    stbtt_GetGlyphBitmapBox(&font, glyph, scale, scale, &min.x, &min.y, &max.x,
                            &max.y);
    Vec2I glyph_size = SubVec2I(max, min);
    u8 *out = PushArray(scratch.arena, u8, glyph_size.x * glyph_size.y);
    ASSERT(out);
    stbtt_MakeGlyphBitmap(&font, out, glyph_size.x, glyph_size.y, glyph_size.x,
                          scale, scale, glyph);

    Bitmap *glyph_bitmap = PushBitmap(scratch.arena, glyph_size);
    u32 *dst_row = glyph_bitmap->pixels;
    u8 *src_row = out;
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
    CopyBitmap(framebuffer, (Vec2){pos_x + min.x, (f32)baseline + min.y},
               glyph_bitmap);

    pos_x += advance * scale;
    if (i + 1 < text32.len) {
      i32 kern = stbtt_GetCodepointKernAdvance(&font, ch, text32.ptr[i + 1]);
      pos_x += scale * kern;
    }
  }

  EndScratch(scratch);
}

void DrawRect(Vec2 min, Vec2 max, u32 color) {
  Bitmap *bitmap = framebuffer;
  Vec2 fb_min = {0.0f, 0.0f};
  Vec2 fb_max = Vec2FromVec2I(bitmap->size);
  min = ClampVec2(min, fb_min, fb_max);
  max = ClampVec2(max, fb_min, fb_max);
  i32 min_x = RoundF32(min.x);
  i32 min_y = RoundF32(min.y);
  i32 max_x = RoundF32(max.x);
  i32 max_y = RoundF32(max.y);
  for (i32 y = min_y; y < max_y; ++y) {
    u32 *row = bitmap->pixels + bitmap->size.x * y;
    for (i32 x = min_x; x < max_x; ++x) {
      row[x] = color;
    }
  }
}

void DrawRectLine(Vec2 min, Vec2 max, u32 color, f32 thickness) {
  DrawRect(min, V2(max.x, min.y + thickness), color);
  DrawRect(V2(min.x, min.y + thickness), V2(min.x + thickness, max.y), color);
  DrawRect(V2(max.x - thickness, min.y + thickness), V2(max.x, max.y), color);
  DrawRect(V2(min.x + thickness, max.y - thickness),
           V2(max.x - thickness, max.y), color);
}
