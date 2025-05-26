#include "flick.h"

#define FL_LIST_ADD_FIRST(first, node, next) \
  do {                                       \
    (node)->next = first;                    \
    (first) = node;                          \
  } while (0)

#define FL_LIST_REMOVE_FIRST(first, next) \
  do {                                    \
    (first) = (first)->next;              \
  } while (0)

#define FL_DLIST_ADD_LAST(first, last, node, prev, next) \
  do {                                                   \
    if (last) {                                          \
      (node)->prev = (last);                             \
      (node)->next = 0;                                  \
      (last)->next = (node);                             \
      (last) = (node);                                   \
    } else {                                             \
      (first) = (last) = (node);                         \
      (node)->prev = (node)->next = 0;                   \
    }                                                    \
  } while (0)

#define FL_DLIST_REMOVE_FIRST(first, last, prev, next) \
  do {                                                 \
    (first) = (first)->next;                           \
    if (!(first)) {                                    \
      (last) = 0;                                      \
    }                                                  \
  } while (0)

#define FL_DLIST_REMOVE(first, last, node, prev, next) \
  do {                                                 \
    if ((first) == (node)) {                           \
      (first) = (node)->next;                          \
    }                                                  \
    if ((last) == (node)) {                            \
      (last) = (node)->prev;                           \
    }                                                  \
    if ((node)->next) {                                \
      (node)->next->prev = (node)->prev;               \
    }                                                  \
    if ((node)->prev) {                                \
      (node)->prev->next = (node)->next;               \
    }                                                  \
    (node)->prev = (node)->next = 0;                   \
  } while (0)

#define FL_DLIST_CONCAT(first1, last1, first2, last2, prev, next) \
  do {                                                            \
    if (last1) {                                                  \
      (last1)->next = first2;                                     \
      (last1) = last2;                                            \
    } else {                                                      \
      first1 = first2;                                            \
      last1 = last2;                                              \
    }                                                             \
  } while (0)


#define FL_HASH_TRIE(Name, Key, Key_Hash, Key_IsEqual, Value)            \
  typedef struct Name Name;                                              \
  struct Name {                                                          \
    Name *slots[4];                                                      \
    Key key;                                                             \
    Value value;                                                         \
  };                                                                     \
                                                                         \
  static inline bool Name##_Upsert(Name **t, Key key, Value **out_value, \
                                   FL_Arena *arena) {                    \
    bool found = false;                                                  \
    Value *value = 0;                                                    \
    for (uint64_t hash = Key_Hash(key); *t; hash <<= 2) {                \
      if (Key_IsEqual(key, (*t)->key)) {                                 \
        found = true;                                                    \
        value = &(*t)->value;                                            \
        break;                                                           \
      }                                                                  \
      t = (*t)->slots + (hash >> 62);                                    \
    }                                                                    \
                                                                         \
    if (!found && arena) {                                               \
      Name *slot = (Name *)FL_Arena_PushStruct(arena, Name);             \
      *slot = (Name){.key = key};                                        \
      *t = slot;                                                         \
      value = &slot->value;                                              \
    }                                                                    \
    *out_value = value;                                                  \
    return !found;                                                       \
  }

#include <stdbool.h>
#include <stdint.h>


typedef struct FL_PointerEventResolver {
  FL_Widget *widget;
} FL_PointerEventResolver;

void FL_PointerEventResolver_Reset(FL_PointerEventResolver *resolver);

/** Represents an object participating in an arena. */
typedef struct FL_GestureArenaMember {
  void *ptr;
  FL_GestureArenaMemberOps *ops;
} FL_GestureArenaMember;

typedef struct FL_GestureArena FL_GestureArena;

typedef struct FL_GestureArenaEntry FL_GestureArenaEntry;
struct FL_GestureArenaEntry {
  FL_GestureArenaEntry *prev;
  FL_GestureArenaEntry *next;
  FL_GestureArena *arena;
  FL_GestureArenaMember member;
  bool active;
};

typedef struct FL_GestureArenaState FL_GestureArenaState;

struct FL_GestureArena {
  FL_GestureArena *prev;
  FL_GestureArena *next;

  FL_GestureArenaState *state;
  FL_GestureArenaEntry *first;
  FL_GestureArenaEntry *last;
  FL_GestureArenaEntry *eager_winner;

  FL_i32 pointer;
  bool open;
};

struct FL_GestureArenaState {
  FL_GestureArena *first_arena;
  FL_GestureArena *last_arena;
  FL_GestureArena *first_free_arena;
  FL_GestureArena *last_free_arena;

  FL_GestureArenaEntry *first_free_entry;
  FL_GestureArenaEntry *last_free_entry;
};

FL_GestureArena *FL_GestureArena_Open(FL_GestureArenaState *state,
                                      FL_i32 pointer, FL_Arena *arena);

/**
 * Prevents new members from entering the arena.
 */
void FL_GestureArena_Close(FL_GestureArena *arena);

FL_GestureArena *FL_GestureArena_Get(FL_GestureArenaState *state,
                                     FL_i32 pointer);

/**
 * Forces resolution of the arena, giving the win to the first member.
 *
 * Sweep is typically after all the other processing for a `FLPointerUpEvent`
 * have taken place. It ensures that multiple passive gestures do not cause a
 * stalemate that prevents the user from interacting with the app.
 *
 * Recognizers that wish to delay resolving an arena past `FLPointerUpEvent`
 * should call `FL_GestureArena_Hold` to delay sweep until
 * `FL_GestureArena_Release` is called.
 */
void FL_GestureArena_Sweep(FL_GestureArena *arena);

void FL_Widget_Unmount(FL_Widget *widget);
#define STB_RECT_PACK_IMPLEMENTATION

// stb_rect_pack.h - v1.01 - public domain - rectangle packing
// Sean Barrett 2014
//
// Useful for e.g. packing rectangular textures into an atlas.
// Does not do rotation.
//
// Before #including,
//
//    #define STB_RECT_PACK_IMPLEMENTATION
//
// in the file that you want to have the implementation.
//
// Not necessarily the awesomest packing method, but better than
// the totally naive one in stb_truetype (which is primarily what
// this is meant to replace).
//
// Has only had a few tests run, may have issues.
//
// More docs to come.
//
// No memory allocations; uses qsort() and assert() from stdlib.
// Can override those by defining STBRP_SORT and STBRP_ASSERT.
//
// This library currently uses the Skyline Bottom-Left algorithm.
//
// Please note: better rectangle packers are welcome! Please
// implement them to the same API, but with a different init
// function.
//
// Credits
//
//  Library
//    Sean Barrett
//  Minor features
//    Martins Mozeiko
//    github:IntellectualKitty
//
//  Bugfixes / warning fixes
//    Jeremy Jaussaud
//    Fabian Giesen
//
// Version history:
//
//     1.01  (2021-07-11)  always use large rect mode, expose STBRP__MAXVAL in public section
//     1.00  (2019-02-25)  avoid small space waste; gracefully fail too-wide rectangles
//     0.99  (2019-02-07)  warning fixes
//     0.11  (2017-03-03)  return packing success/fail result
//     0.10  (2016-10-25)  remove cast-away-const to avoid warnings
//     0.09  (2016-08-27)  fix compiler warnings
//     0.08  (2015-09-13)  really fix bug with empty rects (w=0 or h=0)
//     0.07  (2015-09-13)  fix bug with empty rects (w=0 or h=0)
//     0.06  (2015-04-15)  added STBRP_SORT to allow replacing qsort
//     0.05:  added STBRP_ASSERT to allow replacing assert
//     0.04:  fixed minor bug in STBRP_LARGE_RECTS support
//     0.01:  initial release
//
// LICENSE
//
//   See end of file for license information.

//////////////////////////////////////////////////////////////////////////////
//
//       INCLUDE SECTION
//

#ifndef STB_INCLUDE_STB_RECT_PACK_H
#define STB_INCLUDE_STB_RECT_PACK_H

#define STB_RECT_PACK_VERSION  1

#ifdef STBRP_STATIC
#define STBRP_DEF static
#else
#define STBRP_DEF extern
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct stbrp_context stbrp_context;
typedef struct stbrp_node    stbrp_node;
typedef struct stbrp_rect    stbrp_rect;

typedef int            stbrp_coord;

#define STBRP__MAXVAL  0x7fffffff
// Mostly for internal use, but this is the maximum supported coordinate value.

STBRP_DEF int stbrp_pack_rects (stbrp_context *context, stbrp_rect *rects, int num_rects);
// Assign packed locations to rectangles. The rectangles are of type
// 'stbrp_rect' defined below, stored in the array 'rects', and there
// are 'num_rects' many of them.
//
// Rectangles which are successfully packed have the 'was_packed' flag
// set to a non-zero value and 'x' and 'y' store the minimum location
// on each axis (i.e. bottom-left in cartesian coordinates, top-left
// if you imagine y increasing downwards). Rectangles which do not fit
// have the 'was_packed' flag set to 0.
//
// You should not try to access the 'rects' array from another thread
// while this function is running, as the function temporarily reorders
// the array while it executes.
//
// To pack into another rectangle, you need to call stbrp_init_target
// again. To continue packing into the same rectangle, you can call
// this function again. Calling this multiple times with multiple rect
// arrays will probably produce worse packing results than calling it
// a single time with the full rectangle array, but the option is
// available.
//
// The function returns 1 if all of the rectangles were successfully
// packed and 0 otherwise.

struct stbrp_rect
{
   // reserved for your use:
   int            id;

   // input:
   stbrp_coord    w, h;

   // output:
   stbrp_coord    x, y;
   int            was_packed;  // non-zero if valid packing

}; // 16 bytes, nominally


STBRP_DEF void stbrp_init_target (stbrp_context *context, int width, int height, stbrp_node *nodes, int num_nodes);
// Initialize a rectangle packer to:
//    pack a rectangle that is 'width' by 'height' in dimensions
//    using temporary storage provided by the array 'nodes', which is 'num_nodes' long
//
// You must call this function every time you start packing into a new target.
//
// There is no "shutdown" function. The 'nodes' memory must stay valid for
// the following stbrp_pack_rects() call (or calls), but can be freed after
// the call (or calls) finish.
//
// Note: to guarantee best results, either:
//       1. make sure 'num_nodes' >= 'width'
//   or  2. call stbrp_allow_out_of_mem() defined below with 'allow_out_of_mem = 1'
//
// If you don't do either of the above things, widths will be quantized to multiples
// of small integers to guarantee the algorithm doesn't run out of temporary storage.
//
// If you do #2, then the non-quantized algorithm will be used, but the algorithm
// may run out of temporary storage and be unable to pack some rectangles.

STBRP_DEF void stbrp_setup_allow_out_of_mem (stbrp_context *context, int allow_out_of_mem);
// Optionally call this function after init but before doing any packing to
// change the handling of the out-of-temp-memory scenario, described above.
// If you call init again, this will be reset to the default (false).


STBRP_DEF void stbrp_setup_heuristic (stbrp_context *context, int heuristic);
// Optionally select which packing heuristic the library should use. Different
// heuristics will produce better/worse results for different data sets.
// If you call init again, this will be reset to the default.

enum
{
   STBRP_HEURISTIC_Skyline_default=0,
   STBRP_HEURISTIC_Skyline_BL_sortHeight = STBRP_HEURISTIC_Skyline_default,
   STBRP_HEURISTIC_Skyline_BF_sortHeight
};


//////////////////////////////////////////////////////////////////////////////
//
// the details of the following structures don't matter to you, but they must
// be visible so you can handle the memory allocations for them

struct stbrp_node
{
   stbrp_coord  x,y;
   stbrp_node  *next;
};

struct stbrp_context
{
   int width;
   int height;
   int align;
   int init_mode;
   int heuristic;
   int num_nodes;
   stbrp_node *active_head;
   stbrp_node *free_head;
   stbrp_node extra[2]; // we allocate two extra nodes so optimal user-node-count is 'width' not 'width+2'
};

#ifdef __cplusplus
}
#endif

#endif

/*
------------------------------------------------------------------------------
This software is available under 2 licenses -- choose whichever you prefer.
------------------------------------------------------------------------------
ALTERNATIVE A - MIT License
Copyright (c) 2017 Sean Barrett
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
------------------------------------------------------------------------------
ALTERNATIVE B - Public Domain (www.unlicense.org)
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.
In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
------------------------------------------------------------------------------
*/

#define STB_TRUETYPE_IMPLEMENTATION

// stb_truetype.h - v1.26 - public domain
// authored from 2009-2021 by Sean Barrett / RAD Game Tools
//
// =======================================================================
//
//    NO SECURITY GUARANTEE -- DO NOT USE THIS ON UNTRUSTED FONT FILES
//
// This library does no range checking of the offsets found in the file,
// meaning an attacker can use it to read arbitrary memory.
//
// =======================================================================
//
//   This library processes TrueType files:
//        parse files
//        extract glyph metrics
//        extract glyph shapes
//        render glyphs to one-channel bitmaps with antialiasing (box filter)
//        render glyphs to one-channel SDF bitmaps (signed-distance field/function)
//
//   Todo:
//        non-MS cmaps
//        crashproof on bad data
//        hinting? (no longer patented)
//        cleartype-style AA?
//        optimize: use simple memory allocator for intermediates
//        optimize: build edge-list directly from curves
//        optimize: rasterize directly from curves?
//
// ADDITIONAL CONTRIBUTORS
//
//   Mikko Mononen: compound shape support, more cmap formats
//   Tor Andersson: kerning, subpixel rendering
//   Dougall Johnson: OpenType / Type 2 font handling
//   Daniel Ribeiro Maciel: basic GPOS-based kerning
//
//   Misc other:
//       Ryan Gordon
//       Simon Glass
//       github:IntellectualKitty
//       Imanol Celaya
//       Daniel Ribeiro Maciel
//
//   Bug/warning reports/fixes:
//       "Zer" on mollyrocket       Fabian "ryg" Giesen   github:NiLuJe
//       Cass Everitt               Martins Mozeiko       github:aloucks
//       stoiko (Haemimont Games)   Cap Petschulat        github:oyvindjam
//       Brian Hook                 Omar Cornut           github:vassvik
//       Walter van Niftrik         Ryan Griege
//       David Gow                  Peter LaValle
//       David Given                Sergey Popov
//       Ivan-Assen Ivanov          Giumo X. Clanjor
//       Anthony Pesch              Higor Euripedes
//       Johan Duparc               Thomas Fields
//       Hou Qiming                 Derek Vinyard
//       Rob Loach                  Cort Stratton
//       Kenney Phillis Jr.         Brian Costabile
//       Ken Voskuil (kaesve)       Yakov Galka
//
// VERSION HISTORY
//
//   1.26 (2021-08-28) fix broken rasterizer
//   1.25 (2021-07-11) many fixes
//   1.24 (2020-02-05) fix warning
//   1.23 (2020-02-02) query SVG data for glyphs; query whole kerning table (but only kern not GPOS)
//   1.22 (2019-08-11) minimize missing-glyph duplication; fix kerning if both 'GPOS' and 'kern' are defined
//   1.21 (2019-02-25) fix warning
//   1.20 (2019-02-07) PackFontRange skips missing codepoints; GetScaleFontVMetrics()
//   1.19 (2018-02-11) GPOS kerning, STBTT_fmod
//   1.18 (2018-01-29) add missing function
//   1.17 (2017-07-23) make more arguments const; doc fix
//   1.16 (2017-07-12) SDF support
//   1.15 (2017-03-03) make more arguments const
//   1.14 (2017-01-16) num-fonts-in-TTC function
//   1.13 (2017-01-02) support OpenType fonts, certain Apple fonts
//   1.12 (2016-10-25) suppress warnings about casting away const with -Wcast-qual
//   1.11 (2016-04-02) fix unused-variable warning
//   1.10 (2016-04-02) user-defined fabs(); rare memory leak; remove duplicate typedef
//   1.09 (2016-01-16) warning fix; avoid crash on outofmem; use allocation userdata properly
//   1.08 (2015-09-13) document stbtt_Rasterize(); fixes for vertical & horizontal edges
//   1.07 (2015-08-01) allow PackFontRanges to accept arrays of sparse codepoints;
//                     variant PackFontRanges to pack and render in separate phases;
//                     fix stbtt_GetFontOFfsetForIndex (never worked for non-0 input?);
//                     fixed an assert() bug in the new rasterizer
//                     replace assert() with STBTT_assert() in new rasterizer
//
//   Full history can be found at the end of this file.
//
// LICENSE
//
//   See end of file for license information.
//
// USAGE
//
//   Include this file in whatever places need to refer to it. In ONE C/C++
//   file, write:
//      #define STB_TRUETYPE_IMPLEMENTATION
//   before the #include of this file. This expands out the actual
//   implementation into that C/C++ file.
//
//   To make the implementation private to the file that generates the implementation,
//      #define STBTT_STATIC
//
//   Simple 3D API (don't ship this, but it's fine for tools and quick start)
//           stbtt_BakeFontBitmap()               -- bake a font to a bitmap for use as texture
//           stbtt_GetBakedQuad()                 -- compute quad to draw for a given char
//
//   Improved 3D API (more shippable):
//           #include "stb_rect_pack.h"           -- optional, but you really want it
//           stbtt_PackBegin()
//           stbtt_PackSetOversampling()          -- for improved quality on small fonts
//           stbtt_PackFontRanges()               -- pack and renders
//           stbtt_PackEnd()
//           stbtt_GetPackedQuad()
//
//   "Load" a font file from a memory buffer (you have to keep the buffer loaded)
//           stbtt_InitFont()
//           stbtt_GetFontOffsetForIndex()        -- indexing for TTC font collections
//           stbtt_GetNumberOfFonts()             -- number of fonts for TTC font collections
//
//   Render a unicode codepoint to a bitmap
//           stbtt_GetCodepointBitmap()           -- allocates and returns a bitmap
//           stbtt_MakeCodepointBitmap()          -- renders into bitmap you provide
//           stbtt_GetCodepointBitmapBox()        -- how big the bitmap must be
//
//   Character advance/positioning
//           stbtt_GetCodepointHMetrics()
//           stbtt_GetFontVMetrics()
//           stbtt_GetFontVMetricsOS2()
//           stbtt_GetCodepointKernAdvance()
//
//   Starting with version 1.06, the rasterizer was replaced with a new,
//   faster and generally-more-precise rasterizer. The new rasterizer more
//   accurately measures pixel coverage for anti-aliasing, except in the case
//   where multiple shapes overlap, in which case it overestimates the AA pixel
//   coverage. Thus, anti-aliasing of intersecting shapes may look wrong. If
//   this turns out to be a problem, you can re-enable the old rasterizer with
//        #define STBTT_RASTERIZER_VERSION 1
//   which will incur about a 15% speed hit.
//
// ADDITIONAL DOCUMENTATION
//
//   Immediately after this block comment are a series of sample programs.
//
//   After the sample programs is the "header file" section. This section
//   includes documentation for each API function.
//
//   Some important concepts to understand to use this library:
//
//      Codepoint
//         Characters are defined by unicode codepoints, e.g. 65 is
//         uppercase A, 231 is lowercase c with a cedilla, 0x7e30 is
//         the hiragana for "ma".
//
//      Glyph
//         A visual character shape (every codepoint is rendered as
//         some glyph)
//
//      Glyph index
//         A font-specific integer ID representing a glyph
//
//      Baseline
//         Glyph shapes are defined relative to a baseline, which is the
//         bottom of uppercase characters. Characters extend both above
//         and below the baseline.
//
//      Current Point
//         As you draw text to the screen, you keep track of a "current point"
//         which is the origin of each character. The current point's vertical
//         position is the baseline. Even "baked fonts" use this model.
//
//      Vertical Font Metrics
//         The vertical qualities of the font, used to vertically position
//         and space the characters. See docs for stbtt_GetFontVMetrics.
//
//      Font Size in Pixels or Points
//         The preferred interface for specifying font sizes in stb_truetype
//         is to specify how tall the font's vertical extent should be in pixels.
//         If that sounds good enough, skip the next paragraph.
//
//         Most font APIs instead use "points", which are a common typographic
//         measurement for describing font size, defined as 72 points per inch.
//         stb_truetype provides a point API for compatibility. However, true
//         "per inch" conventions don't make much sense on computer displays
//         since different monitors have different number of pixels per
//         inch. For example, Windows traditionally uses a convention that
//         there are 96 pixels per inch, thus making 'inch' measurements have
//         nothing to do with inches, and thus effectively defining a point to
//         be 1.333 pixels. Additionally, the TrueType font data provides
//         an explicit scale factor to scale a given font's glyphs to points,
//         but the author has observed that this scale factor is often wrong
//         for non-commercial fonts, thus making fonts scaled in points
//         according to the TrueType spec incoherently sized in practice.
//
// DETAILED USAGE:
//
//  Scale:
//    Select how high you want the font to be, in points or pixels.
//    Call ScaleForPixelHeight or ScaleForMappingEmToPixels to compute
//    a scale factor SF that will be used by all other functions.
//
//  Baseline:
//    You need to select a y-coordinate that is the baseline of where
//    your text will appear. Call GetFontBoundingBox to get the baseline-relative
//    bounding box for all characters. SF*-y0 will be the distance in pixels
//    that the worst-case character could extend above the baseline, so if
//    you want the top edge of characters to appear at the top of the
//    screen where y=0, then you would set the baseline to SF*-y0.
//
//  Current point:
//    Set the current point where the first character will appear. The
//    first character could extend left of the current point; this is font
//    dependent. You can either choose a current point that is the leftmost
//    point and hope, or add some padding, or check the bounding box or
//    left-side-bearing of the first character to be displayed and set
//    the current point based on that.
//
//  Displaying a character:
//    Compute the bounding box of the character. It will contain signed values
//    relative to <current_point, baseline>. I.e. if it returns x0,y0,x1,y1,
//    then the character should be displayed in the rectangle from
//    <current_point+SF*x0, baseline+SF*y0> to <current_point+SF*x1,baseline+SF*y1).
//
//  Advancing for the next character:
//    Call GlyphHMetrics, and compute 'current_point += SF * advance'.
//
//
// ADVANCED USAGE
//
//   Quality:
//
//    - Use the functions with Subpixel at the end to allow your characters
//      to have subpixel positioning. Since the font is anti-aliased, not
//      hinted, this is very import for quality. (This is not possible with
//      baked fonts.)
//
//    - Kerning is now supported, and if you're supporting subpixel rendering
//      then kerning is worth using to give your text a polished look.
//
//   Performance:
//
//    - Convert Unicode codepoints to glyph indexes and operate on the glyphs;
//      if you don't do this, stb_truetype is forced to do the conversion on
//      every call.
//
//    - There are a lot of memory allocations. We should modify it to take
//      a temp buffer and allocate from the temp buffer (without freeing),
//      should help performance a lot.
//
// NOTES
//
//   The system uses the raw data found in the .ttf file without changing it
//   and without building auxiliary data structures. This is a bit inefficient
//   on little-endian systems (the data is big-endian), but assuming you're
//   caching the bitmaps or glyph shapes this shouldn't be a big deal.
//
//   It appears to be very hard to programmatically determine what font a
//   given file is in a general way. I provide an API for this, but I don't
//   recommend it.
//
//
// PERFORMANCE MEASUREMENTS FOR 1.06:
//
//                      32-bit     64-bit
//   Previous release:  8.83 s     7.68 s
//   Pool allocations:  7.72 s     6.34 s
//   Inline sort     :  6.54 s     5.65 s
//   New rasterizer  :  5.63 s     5.00 s

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
////
////  SAMPLE PROGRAMS
////
//
//  Incomplete text-in-3d-api example, which draws quads properly aligned to be lossless.
//  See "tests/truetype_demo_win32.c" for a complete version.
#if 0
#define STB_TRUETYPE_IMPLEMENTATION  // force following include to generate implementation

unsigned char ttf_buffer[1<<20];
unsigned char temp_bitmap[512*512];

stbtt_bakedchar cdata[96]; // ASCII 32..126 is 95 glyphs
GLuint ftex;

void my_stbtt_initfont(void)
{
   fread(ttf_buffer, 1, 1<<20, fopen("c:/windows/fonts/times.ttf", "rb"));
   stbtt_BakeFontBitmap(ttf_buffer,0, 32.0, temp_bitmap,512,512, 32,96, cdata); // no guarantee this fits!
   // can free ttf_buffer at this point
   glGenTextures(1, &ftex);
   glBindTexture(GL_TEXTURE_2D, ftex);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 512,512, 0, GL_ALPHA, GL_UNSIGNED_BYTE, temp_bitmap);
   // can free temp_bitmap at this point
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
}

void my_stbtt_print(float x, float y, char *text)
{
   // assume orthographic projection with units = screen pixels, origin at top left
   glEnable(GL_BLEND);
   glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
   glEnable(GL_TEXTURE_2D);
   glBindTexture(GL_TEXTURE_2D, ftex);
   glBegin(GL_QUADS);
   while (*text) {
      if (*text >= 32 && *text < 128) {
         stbtt_aligned_quad q;
         stbtt_GetBakedQuad(cdata, 512,512, *text-32, &x,&y,&q,1);//1=opengl & d3d10+,0=d3d9
         glTexCoord2f(q.s0,q.t0); glVertex2f(q.x0,q.y0);
         glTexCoord2f(q.s1,q.t0); glVertex2f(q.x1,q.y0);
         glTexCoord2f(q.s1,q.t1); glVertex2f(q.x1,q.y1);
         glTexCoord2f(q.s0,q.t1); glVertex2f(q.x0,q.y1);
      }
      ++text;
   }
   glEnd();
}
#endif
//
//
//////////////////////////////////////////////////////////////////////////////
//
// Complete program (this compiles): get a single bitmap, print as ASCII art
//
#if 0
#include <stdio.h>
#define STB_TRUETYPE_IMPLEMENTATION  // force following include to generate implementation

char ttf_buffer[1<<25];

int main(int argc, char **argv)
{
   stbtt_fontinfo font;
   unsigned char *bitmap;
   int w,h,i,j,c = (argc > 1 ? atoi(argv[1]) : 'a'), s = (argc > 2 ? atoi(argv[2]) : 20);

   fread(ttf_buffer, 1, 1<<25, fopen(argc > 3 ? argv[3] : "c:/windows/fonts/arialbd.ttf", "rb"));

   stbtt_InitFont(&font, ttf_buffer, stbtt_GetFontOffsetForIndex(ttf_buffer,0));
   bitmap = stbtt_GetCodepointBitmap(&font, 0,stbtt_ScaleForPixelHeight(&font, s), c, &w, &h, 0,0);

   for (j=0; j < h; ++j) {
      for (i=0; i < w; ++i)
         putchar(" .:ioVM@"[bitmap[j*w+i]>>5]);
      putchar('\n');
   }
   return 0;
}
#endif
//
// Output:
//
//     .ii.
//    @@@@@@.
//   V@Mio@@o
//   :i.  V@V
//     :oM@@M
//   :@@@MM@M
//   @@o  o@M
//  :@@.  M@M
//   @@@o@@@@
//   :M@@V:@@.
//
//////////////////////////////////////////////////////////////////////////////
//
// Complete program: print "Hello World!" banner, with bugs
//
#if 0
char buffer[24<<20];
unsigned char screen[20][79];

int main(int arg, char **argv)
{
   stbtt_fontinfo font;
   int i,j,ascent,baseline,ch=0;
   float scale, xpos=2; // leave a little padding in case the character extends left
   char *text = "Heljo World!"; // intentionally misspelled to show 'lj' brokenness

   fread(buffer, 1, 1000000, fopen("c:/windows/fonts/arialbd.ttf", "rb"));
   stbtt_InitFont(&font, buffer, 0);

   scale = stbtt_ScaleForPixelHeight(&font, 15);
   stbtt_GetFontVMetrics(&font, &ascent,0,0);
   baseline = (int) (ascent*scale);

   while (text[ch]) {
      int advance,lsb,x0,y0,x1,y1;
      float x_shift = xpos - (float) floor(xpos);
      stbtt_GetCodepointHMetrics(&font, text[ch], &advance, &lsb);
      stbtt_GetCodepointBitmapBoxSubpixel(&font, text[ch], scale,scale,x_shift,0, &x0,&y0,&x1,&y1);
      stbtt_MakeCodepointBitmapSubpixel(&font, &screen[baseline + y0][(int) xpos + x0], x1-x0,y1-y0, 79, scale,scale,x_shift,0, text[ch]);
      // note that this stomps the old data, so where character boxes overlap (e.g. 'lj') it's wrong
      // because this API is really for baking character bitmaps into textures. if you want to render
      // a sequence of characters, you really need to render each bitmap to a temp buffer, then
      // "alpha blend" that into the working buffer
      xpos += (advance * scale);
      if (text[ch+1])
         xpos += scale*stbtt_GetCodepointKernAdvance(&font, text[ch],text[ch+1]);
      ++ch;
   }

   for (j=0; j < 20; ++j) {
      for (i=0; i < 78; ++i)
         putchar(" .:ioVM@"[screen[j][i]>>5]);
      putchar('\n');
   }

   return 0;
}
#endif


//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
////
////   INTEGRATION WITH YOUR CODEBASE
////
////   The following sections allow you to supply alternate definitions
////   of C library functions used by stb_truetype, e.g. if you don't
////   link with the C runtime library.

#ifdef STB_TRUETYPE_IMPLEMENTATION
   // #define your own (u)stbtt_int8/16/32 before including to override this
   #ifndef stbtt_uint8
   typedef unsigned char   stbtt_uint8;
   typedef signed   char   stbtt_int8;
   typedef unsigned short  stbtt_uint16;
   typedef signed   short  stbtt_int16;
   typedef unsigned int    stbtt_uint32;
   typedef signed   int    stbtt_int32;
   #endif

   typedef char stbtt__check_size32[sizeof(stbtt_int32)==4 ? 1 : -1];
   typedef char stbtt__check_size16[sizeof(stbtt_int16)==2 ? 1 : -1];

   // e.g. #define your own STBTT_ifloor/STBTT_iceil() to avoid math.h
   #ifndef STBTT_ifloor
   #include <math.h>
   #define STBTT_ifloor(x)   ((int) floor(x))
   #define STBTT_iceil(x)    ((int) ceil(x))
   #endif

   #ifndef STBTT_sqrt
   #include <math.h>
   #define STBTT_sqrt(x)      sqrt(x)
   #define STBTT_pow(x,y)     pow(x,y)
   #endif

   #ifndef STBTT_fmod
   #include <math.h>
   #define STBTT_fmod(x,y)    fmod(x,y)
   #endif

   #ifndef STBTT_cos
   #include <math.h>
   #define STBTT_cos(x)       cos(x)
   #define STBTT_acos(x)      acos(x)
   #endif

   #ifndef STBTT_fabs
   #include <math.h>
   #define STBTT_fabs(x)      fabs(x)
   #endif

   // #define your own functions "STBTT_malloc" / "STBTT_free" to avoid malloc.h
   #ifndef STBTT_malloc
   #include <stdlib.h>
   #define STBTT_malloc(x,u)  ((void)(u),malloc(x))
   #define STBTT_free(x,u)    ((void)(u),free(x))
   #endif

   #ifndef STBTT_assert
   #include <assert.h>
   #define STBTT_assert(x)    assert(x)
   #endif

   #ifndef STBTT_strlen
   #include <string.h>
   #define STBTT_strlen(x)    strlen(x)
   #endif

   #ifndef STBTT_memcpy
   #include <string.h>
   #define STBTT_memcpy       memcpy
   #define STBTT_memset       memset
   #endif
#endif

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
////
////   INTERFACE
////
////

#ifndef __STB_INCLUDE_STB_TRUETYPE_H__
#define __STB_INCLUDE_STB_TRUETYPE_H__

#ifdef STBTT_STATIC
#define STBTT_DEF static
#else
#define STBTT_DEF extern
#endif

#ifdef __cplusplus
extern "C" {
#endif

// private structure
typedef struct
{
   unsigned char *data;
   int cursor;
   int size;
} stbtt__buf;

//////////////////////////////////////////////////////////////////////////////
//
// TEXTURE BAKING API
//
// If you use this API, you only have to call two functions ever.
//

typedef struct
{
   unsigned short x0,y0,x1,y1; // coordinates of bbox in bitmap
   float xoff,yoff,xadvance;
} stbtt_bakedchar;

STBTT_DEF int stbtt_BakeFontBitmap(const unsigned char *data, int offset,  // font location (use offset=0 for plain .ttf)
                                float pixel_height,                     // height of font in pixels
                                unsigned char *pixels, int pw, int ph,  // bitmap to be filled in
                                int first_char, int num_chars,          // characters to bake
                                stbtt_bakedchar *chardata);             // you allocate this, it's num_chars long
// if return is positive, the first unused row of the bitmap
// if return is negative, returns the negative of the number of characters that fit
// if return is 0, no characters fit and no rows were used
// This uses a very crappy packing.

typedef struct
{
   float x0,y0,s0,t0; // top-left
   float x1,y1,s1,t1; // bottom-right
} stbtt_aligned_quad;

STBTT_DEF void stbtt_GetBakedQuad(const stbtt_bakedchar *chardata, int pw, int ph,  // same data as above
                               int char_index,             // character to display
                               float *xpos, float *ypos,   // pointers to current position in screen pixel space
                               stbtt_aligned_quad *q,      // output: quad to draw
                               int opengl_fillrule);       // true if opengl fill rule; false if DX9 or earlier
// Call GetBakedQuad with char_index = 'character - first_char', and it
// creates the quad you need to draw and advances the current position.
//
// The coordinate system used assumes y increases downwards.
//
// Characters will extend both above and below the current position;
// see discussion of "BASELINE" above.
//
// It's inefficient; you might want to c&p it and optimize it.

STBTT_DEF void stbtt_GetScaledFontVMetrics(const unsigned char *fontdata, int index, float size, float *ascent, float *descent, float *lineGap);
// Query the font vertical metrics without having to create a font first.


//////////////////////////////////////////////////////////////////////////////
//
// NEW TEXTURE BAKING API
//
// This provides options for packing multiple fonts into one atlas, not
// perfectly but better than nothing.

typedef struct
{
   unsigned short x0,y0,x1,y1; // coordinates of bbox in bitmap
   float xoff,yoff,xadvance;
   float xoff2,yoff2;
} stbtt_packedchar;

typedef struct stbtt_pack_context stbtt_pack_context;
typedef struct stbtt_fontinfo stbtt_fontinfo;
#ifndef STB_RECT_PACK_VERSION
typedef struct stbrp_rect stbrp_rect;
#endif

STBTT_DEF int  stbtt_PackBegin(stbtt_pack_context *spc, unsigned char *pixels, int width, int height, int stride_in_bytes, int padding, void *alloc_context);
// Initializes a packing context stored in the passed-in stbtt_pack_context.
// Future calls using this context will pack characters into the bitmap passed
// in here: a 1-channel bitmap that is width * height. stride_in_bytes is
// the distance from one row to the next (or 0 to mean they are packed tightly
// together). "padding" is the amount of padding to leave between each
// character (normally you want '1' for bitmaps you'll use as textures with
// bilinear filtering).
//
// Returns 0 on failure, 1 on success.

STBTT_DEF void stbtt_PackEnd  (stbtt_pack_context *spc);
// Cleans up the packing context and frees all memory.

#define STBTT_POINT_SIZE(x)   (-(x))

STBTT_DEF int  stbtt_PackFontRange(stbtt_pack_context *spc, const unsigned char *fontdata, int font_index, float font_size,
                                int first_unicode_char_in_range, int num_chars_in_range, stbtt_packedchar *chardata_for_range);
// Creates character bitmaps from the font_index'th font found in fontdata (use
// font_index=0 if you don't know what that is). It creates num_chars_in_range
// bitmaps for characters with unicode values starting at first_unicode_char_in_range
// and increasing. Data for how to render them is stored in chardata_for_range;
// pass these to stbtt_GetPackedQuad to get back renderable quads.
//
// font_size is the full height of the character from ascender to descender,
// as computed by stbtt_ScaleForPixelHeight. To use a point size as computed
// by stbtt_ScaleForMappingEmToPixels, wrap the point size in STBTT_POINT_SIZE()
// and pass that result as 'font_size':
//       ...,                  20 , ... // font max minus min y is 20 pixels tall
//       ..., STBTT_POINT_SIZE(20), ... // 'M' is 20 pixels tall

typedef struct
{
   float font_size;
   int first_unicode_codepoint_in_range;  // if non-zero, then the chars are continuous, and this is the first codepoint
   int *array_of_unicode_codepoints;       // if non-zero, then this is an array of unicode codepoints
   int num_chars;
   stbtt_packedchar *chardata_for_range; // output
   unsigned char h_oversample, v_oversample; // don't set these, they're used internally
} stbtt_pack_range;

STBTT_DEF int  stbtt_PackFontRanges(stbtt_pack_context *spc, const unsigned char *fontdata, int font_index, stbtt_pack_range *ranges, int num_ranges);
// Creates character bitmaps from multiple ranges of characters stored in
// ranges. This will usually create a better-packed bitmap than multiple
// calls to stbtt_PackFontRange. Note that you can call this multiple
// times within a single PackBegin/PackEnd.

STBTT_DEF void stbtt_PackSetOversampling(stbtt_pack_context *spc, unsigned int h_oversample, unsigned int v_oversample);
// Oversampling a font increases the quality by allowing higher-quality subpixel
// positioning, and is especially valuable at smaller text sizes.
//
// This function sets the amount of oversampling for all following calls to
// stbtt_PackFontRange(s) or stbtt_PackFontRangesGatherRects for a given
// pack context. The default (no oversampling) is achieved by h_oversample=1
// and v_oversample=1. The total number of pixels required is
// h_oversample*v_oversample larger than the default; for example, 2x2
// oversampling requires 4x the storage of 1x1. For best results, render
// oversampled textures with bilinear filtering. Look at the readme in
// stb/tests/oversample for information about oversampled fonts
//
// To use with PackFontRangesGather etc., you must set it before calls
// call to PackFontRangesGatherRects.

STBTT_DEF void stbtt_PackSetSkipMissingCodepoints(stbtt_pack_context *spc, int skip);
// If skip != 0, this tells stb_truetype to skip any codepoints for which
// there is no corresponding glyph. If skip=0, which is the default, then
// codepoints without a glyph recived the font's "missing character" glyph,
// typically an empty box by convention.

STBTT_DEF void stbtt_GetPackedQuad(const stbtt_packedchar *chardata, int pw, int ph,  // same data as above
                               int char_index,             // character to display
                               float *xpos, float *ypos,   // pointers to current position in screen pixel space
                               stbtt_aligned_quad *q,      // output: quad to draw
                               int align_to_integer);

STBTT_DEF int  stbtt_PackFontRangesGatherRects(stbtt_pack_context *spc, const stbtt_fontinfo *info, stbtt_pack_range *ranges, int num_ranges, stbrp_rect *rects);
STBTT_DEF void stbtt_PackFontRangesPackRects(stbtt_pack_context *spc, stbrp_rect *rects, int num_rects);
STBTT_DEF int  stbtt_PackFontRangesRenderIntoRects(stbtt_pack_context *spc, const stbtt_fontinfo *info, stbtt_pack_range *ranges, int num_ranges, stbrp_rect *rects);
// Calling these functions in sequence is roughly equivalent to calling
// stbtt_PackFontRanges(). If you more control over the packing of multiple
// fonts, or if you want to pack custom data into a font texture, take a look
// at the source to of stbtt_PackFontRanges() and create a custom version
// using these functions, e.g. call GatherRects multiple times,
// building up a single array of rects, then call PackRects once,
// then call RenderIntoRects repeatedly. This may result in a
// better packing than calling PackFontRanges multiple times
// (or it may not).

// this is an opaque structure that you shouldn't mess with which holds
// all the context needed from PackBegin to PackEnd.
struct stbtt_pack_context {
   void *user_allocator_context;
   void *pack_info;
   int   width;
   int   height;
   int   stride_in_bytes;
   int   padding;
   int   skip_missing;
   unsigned int   h_oversample, v_oversample;
   unsigned char *pixels;
   void  *nodes;
};

//////////////////////////////////////////////////////////////////////////////
//
// FONT LOADING
//
//

STBTT_DEF int stbtt_GetNumberOfFonts(const unsigned char *data);
// This function will determine the number of fonts in a font file.  TrueType
// collection (.ttc) files may contain multiple fonts, while TrueType font
// (.ttf) files only contain one font. The number of fonts can be used for
// indexing with the previous function where the index is between zero and one
// less than the total fonts. If an error occurs, -1 is returned.

STBTT_DEF int stbtt_GetFontOffsetForIndex(const unsigned char *data, int index);
// Each .ttf/.ttc file may have more than one font. Each font has a sequential
// index number starting from 0. Call this function to get the font offset for
// a given index; it returns -1 if the index is out of range. A regular .ttf
// file will only define one font and it always be at offset 0, so it will
// return '0' for index 0, and -1 for all other indices.

// The following structure is defined publicly so you can declare one on
// the stack or as a global or etc, but you should treat it as opaque.
struct stbtt_fontinfo
{
   void           * userdata;
   unsigned char  * data;              // pointer to .ttf file
   int              fontstart;         // offset of start of font

   int numGlyphs;                     // number of glyphs, needed for range checking

   int loca,head,glyf,hhea,hmtx,kern,gpos,svg; // table locations as offset from start of .ttf
   int index_map;                     // a cmap mapping for our chosen character encoding
   int indexToLocFormat;              // format needed to map from glyph index to glyph

   stbtt__buf cff;                    // cff font data
   stbtt__buf charstrings;            // the charstring index
   stbtt__buf gsubrs;                 // global charstring subroutines index
   stbtt__buf subrs;                  // private charstring subroutines index
   stbtt__buf fontdicts;              // array of font dicts
   stbtt__buf fdselect;               // map from glyph to fontdict
};

STBTT_DEF int stbtt_InitFont(stbtt_fontinfo *info, const unsigned char *data, int offset);
// Given an offset into the file that defines a font, this function builds
// the necessary cached info for the rest of the system. You must allocate
// the stbtt_fontinfo yourself, and stbtt_InitFont will fill it out. You don't
// need to do anything special to free it, because the contents are pure
// value data with no additional data structures. Returns 0 on failure.


//////////////////////////////////////////////////////////////////////////////
//
// CHARACTER TO GLYPH-INDEX CONVERSIOn

STBTT_DEF int stbtt_FindGlyphIndex(const stbtt_fontinfo *info, int unicode_codepoint);
// If you're going to perform multiple operations on the same character
// and you want a speed-up, call this function with the character you're
// going to process, then use glyph-based functions instead of the
// codepoint-based functions.
// Returns 0 if the character codepoint is not defined in the font.


//////////////////////////////////////////////////////////////////////////////
//
// CHARACTER PROPERTIES
//

STBTT_DEF float stbtt_ScaleForPixelHeight(const stbtt_fontinfo *info, float pixels);
// computes a scale factor to produce a font whose "height" is 'pixels' tall.
// Height is measured as the distance from the highest ascender to the lowest
// descender; in other words, it's equivalent to calling stbtt_GetFontVMetrics
// and computing:
//       scale = pixels / (ascent - descent)
// so if you prefer to measure height by the ascent only, use a similar calculation.

STBTT_DEF float stbtt_ScaleForMappingEmToPixels(const stbtt_fontinfo *info, float pixels);
// computes a scale factor to produce a font whose EM size is mapped to
// 'pixels' tall. This is probably what traditional APIs compute, but
// I'm not positive.

STBTT_DEF void stbtt_GetFontVMetrics(const stbtt_fontinfo *info, int *ascent, int *descent, int *lineGap);
// ascent is the coordinate above the baseline the font extends; descent
// is the coordinate below the baseline the font extends (i.e. it is typically negative)
// lineGap is the spacing between one row's descent and the next row's ascent...
// so you should advance the vertical position by "*ascent - *descent + *lineGap"
//   these are expressed in unscaled coordinates, so you must multiply by
//   the scale factor for a given size

STBTT_DEF int  stbtt_GetFontVMetricsOS2(const stbtt_fontinfo *info, int *typoAscent, int *typoDescent, int *typoLineGap);
// analogous to GetFontVMetrics, but returns the "typographic" values from the OS/2
// table (specific to MS/Windows TTF files).
//
// Returns 1 on success (table present), 0 on failure.

STBTT_DEF void stbtt_GetFontBoundingBox(const stbtt_fontinfo *info, int *x0, int *y0, int *x1, int *y1);
// the bounding box around all possible characters

STBTT_DEF void stbtt_GetCodepointHMetrics(const stbtt_fontinfo *info, int codepoint, int *advanceWidth, int *leftSideBearing);
// leftSideBearing is the offset from the current horizontal position to the left edge of the character
// advanceWidth is the offset from the current horizontal position to the next horizontal position
//   these are expressed in unscaled coordinates

STBTT_DEF int  stbtt_GetCodepointKernAdvance(const stbtt_fontinfo *info, int ch1, int ch2);
// an additional amount to add to the 'advance' value between ch1 and ch2

STBTT_DEF int stbtt_GetCodepointBox(const stbtt_fontinfo *info, int codepoint, int *x0, int *y0, int *x1, int *y1);
// Gets the bounding box of the visible part of the glyph, in unscaled coordinates

STBTT_DEF void stbtt_GetGlyphHMetrics(const stbtt_fontinfo *info, int glyph_index, int *advanceWidth, int *leftSideBearing);
STBTT_DEF int  stbtt_GetGlyphKernAdvance(const stbtt_fontinfo *info, int glyph1, int glyph2);
STBTT_DEF int  stbtt_GetGlyphBox(const stbtt_fontinfo *info, int glyph_index, int *x0, int *y0, int *x1, int *y1);
// as above, but takes one or more glyph indices for greater efficiency

typedef struct stbtt_kerningentry
{
   int glyph1; // use stbtt_FindGlyphIndex
   int glyph2;
   int advance;
} stbtt_kerningentry;

STBTT_DEF int  stbtt_GetKerningTableLength(const stbtt_fontinfo *info);
STBTT_DEF int  stbtt_GetKerningTable(const stbtt_fontinfo *info, stbtt_kerningentry* table, int table_length);
// Retrieves a complete list of all of the kerning pairs provided by the font
// stbtt_GetKerningTable never writes more than table_length entries and returns how many entries it did write.
// The table will be sorted by (a.glyph1 == b.glyph1)?(a.glyph2 < b.glyph2):(a.glyph1 < b.glyph1)

//////////////////////////////////////////////////////////////////////////////
//
// GLYPH SHAPES (you probably don't need these, but they have to go before
// the bitmaps for C declaration-order reasons)
//

#ifndef STBTT_vmove // you can predefine these to use different values (but why?)
   enum {
      STBTT_vmove=1,
      STBTT_vline,
      STBTT_vcurve,
      STBTT_vcubic
   };
#endif

#ifndef stbtt_vertex // you can predefine this to use different values
                   // (we share this with other code at RAD)
   #define stbtt_vertex_type short // can't use stbtt_int16 because that's not visible in the header file
   typedef struct
   {
      stbtt_vertex_type x,y,cx,cy,cx1,cy1;
      unsigned char type,padding;
   } stbtt_vertex;
#endif

STBTT_DEF int stbtt_IsGlyphEmpty(const stbtt_fontinfo *info, int glyph_index);
// returns non-zero if nothing is drawn for this glyph

STBTT_DEF int stbtt_GetCodepointShape(const stbtt_fontinfo *info, int unicode_codepoint, stbtt_vertex **vertices);
STBTT_DEF int stbtt_GetGlyphShape(const stbtt_fontinfo *info, int glyph_index, stbtt_vertex **vertices);
// returns # of vertices and fills *vertices with the pointer to them
//   these are expressed in "unscaled" coordinates
//
// The shape is a series of contours. Each one starts with
// a STBTT_moveto, then consists of a series of mixed
// STBTT_lineto and STBTT_curveto segments. A lineto
// draws a line from previous endpoint to its x,y; a curveto
// draws a quadratic bezier from previous endpoint to
// its x,y, using cx,cy as the bezier control point.

STBTT_DEF void stbtt_FreeShape(const stbtt_fontinfo *info, stbtt_vertex *vertices);
// frees the data allocated above

STBTT_DEF unsigned char *stbtt_FindSVGDoc(const stbtt_fontinfo *info, int gl);
STBTT_DEF int stbtt_GetCodepointSVG(const stbtt_fontinfo *info, int unicode_codepoint, const char **svg);
STBTT_DEF int stbtt_GetGlyphSVG(const stbtt_fontinfo *info, int gl, const char **svg);
// fills svg with the character's SVG data.
// returns data size or 0 if SVG not found.

//////////////////////////////////////////////////////////////////////////////
//
// BITMAP RENDERING
//

STBTT_DEF void stbtt_FreeBitmap(unsigned char *bitmap, void *userdata);
// frees the bitmap allocated below

STBTT_DEF unsigned char *stbtt_GetCodepointBitmap(const stbtt_fontinfo *info, float scale_x, float scale_y, int codepoint, int *width, int *height, int *xoff, int *yoff);
// allocates a large-enough single-channel 8bpp bitmap and renders the
// specified character/glyph at the specified scale into it, with
// antialiasing. 0 is no coverage (transparent), 255 is fully covered (opaque).
// *width & *height are filled out with the width & height of the bitmap,
// which is stored left-to-right, top-to-bottom.
//
// xoff/yoff are the offset it pixel space from the glyph origin to the top-left of the bitmap

STBTT_DEF unsigned char *stbtt_GetCodepointBitmapSubpixel(const stbtt_fontinfo *info, float scale_x, float scale_y, float shift_x, float shift_y, int codepoint, int *width, int *height, int *xoff, int *yoff);
// the same as stbtt_GetCodepoitnBitmap, but you can specify a subpixel
// shift for the character

STBTT_DEF void stbtt_MakeCodepointBitmap(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, int codepoint);
// the same as stbtt_GetCodepointBitmap, but you pass in storage for the bitmap
// in the form of 'output', with row spacing of 'out_stride' bytes. the bitmap
// is clipped to out_w/out_h bytes. Call stbtt_GetCodepointBitmapBox to get the
// width and height and positioning info for it first.

STBTT_DEF void stbtt_MakeCodepointBitmapSubpixel(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, float shift_x, float shift_y, int codepoint);
// same as stbtt_MakeCodepointBitmap, but you can specify a subpixel
// shift for the character

STBTT_DEF void stbtt_MakeCodepointBitmapSubpixelPrefilter(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, float shift_x, float shift_y, int oversample_x, int oversample_y, float *sub_x, float *sub_y, int codepoint);
// same as stbtt_MakeCodepointBitmapSubpixel, but prefiltering
// is performed (see stbtt_PackSetOversampling)

STBTT_DEF void stbtt_GetCodepointBitmapBox(const stbtt_fontinfo *font, int codepoint, float scale_x, float scale_y, int *ix0, int *iy0, int *ix1, int *iy1);
// get the bbox of the bitmap centered around the glyph origin; so the
// bitmap width is ix1-ix0, height is iy1-iy0, and location to place
// the bitmap top left is (leftSideBearing*scale,iy0).
// (Note that the bitmap uses y-increases-down, but the shape uses
// y-increases-up, so CodepointBitmapBox and CodepointBox are inverted.)

STBTT_DEF void stbtt_GetCodepointBitmapBoxSubpixel(const stbtt_fontinfo *font, int codepoint, float scale_x, float scale_y, float shift_x, float shift_y, int *ix0, int *iy0, int *ix1, int *iy1);
// same as stbtt_GetCodepointBitmapBox, but you can specify a subpixel
// shift for the character

// the following functions are equivalent to the above functions, but operate
// on glyph indices instead of Unicode codepoints (for efficiency)
STBTT_DEF unsigned char *stbtt_GetGlyphBitmap(const stbtt_fontinfo *info, float scale_x, float scale_y, int glyph, int *width, int *height, int *xoff, int *yoff);
STBTT_DEF unsigned char *stbtt_GetGlyphBitmapSubpixel(const stbtt_fontinfo *info, float scale_x, float scale_y, float shift_x, float shift_y, int glyph, int *width, int *height, int *xoff, int *yoff);
STBTT_DEF void stbtt_MakeGlyphBitmap(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, int glyph);
STBTT_DEF void stbtt_MakeGlyphBitmapSubpixel(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, float shift_x, float shift_y, int glyph);
STBTT_DEF void stbtt_MakeGlyphBitmapSubpixelPrefilter(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, float shift_x, float shift_y, int oversample_x, int oversample_y, float *sub_x, float *sub_y, int glyph);
STBTT_DEF void stbtt_GetGlyphBitmapBox(const stbtt_fontinfo *font, int glyph, float scale_x, float scale_y, int *ix0, int *iy0, int *ix1, int *iy1);
STBTT_DEF void stbtt_GetGlyphBitmapBoxSubpixel(const stbtt_fontinfo *font, int glyph, float scale_x, float scale_y,float shift_x, float shift_y, int *ix0, int *iy0, int *ix1, int *iy1);


// @TODO: don't expose this structure
typedef struct
{
   int w,h,stride;
   unsigned char *pixels;
} stbtt__bitmap;

// rasterize a shape with quadratic beziers into a bitmap
STBTT_DEF void stbtt_Rasterize(stbtt__bitmap *result,        // 1-channel bitmap to draw into
                               float flatness_in_pixels,     // allowable error of curve in pixels
                               stbtt_vertex *vertices,       // array of vertices defining shape
                               int num_verts,                // number of vertices in above array
                               float scale_x, float scale_y, // scale applied to input vertices
                               float shift_x, float shift_y, // translation applied to input vertices
                               int x_off, int y_off,         // another translation applied to input
                               int invert,                   // if non-zero, vertically flip shape
                               void *userdata);              // context for to STBTT_MALLOC

//////////////////////////////////////////////////////////////////////////////
//
// Signed Distance Function (or Field) rendering

STBTT_DEF void stbtt_FreeSDF(unsigned char *bitmap, void *userdata);
// frees the SDF bitmap allocated below

STBTT_DEF unsigned char * stbtt_GetGlyphSDF(const stbtt_fontinfo *info, float scale, int glyph, int padding, unsigned char onedge_value, float pixel_dist_scale, int *width, int *height, int *xoff, int *yoff);
STBTT_DEF unsigned char * stbtt_GetCodepointSDF(const stbtt_fontinfo *info, float scale, int codepoint, int padding, unsigned char onedge_value, float pixel_dist_scale, int *width, int *height, int *xoff, int *yoff);
// These functions compute a discretized SDF field for a single character, suitable for storing
// in a single-channel texture, sampling with bilinear filtering, and testing against
// larger than some threshold to produce scalable fonts.
//        info              --  the font
//        scale             --  controls the size of the resulting SDF bitmap, same as it would be creating a regular bitmap
//        glyph/codepoint   --  the character to generate the SDF for
//        padding           --  extra "pixels" around the character which are filled with the distance to the character (not 0),
//                                 which allows effects like bit outlines
//        onedge_value      --  value 0-255 to test the SDF against to reconstruct the character (i.e. the isocontour of the character)
//        pixel_dist_scale  --  what value the SDF should increase by when moving one SDF "pixel" away from the edge (on the 0..255 scale)
//                                 if positive, > onedge_value is inside; if negative, < onedge_value is inside
//        width,height      --  output height & width of the SDF bitmap (including padding)
//        xoff,yoff         --  output origin of the character
//        return value      --  a 2D array of bytes 0..255, width*height in size
//
// pixel_dist_scale & onedge_value are a scale & bias that allows you to make
// optimal use of the limited 0..255 for your application, trading off precision
// and special effects. SDF values outside the range 0..255 are clamped to 0..255.
//
// Example:
//      scale = stbtt_ScaleForPixelHeight(22)
//      padding = 5
//      onedge_value = 180
//      pixel_dist_scale = 180/5.0 = 36.0
//
//      This will create an SDF bitmap in which the character is about 22 pixels
//      high but the whole bitmap is about 22+5+5=32 pixels high. To produce a filled
//      shape, sample the SDF at each pixel and fill the pixel if the SDF value
//      is greater than or equal to 180/255. (You'll actually want to antialias,
//      which is beyond the scope of this example.) Additionally, you can compute
//      offset outlines (e.g. to stroke the character border inside & outside,
//      or only outside). For example, to fill outside the character up to 3 SDF
//      pixels, you would compare against (180-36.0*3)/255 = 72/255. The above
//      choice of variables maps a range from 5 pixels outside the shape to
//      2 pixels inside the shape to 0..255; this is intended primarily for apply
//      outside effects only (the interior range is needed to allow proper
//      antialiasing of the font at *smaller* sizes)
//
// The function computes the SDF analytically at each SDF pixel, not by e.g.
// building a higher-res bitmap and approximating it. In theory the quality
// should be as high as possible for an SDF of this size & representation, but
// unclear if this is true in practice (perhaps building a higher-res bitmap
// and computing from that can allow drop-out prevention).
//
// The algorithm has not been optimized at all, so expect it to be slow
// if computing lots of characters or very large sizes.



//////////////////////////////////////////////////////////////////////////////
//
// Finding the right font...
//
// You should really just solve this offline, keep your own tables
// of what font is what, and don't try to get it out of the .ttf file.
// That's because getting it out of the .ttf file is really hard, because
// the names in the file can appear in many possible encodings, in many
// possible languages, and e.g. if you need a case-insensitive comparison,
// the details of that depend on the encoding & language in a complex way
// (actually underspecified in truetype, but also gigantic).
//
// But you can use the provided functions in two possible ways:
//     stbtt_FindMatchingFont() will use *case-sensitive* comparisons on
//             unicode-encoded names to try to find the font you want;
//             you can run this before calling stbtt_InitFont()
//
//     stbtt_GetFontNameString() lets you get any of the various strings
//             from the file yourself and do your own comparisons on them.
//             You have to have called stbtt_InitFont() first.


STBTT_DEF int stbtt_FindMatchingFont(const unsigned char *fontdata, const char *name, int flags);
// returns the offset (not index) of the font that matches, or -1 if none
//   if you use STBTT_MACSTYLE_DONTCARE, use a font name like "Arial Bold".
//   if you use any other flag, use a font name like "Arial"; this checks
//     the 'macStyle' header field; i don't know if fonts set this consistently
#define STBTT_MACSTYLE_DONTCARE     0
#define STBTT_MACSTYLE_BOLD         1
#define STBTT_MACSTYLE_ITALIC       2
#define STBTT_MACSTYLE_UNDERSCORE   4
#define STBTT_MACSTYLE_NONE         8   // <= not same as 0, this makes us check the bitfield is 0

STBTT_DEF int stbtt_CompareUTF8toUTF16_bigendian(const char *s1, int len1, const char *s2, int len2);
// returns 1/0 whether the first string interpreted as utf8 is identical to
// the second string interpreted as big-endian utf16... useful for strings from next func

STBTT_DEF const char *stbtt_GetFontNameString(const stbtt_fontinfo *font, int *length, int platformID, int encodingID, int languageID, int nameID);
// returns the string (which may be big-endian double byte, e.g. for unicode)
// and puts the length in bytes in *length.
//
// some of the values for the IDs are below; for more see the truetype spec:
//     http://developer.apple.com/textfonts/TTRefMan/RM06/Chap6name.html
//     http://www.microsoft.com/typography/otspec/name.htm

enum { // platformID
   STBTT_PLATFORM_ID_UNICODE   =0,
   STBTT_PLATFORM_ID_MAC       =1,
   STBTT_PLATFORM_ID_ISO       =2,
   STBTT_PLATFORM_ID_MICROSOFT =3
};

enum { // encodingID for STBTT_PLATFORM_ID_UNICODE
   STBTT_UNICODE_EID_UNICODE_1_0    =0,
   STBTT_UNICODE_EID_UNICODE_1_1    =1,
   STBTT_UNICODE_EID_ISO_10646      =2,
   STBTT_UNICODE_EID_UNICODE_2_0_BMP=3,
   STBTT_UNICODE_EID_UNICODE_2_0_FULL=4
};

enum { // encodingID for STBTT_PLATFORM_ID_MICROSOFT
   STBTT_MS_EID_SYMBOL        =0,
   STBTT_MS_EID_UNICODE_BMP   =1,
   STBTT_MS_EID_SHIFTJIS      =2,
   STBTT_MS_EID_UNICODE_FULL  =10
};

enum { // encodingID for STBTT_PLATFORM_ID_MAC; same as Script Manager codes
   STBTT_MAC_EID_ROMAN        =0,   STBTT_MAC_EID_ARABIC       =4,
   STBTT_MAC_EID_JAPANESE     =1,   STBTT_MAC_EID_HEBREW       =5,
   STBTT_MAC_EID_CHINESE_TRAD =2,   STBTT_MAC_EID_GREEK        =6,
   STBTT_MAC_EID_KOREAN       =3,   STBTT_MAC_EID_RUSSIAN      =7
};

enum { // languageID for STBTT_PLATFORM_ID_MICROSOFT; same as LCID...
       // problematic because there are e.g. 16 english LCIDs and 16 arabic LCIDs
   STBTT_MS_LANG_ENGLISH     =0x0409,   STBTT_MS_LANG_ITALIAN     =0x0410,
   STBTT_MS_LANG_CHINESE     =0x0804,   STBTT_MS_LANG_JAPANESE    =0x0411,
   STBTT_MS_LANG_DUTCH       =0x0413,   STBTT_MS_LANG_KOREAN      =0x0412,
   STBTT_MS_LANG_FRENCH      =0x040c,   STBTT_MS_LANG_RUSSIAN     =0x0419,
   STBTT_MS_LANG_GERMAN      =0x0407,   STBTT_MS_LANG_SPANISH     =0x0409,
   STBTT_MS_LANG_HEBREW      =0x040d,   STBTT_MS_LANG_SWEDISH     =0x041D
};

enum { // languageID for STBTT_PLATFORM_ID_MAC
   STBTT_MAC_LANG_ENGLISH      =0 ,   STBTT_MAC_LANG_JAPANESE     =11,
   STBTT_MAC_LANG_ARABIC       =12,   STBTT_MAC_LANG_KOREAN       =23,
   STBTT_MAC_LANG_DUTCH        =4 ,   STBTT_MAC_LANG_RUSSIAN      =32,
   STBTT_MAC_LANG_FRENCH       =1 ,   STBTT_MAC_LANG_SPANISH      =6 ,
   STBTT_MAC_LANG_GERMAN       =2 ,   STBTT_MAC_LANG_SWEDISH      =5 ,
   STBTT_MAC_LANG_HEBREW       =10,   STBTT_MAC_LANG_CHINESE_SIMPLIFIED =33,
   STBTT_MAC_LANG_ITALIAN      =3 ,   STBTT_MAC_LANG_CHINESE_TRAD =19
};

#ifdef __cplusplus
}
#endif

#endif // __STB_INCLUDE_STB_TRUETYPE_H__


// FULL VERSION HISTORY
//
//   1.25 (2021-07-11) many fixes
//   1.24 (2020-02-05) fix warning
//   1.23 (2020-02-02) query SVG data for glyphs; query whole kerning table (but only kern not GPOS)
//   1.22 (2019-08-11) minimize missing-glyph duplication; fix kerning if both 'GPOS' and 'kern' are defined
//   1.21 (2019-02-25) fix warning
//   1.20 (2019-02-07) PackFontRange skips missing codepoints; GetScaleFontVMetrics()
//   1.19 (2018-02-11) OpenType GPOS kerning (horizontal only), STBTT_fmod
//   1.18 (2018-01-29) add missing function
//   1.17 (2017-07-23) make more arguments const; doc fix
//   1.16 (2017-07-12) SDF support
//   1.15 (2017-03-03) make more arguments const
//   1.14 (2017-01-16) num-fonts-in-TTC function
//   1.13 (2017-01-02) support OpenType fonts, certain Apple fonts
//   1.12 (2016-10-25) suppress warnings about casting away const with -Wcast-qual
//   1.11 (2016-04-02) fix unused-variable warning
//   1.10 (2016-04-02) allow user-defined fabs() replacement
//                     fix memory leak if fontsize=0.0
//                     fix warning from duplicate typedef
//   1.09 (2016-01-16) warning fix; avoid crash on outofmem; use alloc userdata for PackFontRanges
//   1.08 (2015-09-13) document stbtt_Rasterize(); fixes for vertical & horizontal edges
//   1.07 (2015-08-01) allow PackFontRanges to accept arrays of sparse codepoints;
//                     allow PackFontRanges to pack and render in separate phases;
//                     fix stbtt_GetFontOFfsetForIndex (never worked for non-0 input?);
//                     fixed an assert() bug in the new rasterizer
//                     replace assert() with STBTT_assert() in new rasterizer
//   1.06 (2015-07-14) performance improvements (~35% faster on x86 and x64 on test machine)
//                     also more precise AA rasterizer, except if shapes overlap
//                     remove need for STBTT_sort
//   1.05 (2015-04-15) fix misplaced definitions for STBTT_STATIC
//   1.04 (2015-04-15) typo in example
//   1.03 (2015-04-12) STBTT_STATIC, fix memory leak in new packing, various fixes
//   1.02 (2014-12-10) fix various warnings & compile issues w/ stb_rect_pack, C++
//   1.01 (2014-12-08) fix subpixel position when oversampling to exactly match
//                        non-oversampled; STBTT_POINT_SIZE for packed case only
//   1.00 (2014-12-06) add new PackBegin etc. API, w/ support for oversampling
//   0.99 (2014-09-18) fix multiple bugs with subpixel rendering (ryg)
//   0.9  (2014-08-07) support certain mac/iOS fonts without an MS platformID
//   0.8b (2014-07-07) fix a warning
//   0.8  (2014-05-25) fix a few more warnings
//   0.7  (2013-09-25) bugfix: subpixel glyph bug fixed in 0.5 had come back
//   0.6c (2012-07-24) improve documentation
//   0.6b (2012-07-20) fix a few more warnings
//   0.6  (2012-07-17) fix warnings; added stbtt_ScaleForMappingEmToPixels,
//                        stbtt_GetFontBoundingBox, stbtt_IsGlyphEmpty
//   0.5  (2011-12-09) bugfixes:
//                        subpixel glyph renderer computed wrong bounding box
//                        first vertex of shape can be off-curve (FreeSans)
//   0.4b (2011-12-03) fixed an error in the font baking example
//   0.4  (2011-12-01) kerning, subpixel rendering (tor)
//                    bugfixes for:
//                        codepoint-to-glyph conversion using table fmt=12
//                        codepoint-to-glyph conversion using table fmt=4
//                        stbtt_GetBakedQuad with non-square texture (Zer)
//                    updated Hello World! sample to use kerning and subpixel
//                    fixed some warnings
//   0.3  (2009-06-24) cmap fmt=12, compound shapes (MM)
//                    userdata, malloc-from-userdata, non-zero fill (stb)
//   0.2  (2009-03-11) Fix unsigned/signed char warnings
//   0.1  (2009-03-09) First public release
//

/*
------------------------------------------------------------------------------
This software is available under 2 licenses -- choose whichever you prefer.
------------------------------------------------------------------------------
ALTERNATIVE A - MIT License
Copyright (c) 2017 Sean Barrett
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
------------------------------------------------------------------------------
ALTERNATIVE B - Public Domain (www.unlicense.org)
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.
In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
------------------------------------------------------------------------------
*/


typedef struct FL_FontAtlas {
  FL_Allocator allocator;
  stbtt_pack_context pack_context;
  FL_Texture texture;
  bool texture_created;
} FL_FontAtlas;

FL_FontAtlas *FL_FontAtlas_Create(FL_i16 width, FL_i16 height,
                                  FL_Allocator allocator);

void FL_FontAtlas_Destroy(FL_FontAtlas *atlas);

void FL_FontAtlas_EnsureTextureCreated(FL_FontAtlas *atlas, FL_PaintingContext *context);

typedef struct FL_PackedCharKey {
  stbtt_fontinfo *info;
  FL_i32 codepoint;
  FL_i32 pixel_height;
} FL_PackedCharKey;

static inline FL_u64 FL_PackedCharKey_Hash(FL_PackedCharKey key) {
  FL_usize addr = (FL_usize)key.info;
  FL_u64 hash =
      FL_Str_Hash((FL_Str){.ptr = (char *)&addr, .len = sizeof(addr)});

  hash = FL_Str_HashWithSeed(
      (FL_Str){.ptr = (char *)&key.codepoint, .len = sizeof(key.codepoint)},
      hash);

  hash = FL_Str_HashWithSeed((FL_Str){.ptr = (char *)&key.pixel_height,
                                      .len = sizeof(key.pixel_height)},
                             hash);

  return hash;
}

static inline bool FL_PackedCharKey_IsEqual(FL_PackedCharKey a,
                                            FL_PackedCharKey b) {
  return a.info == b.info && a.codepoint == b.codepoint &&
         a.pixel_height == b.pixel_height;
}

typedef struct FL_PackedChar {
  FL_FontAtlas *atlas;
  stbtt_packedchar chardata;
  bool uploaded;
} FL_PackedChar;

FL_HASH_TRIE(FL_PackedCharMap, FL_PackedCharKey, FL_PackedCharKey_Hash,
             FL_PackedCharKey_IsEqual, FL_PackedChar)

struct FL_Font {
  FL_Arena *arena;
  stbtt_fontinfo info;
  FL_PackedCharMap *packed_char_map;
};

FL_PackedChar *FL_Font_GetOrPackChar(FL_Font *font, FL_i32 ch,
                                     FL_f32 pixel_height);

stbtt_aligned_quad FL_PackedChar_GetQuadAndAdvancePos(FL_PackedChar *ch,
                                                      FL_f32 *pos_x,
                                                      FL_f32 *baseline);

#include <stddef.h>
#include <stdint.h>


struct State;
typedef struct State State;

typedef struct Build {
  State *state;
  FL_Arena *arena;
  FL_isize index;
  FL_f32 delta_time;
  FL_f32 fast_animation_rate;
  FL_Widget *root;
  FL_CommandBuffer command_buffer;
} Build;

typedef struct FL_HitTestEntry FL_HitTestEntry;
struct FL_HitTestEntry {
  FL_HitTestEntry *prev;
  FL_HitTestEntry *next;

  FL_Widget *widget;

  /**
   * Describing how `FL_PointerEvent` delivered to this `FL_HitTestEntry` should
   * be transformed from the global coordinate space of the screen to the local
   * coordinate space of `widget`.
   */
  FL_Trans2 transform;

  FL_Vec2 local_position;
};

struct FL_HitTestContext {
  FL_Arena *arena;

  FL_HitTestEntry *first;
  FL_HitTestEntry *last;

  /** position in the screen coordinate space. */
  FL_Vec2 position;

  /** Describing how `position` was transformed to `local_position`. */
  FL_Trans2 transform;

  /**
   * Position relative to widget's local coordinate system. (0, 0) is the
   * top-left of the box.
   */
  FL_Vec2 local_position;
};

struct State {
  FL_Arena *arena;

  Build builds[2];
  FL_isize build_index;
  Build *curr_build;
  Build *last_build;

  FL_i32 next_context_id;
  FL_i32 next_notification_id;

  FL_f32 pixels_per_point;
  FL_f32 points_per_pixel;
  FL_Font *default_font;
  FL_FontAtlas *font_atlas;

  // -- Input ------------------------------------------------------------------
  FL_Vec2 mouse_pos;
  FL_i32 next_pointer_event_id;
  FL_u32 down_button;
  FL_i32 down_pointer;
  FL_i32 next_down_pointer;
  FL_PointerEventResolver resolver;

  // hit test result for button down.
  FL_HitTestContext button_down_hit_test_context;
  // hit test result for mouse move
  FL_HitTestContext button_move_hit_test_contexts[2];
  FL_i32 button_move_hit_test_context_index;

  FL_GestureArenaState gesture_arena_state;
};

State *GetGlobalState(void);

#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


static void *FL_Allocator_Default(void *ctx, void *ptr, FL_isize old_size,
                                  FL_isize new_size) {
  void *result = 0;
  if (!old_size) {
    result = malloc(new_size);
  } else if (!new_size) {
    free(ptr);
  } else {
    result = realloc(ptr, new_size);
  }
  return result;
}

FL_Allocator FL_Allocator_GetDefault(void) {
  return (FL_Allocator){0, &FL_Allocator_Default};
}
static void *FL_CountingAllocator_Impl(void *ctx, void *ptr, FL_isize old_size,
                                       FL_isize new_size) {
  FL_CountingAllocator *ca = ctx;
  ca->count += new_size - old_size;
  return ca->parent.alloc(ca->parent.ctx, ptr, old_size, new_size);
}

static void FL_CountingAllocator_Free(void *ctx, void *ptr, ptrdiff_t size) {
  FL_CountingAllocator *ca = ctx;
  ca->count -= size;
  FL_Allocator_Free(ca->parent, ptr, size);
}

FL_Allocator FL_CountingAllocator_AsAllocator(FL_CountingAllocator *ca) {
  return (FL_Allocator){ca, &FL_CountingAllocator_Impl};
}

typedef struct FL_ArenaState {
  FL_Allocator allocator;
  FL_isize page_size;
} FL_ArenaState;

static inline bool IsPowerOfTwo(FL_isize val) { return (val & (val - 1)) == 0; }

static FL_isize AlignBackward(FL_isize addr, FL_isize alignment) {
  FL_DEBUG_ASSERT(IsPowerOfTwo(alignment));
  return addr & ~(alignment - 1);
}

static FL_isize AlignForward(FL_isize addr, FL_isize alignment) {
  FL_DEBUG_ASSERT(IsPowerOfTwo(alignment));
  return AlignBackward(addr + (alignment - 1), alignment);
}

static bool IsAligned(FL_isize addr, FL_isize alignment) {
  return AlignBackward(addr, alignment) == addr;
}

static FL_MemoryBlock *FL_MemoryBlock_Create(FL_isize size,
                                             FL_ArenaState *state) {
  FL_isize block_size =
      AlignForward(size + sizeof(FL_MemoryBlock), state->page_size);
  char *ptr = FL_Allocator_Alloc(state->allocator, block_size);
  FL_MemoryBlock *block = (FL_MemoryBlock *)(ptr + block_size) - 1;
  FL_ASSERTF(block, "out of memory");
  FL_ASSERT(IsAligned((FL_isize)block, alignof(FL_MemoryBlock)));
  *block = (FL_MemoryBlock){
      .state = state,
      .begin = ptr,
  };
  return block;
}

static void FL_MemoryBlock_Destroy(FL_MemoryBlock *block) {
  FL_isize block_size = (char *)block - block->begin + sizeof(FL_MemoryBlock);
  FL_ArenaState *state = block->state;
  FL_Allocator_Free(state->allocator, block->begin, block_size);
}

FL_Arena *FL_Arena_Create(const FL_ArenaOptions *opts) {
  FL_ASSERTF(IsPowerOfTwo(opts->page_size), "page_size must be power of two");
  FL_ArenaState tmp_state = {
      .allocator = opts->allocator,
      .page_size = opts->page_size,
  };
  if (!tmp_state.allocator.alloc) {
    tmp_state.allocator = FL_Allocator_GetDefault();
  }
  if (!tmp_state.page_size) {
    tmp_state.page_size = 4096;
  }

  FL_MemoryBlock *block = FL_MemoryBlock_Create(
      sizeof(FL_ArenaState) + sizeof(FL_Arena), &tmp_state);
  FL_Arena bootstrap = {
      .begin = block->begin,
      .end = (char *)block,
  };

  FL_ArenaState *state = FL_Arena_PushStruct(&bootstrap, FL_ArenaState);
  *state = tmp_state;
  block->state = state;

  FL_Arena *arena = FL_Arena_PushStruct(&bootstrap, FL_Arena);
  *arena = bootstrap;

  return arena;
}

void FL_Arena_Destroy(FL_Arena *arena) {
  FL_MemoryBlock *block = FL_Arena_GetMemoryBlock(arena);
  while (block->next) {
    block = block->next;
  }
  while (block) {
    FL_MemoryBlock *prev = block->prev;
    FL_MemoryBlock_Destroy(block);
    block = prev;
  }
}

void FL_Arena_Reset(FL_Arena *arena) {
  FL_MemoryBlock *block = FL_Arena_GetMemoryBlock(arena);
  while (block->prev) {
    block = block->prev;
  }
  arena->begin = block->begin;
  arena->end = (char *)block;

  // Keep the memory space for internal state. See FL_Arena_Create.
  FL_Arena_PushStruct(arena, FL_ArenaState);
  FL_Arena_PushStruct(arena, FL_Arena);
}

static void *FL_Arena_AllocatorImpl(void *ctx, void *ptr, FL_isize old_size,
                                    FL_isize new_size) {
  void *result = 0;
  FL_Arena *arena = ctx;
  if (!old_size) {
    result = FL_Arena_Push(arena, new_size, alignof(max_align_t));
  } else {
    FL_Arena temp = *arena;
    if (FL_Arena_Pop(arena, old_size) != ptr) {
      *arena = temp;
    }

    if (new_size) {
      result = FL_Arena_Push(arena, new_size, alignof(max_align_t));
      if (result != ptr) {
        memcpy(result, ptr, old_size);
      }
    }
  }

  return result;
}

FL_Allocator FL_Arena_AsAllocator(FL_Arena *arena) {
  return (FL_Allocator){
      .ctx = arena,
      .alloc = FL_Arena_AllocatorImpl,
  };
}

FL_MemoryBlock *FL_Arena_GetMemoryBlock(FL_Arena *arena) {
  return (FL_MemoryBlock *)arena->end;
}

FL_Allocator FL_Arena_GetAllocator(FL_Arena *arena) {
  FL_MemoryBlock *block = FL_Arena_GetMemoryBlock(arena);
  FL_ArenaState *state = block->state;
  return state->allocator;
}

void *FL_Arena_Push(FL_Arena *arena, FL_isize size, FL_isize alignment) {
  FL_ASSERT(size >= 0);
  char *addr = (char *)AlignForward((FL_isize)arena->begin, alignment);
  while ((addr + size) > arena->end) {
    FL_MemoryBlock *block = FL_Arena_GetMemoryBlock(arena);
    FL_MemoryBlock *next = block->next;
    if (!next) {
      next = FL_MemoryBlock_Create(size, block->state);
      next->prev = block;
      block->next = next;
    }
    arena->begin = next->begin;
    arena->end = (char *)next;

    addr = (char *)AlignForward((FL_isize)arena->begin, alignment);
  }

  arena->begin = addr + size;

  return addr;
}

void *FL_Arena_Pop(FL_Arena *arena, FL_isize size) {
  FL_MemoryBlock *block = FL_Arena_GetMemoryBlock(arena);
  while (block) {
    char *new_begin = arena->begin - size;
    if (new_begin >= block->begin) {
      arena->begin = new_begin;
      return new_begin;
    }

    size -= arena->begin - block->begin;
    block = block->prev;
    FL_ASSERTF(block, "arena overflow");
    arena->end = (char *)block;
    arena->begin = arena->end;
  }
  FL_UNREACHABLE;
}

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>


FL_Str FL_Str_FormatV(FL_Arena *arena, const char *format, va_list ap) {
  FL_isize buf_len = 256;
  char *buf_ptr = FL_Arena_PushArray(arena, char, buf_len);

  va_list args;
  va_copy(args, ap);
  FL_isize str_len = vsnprintf(buf_ptr, buf_len, format, args);

  if (str_len <= buf_len) {
    // Free the unused part of the buffer.
    FL_Arena_Pop(arena, buf_len - str_len);
  } else {
    // The buffer was too small. We need to resize it and try again.
    FL_Arena_Pop(arena, buf_len);
    buf_len = str_len;
    buf_ptr = FL_Arena_PushArray(arena, char, buf_len);
    va_copy(args, ap);
    vsnprintf(buf_ptr, buf_len, format, args);
  }

  return (FL_Str){buf_ptr, str_len};
}

FL_Str FL_Str_Format(FL_Arena *arena, const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  FL_Str result = FL_Str_FormatV(arena, format, ap);
  va_end(ap);
  return result;
}

FL_u64 FL_Str_HashWithSeed(FL_Str s, FL_u64 seed) {
  // FNV-style hash
  FL_u64 h = seed;
  for (FL_isize i = 0; i < s.len; i++) {
    h ^= s.ptr[i];
    h *= 1111111111111111111u;
  }
  return h;
}


FL_FontAtlas *FL_FontAtlas_Create(FL_i16 width, FL_i16 height,
                                  FL_Allocator allocator) {
  FL_FontAtlas *atlas = FL_Allocator_Alloc(allocator, sizeof(FL_FontAtlas));
  *atlas = (FL_FontAtlas){
      .allocator = allocator,
  };

  FL_u8 *pixels = FL_Allocator_Alloc(allocator, width * height);
  int ok =
      stbtt_PackBegin(&atlas->pack_context, pixels, width, height, 0, 1, 0);
  FL_ASSERT(ok);

  // Top left corner of each font atlas is used as "white texture".
  stbrp_rect rect = {
      .w = 1,
      .h = 1,
  };
  stbtt_PackFontRangesPackRects(&atlas->pack_context, &rect, 1);
  FL_ASSERT(rect.x == 0);
  FL_ASSERT(rect.y == 0);

  stbtt_PackSetOversampling(&atlas->pack_context, 2, 2);
  return atlas;
}

void FL_FontAtlas_Destroy(FL_FontAtlas *atlas) {
  stbtt_PackEnd(&atlas->pack_context);
  FL_Allocator_Free(atlas->allocator, atlas->pack_context.pixels,
                    atlas->pack_context.width * atlas->pack_context.height);
  FL_Allocator_Free(atlas->allocator, atlas, sizeof(FL_FontAtlas));
}

void FL_FontAtlas_EnsureTextureCreated(FL_FontAtlas *atlas,
                                       FL_PaintingContext *context) {
  if (!atlas->texture_created) {
    FL_ASSERT(atlas->pack_context.width > 0 && atlas->pack_context.height > 0);
    FL_u32 *pixels = FL_Arena_PushArray(
        context->arena, FL_u32,
        atlas->pack_context.width * atlas->pack_context.height);
    memset(pixels, 0,
           sizeof(FL_u32) * atlas->pack_context.width *
               atlas->pack_context.height);
    pixels[0] = 0xFFFFFFFF;
    FL_Draw_CreateTexture(context, &atlas->texture, atlas->pack_context.width,
                          atlas->pack_context.height, pixels);
    atlas->texture_created = true;
  }
}

FL_Font *FL_Font_Load(const FL_FontOptions *opts) {
  State *state = GetGlobalState();
  FL_Allocator allocator = FL_Arena_GetAllocator(state->arena);

  FL_Arena *arena = FL_Arena_Create(&(FL_ArenaOptions){
      .allocator = allocator,
  });
  FL_Font *font = FL_Arena_PushStruct(arena, FL_Font);
  *font = (FL_Font){
      .arena = arena,
  };

  int offset = stbtt_GetFontOffsetForIndex(opts->data, opts->index);
  if (stbtt_InitFont(&font->info, opts->data, offset)) {
    if (!state->default_font) {
      state->default_font = font;
    }
  } else {
    FL_Arena_Destroy(arena);
    font = 0;
  }

  return font;
}

void FL_Font_Destroy(FL_Font *font) { FL_Arena_Destroy(font->arena); }

FL_PackedChar *FL_Font_GetOrPackChar(FL_Font *font, FL_i32 ch,
                                     FL_f32 pixel_height) {
  FL_PackedCharKey key = {
      .info = &font->info,
      .codepoint = ch,
      .pixel_height = (FL_i32)FL_Round(pixel_height),
  };
  FL_PackedChar *packed_char;
  if (FL_PackedCharMap_Upsert(&font->packed_char_map, key, &packed_char,
                              font->arena)) {
    *packed_char = (FL_PackedChar){};

    State *state = GetGlobalState();
    FL_FontAtlas *atlas = state->font_atlas;
    while (true) {
      stbtt_pack_range range = {
          .font_size = (FL_f32)key.pixel_height,
          .first_unicode_codepoint_in_range = ch,
          .num_chars = 1,
          .chardata_for_range = &packed_char->chardata,
      };
      stbrp_rect rect;
      int num_rects = stbtt_PackFontRangesGatherRects(
          &atlas->pack_context, &font->info, &range, 1, &rect);
      stbtt_PackFontRangesPackRects(&atlas->pack_context, &rect, num_rects);
      if (stbtt_PackFontRangesRenderIntoRects(&atlas->pack_context, &font->info,
                                              &range, 1, &rect)) {
        packed_char->atlas = atlas;
        break;
      } else {
        // TODO: Create another atlas.
        FL_ASSERT(false);
      }
    }
  }
  return packed_char;
}

stbtt_aligned_quad FL_PackedChar_GetQuadAndAdvancePos(FL_PackedChar *ch,
                                                      FL_f32 *pos_x,
                                                      FL_f32 *pos_y) {
  stbtt_aligned_quad quad;
  stbtt_GetPackedQuad(&ch->chardata, ch->atlas->pack_context.width,
                      ch->atlas->pack_context.height, 0, pos_x, pos_y, &quad,
                      0);
  return quad;
}

FL_Font_TextMetrics FL_Font_MeasureText(FL_Font *font, FL_Str text,
                                        FL_f32 font_size, FL_f32 max_width) {
  State *state = GetGlobalState();
  if (!font) {
    font = state->default_font;
    if (!font) {
      return (FL_Font_TextMetrics){0};
    }
  }

  FL_f32 pixels_per_point = state->pixels_per_point;
  FL_f32 points_per_pixel = state->points_per_pixel;
  FL_f32 pixel_height = font_size * pixels_per_point;

  max_width = max_width * pixels_per_point;

  FL_f32 scale = stbtt_ScaleForPixelHeight(&font->info, pixel_height);
  FL_i32 ascent, descent, line_gap;
  stbtt_GetFontVMetrics(&font->info, &ascent, &descent, &line_gap);

  FL_f32 pos_x = 0.0f;
  FL_f32 pos_y = 0.0f;

  FL_isize i = 0;
  for (; i < text.len; ++i) {
    if (pos_x > max_width) {
      break;
    }

    FL_PackedChar *packed_char =
        FL_Font_GetOrPackChar(font, text.ptr[i], pixel_height);
    FL_PackedChar_GetQuadAndAdvancePos(packed_char, &pos_x, &pos_y);
    // TODO: Kerning
  }

  return (FL_Font_TextMetrics){
      .width = pos_x * points_per_pixel,
      .font_bounding_box_ascent = (FL_f32)ascent * scale * points_per_pixel,
      .font_bounding_box_descent = (FL_f32)-descent * scale * points_per_pixel,
      .char_count = i,
  };
}


void FL_Draw_CreateTexture(FL_PaintingContext *context, FL_Texture *texture,
                           FL_i32 width, FL_i32 height, FL_u32 *pixels) {
  FL_ASSERT(!texture->id);
  FL_CommandBuffer *command_buffer = context->command_buffer;
  *FL_TextureCommandArray_Add(&command_buffer->texture_commands,
                              command_buffer->allocator) = (FL_TextureCommand){
      .texture = texture,
      .width = width,
      .height = height,
      .pixels = pixels,
  };
}

void FL_Draw_UpdateTexture(FL_PaintingContext *context, FL_Texture *texture,
                           FL_i32 x, FL_i32 y, FL_i32 width, FL_i32 height,
                           FL_u32 *pixels) {
  FL_CommandBuffer *command_buffer = context->command_buffer;
  *FL_TextureCommandArray_Add(&command_buffer->texture_commands,
                              command_buffer->allocator) = (FL_TextureCommand){
      .texture = texture,
      .x = x,
      .y = y,
      .width = width,
      .height = height,
      .pixels = pixels,
  };
}

void FL_Draw_PushClipRect(FL_PaintingContext *context, FL_Rect rect) {
  *FL_RectArray_Add(&context->clip_rect_array,
                    FL_Arena_AsAllocator(context->arena)) = rect;
}

void FL_Draw_PopClipRect(FL_PaintingContext *context) {
  if (context->clip_rect_array.len > 0) {
    context->clip_rect_array.len -= 1;
  }
}

static FL_Rect GetClipRect(FL_PaintingContext *context) {
  if (context->clip_rect_array.len > 0) {
    return context->clip_rect_array.ptr[context->clip_rect_array.len - 1];
  } else {
    return context->viewport;
  }
}

void FL_Draw_AddTexture(FL_PaintingContext *context, FL_Rect dst_rect,
                        FL_Rect src_rect, FL_Texture *texture, FL_u32 color) {
  FL_CommandBuffer *command_buffer = context->command_buffer;
  FL_DrawIndex vertex_offset = (FL_DrawIndex)command_buffer->vertex_buffer.len;
  FL_ASSERT((FL_isize)vertex_offset == command_buffer->vertex_buffer.len);
  *FL_DrawVertexArray_Add(&command_buffer->vertex_buffer,
                          command_buffer->allocator) = (FL_DrawVertex){
      .pos = {dst_rect.left, dst_rect.top},
      .uv = {src_rect.left, src_rect.top},
      .color = color,
  };
  *FL_DrawVertexArray_Add(&command_buffer->vertex_buffer,
                          command_buffer->allocator) = (FL_DrawVertex){
      .pos = {dst_rect.right, dst_rect.top},
      .uv = {src_rect.right, src_rect.top},
      .color = color,
  };
  *FL_DrawVertexArray_Add(&command_buffer->vertex_buffer,
                          command_buffer->allocator) = (FL_DrawVertex){
      .pos = {dst_rect.right, dst_rect.bottom},
      .uv = {src_rect.right, src_rect.bottom},
      .color = color,
  };
  *FL_DrawVertexArray_Add(&command_buffer->vertex_buffer,
                          command_buffer->allocator) = (FL_DrawVertex){
      .pos = {dst_rect.left, dst_rect.bottom},
      .uv = {src_rect.left, src_rect.bottom},
      .color = color,
  };

  FL_DrawIndex index_offset = (FL_DrawIndex)command_buffer->index_buffer.len;
  FL_ASSERT((FL_isize)index_offset == command_buffer->index_buffer.len);
  *FL_DrawIndexArray_Add(&command_buffer->index_buffer,
                         command_buffer->allocator) = vertex_offset + 0;
  *FL_DrawIndexArray_Add(&command_buffer->index_buffer,
                         command_buffer->allocator) = vertex_offset + 1;
  *FL_DrawIndexArray_Add(&command_buffer->index_buffer,
                         command_buffer->allocator) = vertex_offset + 2;
  *FL_DrawIndexArray_Add(&command_buffer->index_buffer,
                         command_buffer->allocator) = vertex_offset + 0;
  *FL_DrawIndexArray_Add(&command_buffer->index_buffer,
                         command_buffer->allocator) = vertex_offset + 2;
  *FL_DrawIndexArray_Add(&command_buffer->index_buffer,
                         command_buffer->allocator) = vertex_offset + 3;

  FL_Rect clip_rect = GetClipRect(context);
  FL_DrawCommandArray *draw_commands = &command_buffer->draw_commands;
  FL_DrawCommand *current_draw_command = 0;
  if (draw_commands->len > 0) {
    current_draw_command = draw_commands->ptr + (draw_commands->len - 1);
  }
  if (current_draw_command &&
      current_draw_command->index_offset + current_draw_command->index_count ==
          index_offset &&
      (!texture || !current_draw_command->texture ||
       current_draw_command->texture == texture) &&
      FL_Rect_IsEqual(current_draw_command->clip_rect, clip_rect)) {
    if (!current_draw_command->texture && texture) {
      current_draw_command->texture = texture;
    }
    current_draw_command->index_count += 6;
  } else {
    *FL_DrawCommandArray_Add(&command_buffer->draw_commands,
                             command_buffer->allocator) = (FL_DrawCommand){
        .clip_rect = GetClipRect(context),
        .texture = texture,
        .index_offset = index_offset,
        .index_count = 6,
    };
  }
}

void FL_Draw_AddRectLines(FL_PaintingContext *context, FL_Rect rect,
                          FL_u32 color, FL_f32 line_width) {
  FL_f32 left = rect.left;
  FL_f32 right = rect.right;
  FL_f32 top = rect.top;
  FL_f32 bottom = rect.bottom;
  FL_f32 half_width = line_width / 2.0f;

  FL_Draw_AddRect(context,
                  (FL_Rect){left - half_width, right + half_width,
                            top - half_width, top + half_width},
                  color);
  FL_Draw_AddRect(context,
                  (FL_Rect){left - half_width, left + half_width,
                            top + half_width, bottom + half_width},
                  color);
  FL_Draw_AddRect(context,
                  (FL_Rect){right - half_width, right + half_width,
                            top + half_width, bottom + half_width},
                  color);
  FL_Draw_AddRect(context,
                  (FL_Rect){left + half_width, right - half_width,
                            bottom - half_width, bottom + half_width},
                  color);
}

void FL_Draw_AddRect(FL_PaintingContext *context, FL_Rect rect, FL_u32 color) {
  FL_Draw_AddTexture(context, rect, (FL_Rect){0}, 0, color);
}

void FL_Draw_AddText(FL_PaintingContext *context, FL_Font *font, FL_Str text,
                     FL_f32 x, FL_f32 y, FL_u32 color, FL_f32 font_size) {
  State *state = GetGlobalState();
  if (!font) {
    font = state->default_font;
    if (!font) {
      return;
    }
  }

  FL_f32 pixels_per_point = state->pixels_per_point;
  FL_f32 points_per_pixel = state->points_per_pixel;
  FL_f32 pixel_height = font_size * pixels_per_point;

  FL_f32 scale = stbtt_ScaleForPixelHeight(&font->info, pixel_height);
  FL_i32 ascent, descent, line_gap;
  stbtt_GetFontVMetrics(&font->info, &ascent, &descent, &line_gap);

  FL_f32 pos_x = x * pixels_per_point;
  FL_f32 baseline = (FL_f32)ascent * scale;
  FL_f32 pos_y = y * pixels_per_point + baseline;

  for (FL_isize i = 0; i < text.len; ++i) {
    FL_PackedChar *packed_char =
        FL_Font_GetOrPackChar(font, text.ptr[i], pixel_height);
    stbtt_aligned_quad quad =
        FL_PackedChar_GetQuadAndAdvancePos(packed_char, &pos_x, &pos_y);
    FL_f32 quad_w = quad.x1 - quad.x0;
    FL_f32 quad_h = quad.y1 - quad.y0;
    if (quad_w > 0 && quad_h > 0) {
      FL_FontAtlas *atlas = packed_char->atlas;
      FL_FontAtlas_EnsureTextureCreated(atlas, context);

      if (!packed_char->uploaded) {
        int x = packed_char->chardata.x0;
        int y = packed_char->chardata.y0;
        int width = packed_char->chardata.x1 - packed_char->chardata.x0;
        int height = packed_char->chardata.y1 - packed_char->chardata.y0;
        FL_u32 *pixels =
            FL_Arena_PushArray(context->arena, FL_u32, width * height);

        FL_u32 *dst_row = pixels;
        FL_u8 *src_row =
            atlas->pack_context.pixels + y * atlas->pack_context.width + x;
        for (FL_i16 j = 0; j < height; ++j) {
          FL_u32 *dst = dst_row;
          FL_u8 *src = src_row;
          for (FL_i16 i = 0; i < width; ++i) {
            FL_u32 alpha = *src++;
            (*dst++) = FL_COLOR_RGBA(255, 255, 255, alpha);
          }
          dst_row += width;
          src_row += atlas->pack_context.width;
        }

        FL_Draw_UpdateTexture(context, &atlas->texture, x, y, width, height,
                              pixels);
        packed_char->uploaded = true;
      }

      FL_Rect src_rect = {
          .left = quad.s0,
          .top = quad.t0,
          .right = quad.s1,
          .bottom = quad.t1,
      };
      FL_Rect dst_rect = {
          .left = quad.x0 * points_per_pixel,
          .top = quad.y0 * points_per_pixel,
          .right = quad.x1 * points_per_pixel,
          .bottom = quad.y1 * points_per_pixel,
      };
      FL_Draw_AddTexture(context, dst_rect, src_rect,
                         &packed_char->atlas->texture, color);
    }

    // TODO: Kerning
  }
}

#include <stdint.h>


bool FL_PointerEventResolver_Register(struct FL_Widget *widget) {
  Build *build = widget->build;
  State *state = build->state;
  FL_PointerEventResolver *resolver = &state->resolver;
  if (!resolver->widget) {
    resolver->widget = widget;
    return true;
  }
  return false;
}

void FL_PointerEventResolver_Reset(FL_PointerEventResolver *resolver) {
  (*resolver) = (FL_PointerEventResolver){0};
}

static void fl_gesture_arena_free(FL_GestureArena *arena) {
  FL_GestureArenaState *state = arena->state;

  FL_DLIST_REMOVE(state->first_arena, state->last_arena, arena, prev, next);

  for (FL_GestureArenaEntry *entry = arena->first; entry; entry = entry->next) {
    entry->active = false;
  }
  FL_DLIST_CONCAT(state->first_free_entry, state->last_free_entry, arena->first,
                  arena->last, prev, next);

  FL_DLIST_ADD_LAST(state->first_free_arena, state->last_free_arena, arena,
                    prev, next);
}

static inline void fl_gesture_arena_member_accept(FL_GestureArenaMember member,
                                                  FL_i32 pointer) {
  member.ops->accept(member.ptr, pointer);
}

static inline void fl_gesture_arena_member_reject(FL_GestureArenaMember member,
                                                  FL_i32 pointer) {
  member.ops->reject(member.ptr, pointer);
}

static void FL_GestureArena_Resolve_by_default(FL_GestureArena *arena) {
  FL_ASSERT(!arena->open);
  FL_ASSERT(arena->first && arena->first == arena->last);

  fl_gesture_arena_member_accept(arena->first->member, arena->pointer);
  fl_gesture_arena_free(arena);
}

static void FL_GestureArena_Resolve_in_favor_of(FL_GestureArena *arena,
                                                FL_GestureArenaEntry *winner) {
  FL_ASSERT(!arena->open);
  FL_ASSERT(!arena->eager_winner || arena->eager_winner == winner);

  for (FL_GestureArenaEntry *entry = arena->first; entry; entry = entry->next) {
    if (entry != winner) {
      fl_gesture_arena_member_reject(entry->member, arena->pointer);
    }
  }
  fl_gesture_arena_member_accept(winner->member, arena->pointer);
  fl_gesture_arena_free(arena);
}

static void fl_gesture_arena_try_resolve(FL_GestureArena *arena) {
  if (!arena->first) {
    fl_gesture_arena_free(arena);
  } else if (arena->first == arena->last) {
    FL_GestureArena_Resolve_by_default(arena);
  } else if (arena->eager_winner) {
    FL_GestureArena_Resolve_in_favor_of(arena, arena->eager_winner);
  }
}

FL_GestureArena *FL_GestureArena_Open(FL_GestureArenaState *state,
                                      FL_i32 pointer, FL_Arena *arena) {
  FL_ASSERT(!FL_GestureArena_Get(state, pointer));

  FL_GestureArena *gesture_arena = state->first_free_arena;
  if (gesture_arena) {
    FL_DLIST_REMOVE_FIRST(state->first_free_arena, state->last_free_entry, prev,
                          next);
  } else {
    gesture_arena = FL_Arena_PushStruct(arena, FL_GestureArena);
  }

  *gesture_arena = (FL_GestureArena){
      .state = state,
      .pointer = pointer,
      .open = true,
  };

  FL_DLIST_ADD_LAST(state->first_arena, state->last_arena, gesture_arena, prev,
                    next);

  return gesture_arena;
}

void FL_GestureArena_Close(FL_GestureArena *arena) {
  arena->open = false;
  fl_gesture_arena_try_resolve(arena);
}

FL_GestureArena *FL_GestureArena_Get(FL_GestureArenaState *state,
                                     FL_i32 pointer) {
  for (FL_GestureArena *gesture_arena = state->first_arena; gesture_arena;
       gesture_arena = gesture_arena->next) {
    if (gesture_arena->pointer == pointer) {
      return gesture_arena;
    }
  }
  return 0;
}

void FL_GestureArena_Sweep(FL_GestureArena *arena) {
  FL_ASSERT(!arena->open);

  // TODO: check for hold

  FL_GestureArenaEntry *winner = arena->first;
  if (winner) {
    // First member wins.
    fl_gesture_arena_member_accept(winner->member, arena->pointer);

    // Reject all the other members.
    for (FL_GestureArenaEntry *entry = winner->next; entry;
         entry = entry->next) {
      fl_gesture_arena_member_reject(entry->member, arena->pointer);
    }
  }

  fl_gesture_arena_free(arena);
}

FL_GestureArenaEntry *FL_GestureArena_Add(FL_i32 pointer,
                                          FL_GestureArenaMemberOps *ops,
                                          void *ctx) {
  FL_GestureArenaMember member = {.ops = ops, .ptr = ctx};
  State *state = GetGlobalState();
  FL_GestureArenaState *gesture_arena_state = &state->gesture_arena_state;
  FL_GestureArena *arena = FL_GestureArena_Get(gesture_arena_state, pointer);
  FL_ASSERTF(arena && arena->open, "gesture arena is not open");

  FL_GestureArenaEntry *entry = gesture_arena_state->first_free_entry;
  if (entry) {
    FL_DLIST_REMOVE_FIRST(gesture_arena_state->first_free_entry,
                          gesture_arena_state->last_free_entry, prev, next);
  } else {
    entry = FL_Arena_PushStruct(state->arena, FL_GestureArenaEntry);
  }

  (*entry) = (FL_GestureArenaEntry){
      .arena = arena,
      .member = member,
      .active = true,
  };

  FL_DLIST_ADD_LAST(arena->first, arena->last, entry, prev, next);
  return entry;
}

void FL_GestureArena_Update(FL_GestureArenaEntry *entry, void *ctx) {
  FL_ASSERT(entry->active);
  entry->member.ptr = ctx;
}

static void fl_gesture_arena_free_entry(FL_GestureArena *arena,
                                        FL_GestureArenaEntry *entry) {
  FL_GestureArenaState *state = arena->state;

  entry->active = false;

  FL_DLIST_REMOVE(arena->first, arena->last, entry, prev, next);
  FL_DLIST_ADD_LAST(state->first_free_entry, state->last_free_entry, entry,
                    prev, next);
}

void FL_GestureArena_Resolve(FL_GestureArenaEntry *entry, bool accepted) {
  FL_GestureArena *arena = entry->arena;
  if (accepted) {
    FL_GestureArena_Resolve_in_favor_of(arena, entry);
  } else {
    fl_gesture_arena_member_reject(entry->member, arena->pointer);
    fl_gesture_arena_free_entry(arena, entry);
  }
}

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>


static bool fl_struct_size_is_equal(FL_StructSize a, FL_StructSize b) {
  return a.size == b.size && a.alignment == b.alignment;
}

FL_Widget *FL_Widget_Create_(const FL_WidgetClass *klass, FL_Key key,
                             const void *props, FL_StructSize props_size) {
  FL_ASSERTF(fl_struct_size_is_equal(klass->props_size, props_size),
             "%s: expected {size=%td, alignment=%td}, but got {size=%td, "
             "alignment=%td}",
             klass->name, klass->props_size.size, klass->props_size.alignment,
             props_size.size, props_size.alignment);
  State *state = GetGlobalState();
  Build *build = state->curr_build;
  FL_Widget *widget = FL_Arena_PushStruct(build->arena, FL_Widget);
  *widget = (FL_Widget){
      .klass = klass,
      .key = key,
      .build = build,
      .props = FL_Arena_Dup(build->arena, props, props_size.size,
                            props_size.alignment),
  };
  return widget;
}

static bool can_reuse_widget(FL_Widget *curr, FL_Widget *last) {
  return last && !last->link && last->klass == curr->klass &&
         FL_Key_IsEqual(last->key, curr->key);
}

void FL_Widget_Mount(FL_Widget *parent, FL_Widget *widget) {
  FL_ASSERT(!widget->first && !widget->last);
  FL_ASSERT(widget->child_count == 0);
  FL_ASSERT(!widget->parent);
  FL_ASSERT(!widget->state);

  Build *build = widget->build;

  FL_Widget *last = 0;
  if (parent) {
    if (parent->link) {
      // TODO: use key to find widget from last build
    }

    if (!last) {
      last = parent->last_child_of_link;
      if (!can_reuse_widget(widget, last)) {
        last = 0;
      }
    }

    if (parent->last_child_of_link) {
      parent->last_child_of_link = parent->last_child_of_link->next;
    }

    widget->parent = parent;
    FL_DLIST_ADD_LAST(parent->first, parent->last, widget, prev, next);
    parent->child_count++;
  } else {
    State *state = build->state;
    FL_Widget *last_root = state->last_build->root;
    if (can_reuse_widget(widget, last_root)) {
      last = last_root;
    }
    widget->parent = 0;
    widget->prev = widget->next = 0;
  }

  if (last) {
    last->link = widget;

    widget->link = last;
    widget->size = last->size;
    widget->offset = last->offset;
    widget->last_child_of_link = last->first;
  }

  FL_StructSize state_size = widget->klass->state_size;
  if (state_size.size > 0) {
    widget->state =
        FL_Arena_Push(build->arena, state_size.size, state_size.alignment);
    if (widget->link) {
      memcpy(widget->state, widget->link->state, state_size.size);
    } else {
      memset(widget->state, 0, state_size.size);
    }
  }

  if (widget->klass->mount) {
    widget->klass->mount(widget);
  }
}

void FL_Widget_Unmount(FL_Widget *widget) {
  for (FL_Widget *child = widget->first; child; child = child->next) {
    FL_Widget_Unmount(child);
  }

  if (widget->klass->unmount) {
    widget->klass->unmount(widget);
  }
}

static uint64_t FL_ContextID_Hash(FL_ContextID id) { return id; }

static bool FL_ContextID_IsEqual(FL_ContextID a, FL_ContextID b) {
  return a == b;
}

FL_HASH_TRIE(FL_Context, FL_ContextID, FL_ContextID_Hash, FL_ContextID_IsEqual,
             FL_ContextData)

void *FL_Widget_GetContext_(FL_Widget *widget, FL_ContextID id, FL_isize size) {
  FL_ContextData *data;
  FL_Context_Upsert(&widget->context, id, &data, 0);
  if (data) {
    FL_ASSERTF(data->len == size,
               "%s: expected context value size %td, but got %td",
               widget->klass->name, data->len, size);
    return data->ptr;
  }
  return 0;
}

void *FL_Widget_SetContext_(FL_Widget *widget, FL_ContextID id,
                            FL_StructSize size) {
  FL_ASSERT(id != 0);
  Build *build = widget->build;
  FL_ContextData *data;
  FL_Context_Upsert(&widget->context, id, &data, build->arena);
  data->len = size.size;
  data->ptr = FL_Arena_Push(build->arena, size.size, size.alignment);
  return data->ptr;
}

void FL_Widget_Layout_Default(FL_Widget *widget,
                              FL_BoxConstraints constraints) {
  FL_Vec2 size = FL_BoxConstraints_GetSmallest(constraints);
  for (FL_Widget *child = widget->first; child; child = child->next) {
    FL_Widget_Layout(child, constraints);
    size = FL_Vec2_Max(size, child->size);
  }
  widget->size = size;
}

static bool FL_Widget_HitTestChildren(FL_Widget *widget,
                                      FL_HitTestContext *context) {
  FL_Trans2 parent_transform = context->transform;
  FL_Vec2 parent_local_position = context->local_position;

  bool hit = false;
  for (FL_Widget *child = widget->last; child; child = child->prev) {
    context->transform = FL_Trans2_Dot(
        parent_transform, FL_Trans2_Offset(-child->offset.x, -child->offset.y));
    context->local_position = FL_Vec2_Sub(parent_local_position, child->offset);

    hit = FL_Widget_HitTest(child, context);
    if (hit) {
      break;
    }
  }

  context->transform = parent_transform;
  context->local_position = parent_local_position;
  return hit;
}

bool FL_Widget_HitTest_DeferToChild(FL_Widget *widget,
                                    FL_HitTestContext *context) {
  if (!FL_Vec2_Contains(context->local_position, FL_Vec2_Zero(),
                        widget->size)) {
    return false;
  }

  if (!FL_Widget_HitTestChildren(widget, context)) {
    return false;
  }

  FL_HitTest_AddWidget(context, widget);
  return true;
}

bool FL_Widget_HitTest_Transluscent(FL_Widget *widget,
                                    FL_HitTestContext *context) {
  if (!FL_Vec2_Contains(context->local_position, FL_Vec2_Zero(),
                        widget->size)) {
    return false;
  }

  bool hit_children = FL_Widget_HitTestChildren(widget, context);

  FL_HitTest_AddWidget(context, widget);

  return hit_children;
}

bool FL_Widget_HitTest_Opaque(FL_Widget *widget, FL_HitTestContext *context) {
  if (!FL_Vec2_Contains(context->local_position, FL_Vec2_Zero(),
                        widget->size)) {
    return false;
  }

  FL_Widget_HitTestChildren(widget, context);

  FL_HitTest_AddWidget(context, widget);

  return true;
}

bool FL_Widget_HitTest_ByBehaviour(FL_Widget *widget,
                                   FL_HitTestContext *context,
                                   FL_HitTestBehaviour behaviour) {
  switch (behaviour) {
    case FL_HitTestBehaviour_DeferToChild: {
      return FL_Widget_HitTest_DeferToChild(widget, context);
    } break;

    case FL_HitTestBehaviour_Translucent: {
      return FL_Widget_HitTest_Transluscent(widget, context);
    } break;

    case FL_HitTestBehaviour_Opaque: {
      return FL_Widget_HitTest_Opaque(widget, context);
    } break;

    default:
      FL_UNREACHABLE;
  }
}

bool FL_Widget_HitTest(FL_Widget *widget, FL_HitTestContext *context) {
  if (widget->klass->hit_test) {
    return widget->klass->hit_test(widget, context);
  } else {
    return FL_Widget_HitTest_Opaque(widget, context);
  }
}

void FL_Widget_Paint_Default(FL_Widget *widget, FL_PaintingContext *context,
                             FL_Vec2 offset) {
  for (FL_Widget *child = widget->first; child; child = child->next) {
    FL_Widget_Paint(child, context, FL_Vec2_Add(offset, child->offset));
  }
}

void FL_Widget_SendNotification(FL_Widget *widget, FL_NotificationID id,
                                void *data) {
  for (FL_Widget *parent = widget->parent; parent; parent = parent->parent) {
    if (parent->klass->on_notification &&
        parent->klass->on_notification(parent, id, data)) {
      break;
    }
  }
}

void FL_WidgetList_Append(FL_WidgetList *list, FL_Widget *widget) {
  Build *build = widget->build;
  FL_WidgetListEntry *entry =
      FL_Arena_PushStruct(build->arena, FL_WidgetListEntry);
  *entry = (FL_WidgetListEntry){
      .widget = widget,
  };
  FL_DLIST_ADD_LAST(list->first, list->last, entry, prev, next);
}

FL_WidgetList FL_WidgetList_Make(FL_Widget *widgets[]) {
  FL_WidgetList list = {0};
  for (int i = 0;; ++i) {
    FL_Widget *widget = widgets[i];
    if (!widget) {
      break;
    }
    FL_WidgetList_Append(&list, widget);
  }
  return list;
}

FL_f32 FL_Widget_GetDeltaTime(FL_Widget *widget) {
  Build *build = widget->build;
  return build->delta_time;
}

FL_f32 FL_Widget_AnimateFast(FL_Widget *widget, FL_f32 value, FL_f32 target) {
  Build *build = widget->build;
  FL_f32 result;
  FL_f32 diff = (target - value);
  if (FL_Abs(diff) < FL_PRECISION_ERROR_TOLERANCE) {
    result = target;
  } else {
    result = value + diff * build->fast_animation_rate;
  }
  return result;
}

FL_Arena *FL_Widget_GetArena(FL_Widget *widget) {
  Build *build = widget->build;
  return build->arena;
}

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>


static void FL_CommandBuffer_Deinit(FL_CommandBuffer *command_buffer) {
  FL_TextureCommandArray_Deinit(&command_buffer->texture_commands,
                                command_buffer->allocator);
}

static void Build_Init(Build *build, State *state, FL_isize index,
                       FL_Allocator allocator) {
  (*build) = (Build){
      .state = state,
      .arena = FL_Arena_Create(&(FL_ArenaOptions){
          .allocator = allocator,
      }),
      .index = index,
      .command_buffer = {.allocator = allocator},
  };
}

static void Build_Deinit(Build *build) {
  FL_CommandBuffer_Deinit(&build->command_buffer);
  FL_Arena_Destroy(build->arena);
}

void FL_HitTest_Init(FL_HitTestContext *context, FL_Arena *arena) {
  *context = (FL_HitTestContext){
      .arena = arena,
  };
}

void FL_HitTest_AddWidget(FL_HitTestContext *context, FL_Widget *widget) {
  FL_HitTestEntry *entry = FL_Arena_PushStruct(context->arena, FL_HitTestEntry);
  entry->widget = widget;
  entry->transform = context->transform;
  entry->local_position = context->local_position;
  FL_DLIST_ADD_LAST(context->first, context->last, entry, prev, next);
}

static bool FL_HitTest_HasWidget(FL_HitTestContext *context,
                                 FL_Widget *widget) {
  // TODO: Use hash map to speed up lookup.
  for (FL_HitTestEntry *entry = context->first; entry; entry = entry->next) {
    if (entry->widget == widget) {
      return true;
    }
  }
  return false;
}

/**
 * Update widget references to their links.
 */
static void FL_HitTest_Sync(FL_HitTestContext *context) {
  FL_HitTestEntry *entry = context->last;
  for (; entry && entry->widget; entry = entry->prev) {
    entry->widget = entry->widget->link;
    if (!entry->widget) {
      break;
    }
  }

  if (entry) {
    FL_ASSERT(!entry->widget);
    if (entry->next) {
      context->first = entry->next;
      context->first->prev = 0;
    } else {
      context->first = context->last = 0;
    }
  }
}

static void FL_HitTest_Reset(FL_HitTestContext *context) {
  FL_Arena_Reset(context->arena);
  context->first = context->last = 0;
}

static State *global_state;

static void SetGlobalState(State *state) { global_state = state; }

State *GetGlobalState(void) {
  FL_ASSERTF(global_state, "Did you call FL_Init?");
  return global_state;
}

static void InitWidgets(void) {
  FL_Basic_Init();
  FL_Flex_Init();
  FL_Stack_Init();
  FL_Viewport_Init();
}

void FL_Init(const FL_InitOptions *opts) {
  FL_Allocator allocator = opts->allocator;
  if (!allocator.alloc) {
    allocator = FL_Allocator_GetDefault();
  }

  FL_Arena *arena = FL_Arena_Create(&(FL_ArenaOptions){
      .allocator = allocator,
  });

  State *state = FL_Arena_PushStruct(arena, State);
  *state = (State){
      .arena = arena,

      .next_context_id = 1,
      .next_notification_id = 1,

      .pixels_per_point = 1.0f,
      .points_per_pixel = 1.0f,

      .next_down_pointer = 1,
  };

  for (int i = 0; i < FL_COUNT_OF(state->builds); i++) {
    Build_Init(state->builds + i, state, i, allocator);
  }
  state->curr_build = state->builds;
  state->last_build = state->builds + 1;

  FL_HitTest_Init(&state->button_down_hit_test_context,
                  FL_Arena_Create(&(FL_ArenaOptions){.allocator = allocator}));
  for (int i = 0; i < FL_COUNT_OF(state->button_move_hit_test_contexts); i++) {
    FL_HitTest_Init(
        state->button_move_hit_test_contexts + i,
        FL_Arena_Create(&(FL_ArenaOptions){.allocator = allocator}));
  }

  SetGlobalState(state);
  state->font_atlas = FL_FontAtlas_Create(1024, 1024, allocator);

  InitWidgets();
}

void FL_Deinit(void) {
  // Unmount and cleanup widgets from last build.
  FL_Run(&(FL_RunOptions){0});

  State *state = GetGlobalState();
  SetGlobalState(0);

  for (int i = 0; i < FL_COUNT_OF(global_state->builds); i++) {
    Build_Deinit(state->builds + i);
  }

  FL_Arena_Destroy(state->button_down_hit_test_context.arena);
  for (int i = 0; i < FL_COUNT_OF(state->button_move_hit_test_contexts); i++) {
    FL_Arena_Destroy(state->button_move_hit_test_contexts[i].arena);
  }

  FL_FontAtlas_Destroy(state->font_atlas);

  FL_Arena_Destroy(state->arena);
}

void FL_SetPixelsPerPoint(FL_f32 pixels_per_point) {
  State *state = GetGlobalState();
  state->pixels_per_point = pixels_per_point;
  if (state->pixels_per_point == 0.0f) {
    state->pixels_per_point = 1.0f;
  }
  state->points_per_pixel = 1.0f / state->pixels_per_point;
}

FL_ContextID FL_Context_Register(void) {
  State *state = GetGlobalState();
  return state->next_context_id++;
}

FL_NotificationID FL_Notification_Register(void) {
  State *state = GetGlobalState();
  return state->next_notification_id++;
}

static void HitTest(FL_Widget *widget, FL_HitTestContext *context,
                    FL_Vec2 pos) {
  if (!widget) {
    return;
  }

  context->position = pos;
  context->transform = FL_Trans2_Identity();
  context->local_position = pos;
  FL_Widget_HitTest(widget, context);
}

static void FL_CheckEnterExit(State *state, Build *build, FL_Vec2 pos) {
  // Handle ENTER/EXIT event
  FL_HitTestContext *last_context = state->button_move_hit_test_contexts +
                                    state->button_move_hit_test_context_index;
  state->button_move_hit_test_context_index =
      (state->button_move_hit_test_context_index + 1) %
      FL_COUNT_OF(state->button_move_hit_test_contexts);
  FL_HitTestContext *context = state->button_move_hit_test_contexts +
                               state->button_move_hit_test_context_index;
  HitTest(build->root, context, pos);

  for (FL_HitTestEntry *entry = last_context->first; entry;
       entry = entry->next) {
    if (!FL_HitTest_HasWidget(context, entry->widget)) {
      FL_Widget_OnPointerEvent(
          entry->widget,
          (FL_PointerEvent){
              .type = FL_PointerEventType_Exit,
              .pointer = state->down_pointer,
              .button = state->down_button,
              .position = pos,
              .transform = entry->transform,
              .local_position = FL_Trans2_DotVec2(entry->transform, pos),
          });
    }
  }
  FL_PointerEventResolver_Reset(&state->resolver);

  for (FL_HitTestEntry *entry = context->first; entry; entry = entry->next) {
    if (!FL_HitTest_HasWidget(last_context, entry->widget)) {
      FL_Widget_OnPointerEvent(entry->widget,
                               (FL_PointerEvent){
                                   .type = FL_PointerEventType_Enter,
                                   .pointer = state->down_pointer,
                                   .button = state->down_button,
                                   .position = pos,
                                   .transform = entry->transform,
                                   .local_position = entry->local_position,
                               });
    }
  }
  FL_PointerEventResolver_Reset(&state->resolver);

  FL_HitTest_Reset(last_context);
}

void FL_OnMouseMove(FL_Vec2 pos) {
  State *state = GetGlobalState();
  Build *build = state->last_build;

  FL_CheckEnterExit(state, build, pos);

  if (state->down_pointer) {
    for (FL_HitTestEntry *entry = state->button_down_hit_test_context.first;
         entry; entry = entry->next) {
      FL_Widget_OnPointerEvent(
          entry->widget,
          (FL_PointerEvent){
              .type = FL_PointerEventType_Move,
              .pointer = state->down_pointer,
              .button = state->down_button,
              .position = pos,
              .transform = entry->transform,
              .local_position = FL_Trans2_DotVec2(entry->transform, pos),
              .delta = FL_Vec2_Sub(pos, state->mouse_pos),
          });
    }
    FL_PointerEventResolver_Reset(&state->resolver);
  } else {
    // Only send HOVER events if there isn't button down.

    // TODO: Reuse hit test result from above.
    HitTest(build->root, &state->button_down_hit_test_context, pos);
    for (FL_HitTestEntry *entry = state->button_down_hit_test_context.first;
         entry; entry = entry->next) {
      FL_Widget_OnPointerEvent(entry->widget,
                               (FL_PointerEvent){
                                   .type = FL_PointerEventType_Hover,
                                   .button = state->down_button,
                                   .position = pos,
                                   .transform = entry->transform,
                                   .local_position = entry->local_position,
                               });
    }
    FL_PointerEventResolver_Reset(&state->resolver);
    FL_HitTest_Reset(&state->button_down_hit_test_context);
  }

  state->mouse_pos = pos;
}

void FL_OnMouseButtonDown(FL_Vec2 pos, FL_u32 button) {
  State *state = GetGlobalState();
  Build *build = state->last_build;

  state->down_button |= button;

  // TODO: Multiple touch?
  if (state->down_pointer) {
    // If there is already pointer down, treat this as MOVE event.
    FL_OnMouseMove(pos);
  } else {
    state->down_pointer = state->next_down_pointer++;

    FL_ASSERT(state->down_button == button);

    FL_GestureArena *gesture_arena = FL_GestureArena_Open(
        &state->gesture_arena_state, state->down_pointer, state->arena);

    HitTest(build->root, &state->button_down_hit_test_context, pos);
    for (FL_HitTestEntry *entry = state->button_down_hit_test_context.first;
         entry; entry = entry->next) {
      FL_Widget_OnPointerEvent(entry->widget,
                               (FL_PointerEvent){
                                   .type = FL_PointerEventType_Down,
                                   .pointer = state->down_pointer,
                                   .button = state->down_button,
                                   .position = pos,
                                   .transform = entry->transform,
                                   .local_position = entry->local_position,
                               });
    }
    FL_PointerEventResolver_Reset(&state->resolver);

    FL_GestureArena_Close(gesture_arena);

    state->mouse_pos = pos;
  }
}

void FL_OnMouseButtonUp(FL_Vec2 pos, FL_u32 button) {
  State *state = GetGlobalState();

  state->down_button &= (~button);

  // Only send UP event when no button is down.
  if (!state->down_button) {
    FL_ASSERT(state->down_pointer);

    for (FL_HitTestEntry *entry = state->button_down_hit_test_context.first;
         entry; entry = entry->next) {
      FL_Widget_OnPointerEvent(
          entry->widget,
          (FL_PointerEvent){
              .type = FL_PointerEventType_Up,
              .pointer = state->down_pointer,
              .button = button,
              .position = pos,
              .transform = entry->transform,
              .local_position = FL_Trans2_DotVec2(entry->transform, pos),
          });
    }
    FL_PointerEventResolver_Reset(&state->resolver);

    FL_HitTest_Reset(&state->button_down_hit_test_context);

    FL_GestureArena *gesture_arena =
        FL_GestureArena_Get(&state->gesture_arena_state, state->down_pointer);
    if (gesture_arena) {
      FL_GestureArena_Sweep(gesture_arena);
    }

    state->down_pointer = 0;
    state->mouse_pos = pos;
  } else {
    FL_OnMouseMove(pos);
  }
}

void FL_OnMouseScroll(FL_Vec2 pos, FL_Vec2 delta) {
  State *state = GetGlobalState();
  Build *build = state->last_build;

  FL_Arena scratch = *build->arena;
  FL_HitTestContext context;
  FL_HitTest_Init(&context, &scratch);
  HitTest(build->root, &context, pos);
  for (FL_HitTestEntry *entry = context.first; entry; entry = entry->next) {
    FL_Widget_OnPointerEvent(entry->widget,
                             (FL_PointerEvent){
                                 .type = FL_PointerEventType_Scroll,
                                 .position = pos,
                                 .transform = entry->transform,
                                 .local_position = entry->local_position,
                                 .delta = delta,
                             });
  }
  FL_PointerEventResolver_Reset(&state->resolver);

  state->mouse_pos = pos;
}

static void ResetLink(FL_Widget *widget) {
  for (FL_Widget *child = widget->first; child; child = child->next) {
    ResetLink(child);
  }
  widget->link = 0;
}

static void PrepareNextBuild(State *state) {
  state->build_index++;
  state->curr_build =
      state->builds + (state->build_index % FL_COUNT_OF(state->builds));
  state->last_build =
      state->builds + ((state->build_index - 1) % FL_COUNT_OF(state->builds));

  Build *build = state->curr_build;
  FL_Arena_Reset(build->arena);
  build->index = state->build_index;
  build->root = 0;
}

FL_DrawList FL_Run(const FL_RunOptions *opts) {
  State *state = GetGlobalState();
  Build *build = state->curr_build;
  build->delta_time = opts->delta_time;
  build->fast_animation_rate = 1.0f - FL_Exp(-50.f * opts->delta_time);

  if (state->last_build->root) {
    ResetLink(state->last_build->root);
  }

  build->root = opts->widget;
  if (build->root) {
    FL_Widget_Mount(0, build->root);

    FL_Widget_Layout(
        build->root,
        FL_BoxConstraints_Tight(opts->viewport.right - opts->viewport.left,
                                opts->viewport.bottom - opts->viewport.top));
  }

  FL_HitTest_Sync(&state->button_down_hit_test_context);
  for (int i = 0; i < FL_COUNT_OF(state->button_move_hit_test_contexts); i++) {
    FL_HitTest_Sync(state->button_move_hit_test_contexts + i);
  }

  FL_CheckEnterExit(state, build, state->mouse_pos);

  if (state->last_build->root) {
    FL_Widget_Unmount(state->last_build->root);
  }

  FL_CommandBuffer *command_buffer = &build->command_buffer;
  if (build->root) {
    FL_PaintingContext context = {
        .arena = build->arena,
        .command_buffer = command_buffer,
        .viewport = opts->viewport,
    };
    FL_Widget_Paint(build->root, &context,
                    (FL_Vec2){opts->viewport.left, opts->viewport.top});

    if (command_buffer->draw_commands.len > 0) {
      FL_DrawCommand *last_draw_command = command_buffer->draw_commands.ptr +
                                          command_buffer->draw_commands.len - 1;
      if (!last_draw_command->texture) {
        FL_FontAtlas *atlas = state->font_atlas;
        FL_FontAtlas_EnsureTextureCreated(atlas, &context);
        last_draw_command->texture = &atlas->texture;
      }
    }
  }

  FL_DrawList result = {
      .texture_commands = command_buffer->texture_commands.ptr,
      .texture_command_count = command_buffer->texture_commands.len,

      .vertex_buffer = command_buffer->vertex_buffer.ptr,
      .vertex_count = command_buffer->vertex_buffer.len,

      .index_buffer = command_buffer->index_buffer.ptr,
      .index_count = command_buffer->index_buffer.len,

      .draw_commands = command_buffer->draw_commands.ptr,
      .draw_command_count = command_buffer->draw_commands.len,
  };
  FL_TextureCommandArray_Clear(&command_buffer->texture_commands);
  FL_DrawVertexArray_Clear(&command_buffer->vertex_buffer);
  FL_DrawIndexArray_Clear(&command_buffer->index_buffer);
  FL_DrawCommandArray_Clear(&command_buffer->draw_commands);

  PrepareNextBuild(state);

  return result;
}

FL_Str FL_Format(const char *format, ...) {
  State *state = GetGlobalState();
  Build *build = state->curr_build;
  FL_Arena *arena = build->arena;

  va_list ap;
  va_start(ap, format);
  FL_Str result = FL_Str_FormatV(arena, format, ap);
  va_end(ap);

  return result;
}

#include <stdint.h>


static void FL_ColoredBox_Mount(FL_Widget *widget) {
  FL_ColoredBoxProps *props = FL_Widget_GetProps(widget, FL_ColoredBoxProps);
  if (props->child) {
    FL_Widget_Mount(widget, props->child);
  }
}

static void FL_ColoredBox_Paint(FL_Widget *widget, FL_PaintingContext *context,
                                FL_Vec2 offset) {
  FL_ColoredBoxProps *props = FL_Widget_GetProps(widget, FL_ColoredBoxProps);
  FL_Vec2 size = widget->size;
  if (size.x > 0 && size.y > 0) {
    FL_Draw_AddRect(context, FL_Rect_FromMinSize(offset, size), props->color);
  }

  FL_Widget_Paint_Default(widget, context, offset);
}

static FL_WidgetClass FL_ColoredBoxClass = {
    .name = "ColoredBox",
    .props_size = FL_SIZE_OF(FL_ColoredBoxProps),
    .mount = FL_ColoredBox_Mount,
    .paint = FL_ColoredBox_Paint,
    .hit_test = FL_Widget_HitTest_Opaque,
};

FL_Widget *FL_ColoredBox(const FL_ColoredBoxProps *props) {
  return FL_Widget_Create(&FL_ColoredBoxClass, props->key, props);
}

static void FL_ConstrainedBox_Layout(FL_Widget *widget,
                                     FL_BoxConstraints constraints) {
  FL_ConstrainedBoxProps *props =
      FL_Widget_GetProps(widget, FL_ConstrainedBoxProps);
  FL_BoxConstraints enforced_constraints =
      FL_BoxConstraints_Enforce(props->constraints, constraints);
  FL_Widget *child = props->child;
  if (child) {
    FL_Widget_Mount(widget, child);
    FL_Widget_Layout(child, enforced_constraints);
    widget->size = child->size;
  } else {
    widget->size =
        FL_BoxConstraints_Constrain(enforced_constraints, FL_Vec2_Zero());
  }
}

static FL_WidgetClass FL_ConstrainedBoxClass = {
    .name = "ConstrainedBox",
    .props_size = FL_SIZE_OF(FL_ConstrainedBoxProps),
    .layout = FL_ConstrainedBox_Layout,
};

FL_Widget *FL_ConstrainedBox(const FL_ConstrainedBoxProps *props) {
  return FL_Widget_Create(&FL_ConstrainedBoxClass, props->key, props);
}

static FL_BoxConstraints limit_constraints(FL_BoxConstraints constraints,
                                           FL_f32 max_width,
                                           FL_f32 max_height) {
  return (FL_BoxConstraints){
      constraints.min_width,
      FL_BoxConstraints_HasBoundedWidth(constraints)
          ? constraints.max_width
          : FL_BoxConstraints_ConstrainWidth(constraints, max_width),
      constraints.min_height,
      FL_BoxConstraints_HasBoundedHeight(constraints)
          ? constraints.max_height
          : FL_BoxConstraints_ConstrainHeight(constraints, max_height),
  };
}

static void FL_LimitedBox_Layout(FL_Widget *widget,
                                 FL_BoxConstraints constraints) {
  FL_LimitedBoxProps *props = FL_Widget_GetProps(widget, FL_LimitedBoxProps);
  FL_BoxConstraints limited_constraints =
      limit_constraints(constraints, props->max_width, props->max_height);

  FL_Widget *child = props->child;
  if (child) {
    FL_Widget_Mount(widget, child);
    FL_Widget_Layout(child, limited_constraints);
    FL_Vec2 child_size = child->size;
    widget->size = FL_BoxConstraints_Constrain(constraints, child_size);
  } else {
    widget->size =
        FL_BoxConstraints_Constrain(limited_constraints, FL_Vec2_Zero());
  }
}

static FL_WidgetClass FL_LimitedBoxClass = {
    .name = "LimitedBox",
    .props_size = FL_SIZE_OF(FL_LimitedBoxProps),
    .layout = FL_LimitedBox_Layout,
};

FL_Widget *FL_LimitedBox(const FL_LimitedBoxProps *props) {
  return FL_Widget_Create(&FL_LimitedBoxClass, props->key, props);
}

static void AlignChildren(FL_Widget *widget, FL_Alignment alignment) {
  // TODO: text direction
  for (FL_Widget *child = widget->first; child; child = child->next) {
    child->offset = FL_Alignment_AlignOffset(
        alignment, FL_Vec2_Sub(widget->size, child->size));
  }
}

static void FL_Align_Layout(FL_Widget *widget, FL_BoxConstraints constraints) {
  FL_AlignProps *props = FL_Widget_GetProps(widget, FL_AlignProps);
  FL_f32o width = props->width;
  FL_f32o height = props->height;

  if (props->width.present) {
    FL_ASSERTF(width.value >= 0, "width must be positive, got %f",
               (FL_f64)width.value);
  }
  if (height.present) {
    FL_ASSERTF(height.value >= 0, "height must be positive, got %f",
               (FL_f64)height.value);
  }
  bool should_shrink_wrap_width =
      width.present || FL_IsInfinite(constraints.max_width);
  bool should_shrink_wrap_height =
      height.present || FL_IsInfinite(constraints.max_height);

  FL_Widget *child = props->child;
  if (child) {
    FL_BoxConstraints child_constraints = FL_BoxConstraints_Loosen(constraints);

    FL_Widget_Mount(widget, child);
    FL_Widget_Layout(child, child_constraints);
    FL_Vec2 child_size = child->size;

    FL_Vec2 wrap_size = (FL_Vec2){
        should_shrink_wrap_width
            ? (child_size.x * (width.present ? width.value : 1.0f))
            : FL_INFINITY,
        should_shrink_wrap_height
            ? (child_size.y * (height.present ? height.value : 1.0f))
            : FL_INFINITY,
    };

    widget->size = FL_BoxConstraints_Constrain(constraints, wrap_size);

    AlignChildren(widget, props->alignment);
  } else {
    FL_Vec2 size = (FL_Vec2){
        should_shrink_wrap_width ? 0 : FL_INFINITY,
        should_shrink_wrap_height ? 0 : FL_INFINITY,
    };
    widget->size = FL_BoxConstraints_Constrain(constraints, size);
  }
}

static FL_WidgetClass FL_AlignClass = {
    .name = "Align",
    .props_size = FL_SIZE_OF(FL_AlignProps),
    .layout = FL_Align_Layout,
};

FL_Widget *FL_Align(const FL_AlignProps *props) {
  return FL_Widget_Create(&FL_AlignClass, props->key, props);
}

FL_WidgetClass FL_CenterClass = {
    .name = "Center",
    .props_size = FL_SIZE_OF(FL_AlignProps),
    .layout = FL_Align_Layout,
};

FL_Widget *FL_Center(const FL_CenterProps *props) {
  FL_AlignProps align_props = {
      .key = props->key,
      .alignment = FL_Alignment_Center(),
      .width = props->width,
      .height = props->height,
      .child = props->child,
  };
  return FL_Widget_Create(&FL_CenterClass, align_props.key, &align_props);
}

typedef struct FLResolvedEdgeInsets {
  FL_f32 left;
  FL_f32 right;
  FL_f32 top;
  FL_f32 bottom;
} FLResolvedEdgeInsets;

static void FL_Padding_Layout(FL_Widget *widget,
                              FL_BoxConstraints constraints) {
  FL_PaddingProps *props = FL_Widget_GetProps(widget, FL_PaddingProps);
  // TODO: text direction
  FLResolvedEdgeInsets resolved_padding = {
      .left = props->padding.start,
      .right = props->padding.end,
      .top = props->padding.top,
      .bottom = props->padding.bottom,
  };
  FL_f32 horizontal = resolved_padding.left + resolved_padding.right;
  FL_f32 vertical = resolved_padding.top + resolved_padding.bottom;

  FL_Widget *child = props->child;
  if (child) {
    FL_BoxConstraints inner_constraints =
        FL_BoxConstraints_Deflate(constraints, horizontal, vertical);

    FL_Widget_Mount(widget, child);
    FL_Widget_Layout(child, inner_constraints);
    FL_Vec2 child_size = child->size;
    child->offset = (FL_Vec2){resolved_padding.left, resolved_padding.top};

    widget->size = FL_BoxConstraints_Constrain(
        constraints,
        (FL_Vec2){horizontal + child_size.x, vertical + child_size.y});
  } else {
    widget->size = FL_BoxConstraints_Constrain(constraints,
                                               (FL_Vec2){horizontal, vertical});
  }
}

FL_WidgetClass FL_PaddingClass = {
    .name = "Padding",
    .props_size = FL_SIZE_OF(FL_PaddingProps),
    .layout = FL_Padding_Layout,
};

FL_Widget *FL_Padding(const FL_PaddingProps *props) {
  return FL_Widget_Create(&FL_PaddingClass, props->key, props);
}

FL_Widget *FL_Container(const FL_ContainerProps *props) {
  FL_BoxConstraintsO constraints = props->constraints;

  if (props->width.present || props->height.present) {
    if (constraints.present) {
      constraints = FL_BoxConstraints_Some(FL_BoxConstraints_Tighten(
          constraints.value, props->width, props->height));
    } else {
      constraints = FL_BoxConstraints_Some(
          FL_BoxConstraints_TightFor(props->width, props->height));
    }
  }

  FL_Widget *widget = props->child;
  if (!widget &&
      (!constraints.present || !FL_BoxConstraints_IsTight(constraints.value))) {
    widget = FL_LimitedBox(&(FL_LimitedBoxProps){
        .child = FL_ConstrainedBox(&(FL_ConstrainedBoxProps){
            .constraints =
                (FL_BoxConstraints){
                    FL_INFINITY,
                    FL_INFINITY,
                    FL_INFINITY,
                    FL_INFINITY,
                },
        }),
    });
  } else if (props->alignment.present) {
    widget = FL_Align(&(FL_AlignProps){
        .alignment = props->alignment.value,
        .child = widget,
    });
  }

  if (props->padding.present) {
    widget = FL_Padding(&(FL_PaddingProps){
        .padding = props->padding.value,
        .child = widget,
    });
  }

  if (props->color.present) {
    widget = FL_ColoredBox(&(FL_ColoredBoxProps){
        .color = props->color.value,
        .child = widget,
    });
  }

  if (constraints.present) {
    widget = FL_ConstrainedBox(&(FL_ConstrainedBoxProps){
        .constraints = constraints.value,
        .child = widget,
    });
  }

  if (props->margin.present) {
    widget = FL_Padding(&(FL_PaddingProps){
        .padding = props->margin.value,
        .child = widget,
    });
  }

  FL_ASSERT(widget);

  return widget;
}

static void FL_UnconstrainedBox_Layout(FL_Widget *widget,
                                       FL_BoxConstraints constraints) {
  FL_UnconstrainedBoxProps *props =
      FL_Widget_GetProps(widget, FL_UnconstrainedBoxProps);

  FL_Widget *child = props->child;
  if (child) {
    FL_BoxConstraints child_constraints = (FL_BoxConstraints){
        0,
        FL_INFINITY,
        0,
        FL_INFINITY,
    };

    FL_Widget_Mount(widget, child);
    FL_Widget_Layout(widget->first, child_constraints);
    FL_Vec2 child_size = child->size;
    widget->size = FL_BoxConstraints_Constrain(constraints, child_size);
  } else {
    widget->size = FL_BoxConstraints_Constrain(constraints, FL_Vec2_Zero());
  }

  AlignChildren(widget, props->alignment);
}

static FL_WidgetClass FL_UnconstrainedBoxClass = {
    .name = "UnconstrainedBox",
    .props_size = FL_SIZE_OF(FL_UnconstrainedBoxProps),
    .layout = FL_UnconstrainedBox_Layout,
};

FL_Widget *FL_UnconstrainedBox(const FL_UnconstrainedBoxProps *props) {
  return FL_Widget_Create(&FL_UnconstrainedBoxClass, props->key, props);
}

void FL_Basic_Init(void) {}

#include <stdbool.h>
#include <stdint.h>


static FL_ContextID FL_FlexContext_ID;

typedef struct FL_FlexContext {
  FL_i32 flex;
  FL_FlexFit fit;
} FL_FlexContext;

typedef struct AxisSize {
  FL_f32 main;
  FL_f32 cross;
} AxisSize;

static inline AxisSize AxisSize_FromVec2(FL_Vec2 size, FL_Axis direction) {
  switch (direction) {
    case FL_Axis_Horizontal: {
      return (AxisSize){size.x, size.y};
    } break;

    case FL_Axis_Vertical: {
      return (AxisSize){size.y, size.x};
    } break;

    default:
      FL_UNREACHABLE;
  }
}

static inline FL_Vec2 AxisSize_ToVec2(AxisSize size, FL_Axis direction) {
  switch (direction) {
    case FL_Axis_Horizontal: {
      return (FL_Vec2){size.main, size.cross};
    } break;

    case FL_Axis_Vertical: {
      return (FL_Vec2){size.cross, size.main};
    } break;

    default:
      FL_UNREACHABLE;
  }
}

static inline FL_BoxConstraints GetConstrainsForNonFlexChild(
    FL_Axis direction, FL_CrossAxisAlignment cross_axis_alignment,
    FL_BoxConstraints constraints) {
  bool should_fill_cross_axis = false;
  if (cross_axis_alignment == FL_CrossAxisAlignment_Stretch) {
    should_fill_cross_axis = true;
  }

  FL_BoxConstraints result;
  switch (direction) {
    case FL_Axis_Horizontal: {
      if (should_fill_cross_axis) {
        result = FL_BoxConstraints_TightHeight(constraints.max_height);
      } else {
        result = (FL_BoxConstraints){
            .min_width = 0,
            .max_width = FL_INFINITY,
            .min_height = 0,
            .max_height = constraints.max_height,
        };
      }
    } break;
    case FL_Axis_Vertical: {
      if (should_fill_cross_axis) {
        result = FL_BoxConstraints_TightWidth(constraints.max_width);
      } else {
        result = (FL_BoxConstraints){
            .min_width = 0,
            .max_width = constraints.max_width,
            .min_height = 0,
            .max_height = FL_INFINITY,
        };
      }
    } break;
    default:
      FL_UNREACHABLE;
  }
  return result;
}

static inline FL_BoxConstraints GetConstrainsForFlexChild(
    FL_Axis direction, FL_CrossAxisAlignment cross_axis_alignment,
    FL_BoxConstraints constraints, FL_f32 max_child_extent,
    FL_FlexContext *flex) {
  FL_DEBUG_ASSERT(flex->flex > 0);
  FL_DEBUG_ASSERT(max_child_extent >= 0.0f);
  FL_f32 min_child_extent = 0.0;
  if (flex->fit == FL_FlexFit_Tight) {
    min_child_extent = max_child_extent;
  }
  bool should_fill_cross_axis = false;
  if (cross_axis_alignment == FL_CrossAxisAlignment_Stretch) {
    should_fill_cross_axis = true;
  }
  FL_BoxConstraints result;
  if (direction == FL_Axis_Horizontal) {
    result = (FL_BoxConstraints){
        .min_width = min_child_extent,
        .max_width = max_child_extent,
        .min_height = should_fill_cross_axis ? constraints.max_height : 0,
        .max_height = constraints.max_height,
    };
  } else {
    result = (FL_BoxConstraints){
        .min_width = should_fill_cross_axis ? constraints.max_width : 0,
        .max_width = constraints.max_width,
        .min_height = min_child_extent,
        .max_height = max_child_extent,
    };
  }
  return result;
}

static AxisSize AxisSize_Constrains(AxisSize size,
                                    FL_BoxConstraints constraints,
                                    FL_Axis direction) {
  FL_BoxConstraints effective_constraints = constraints;
  if (direction != FL_Axis_Horizontal) {
    effective_constraints = FL_BoxConstraints_Flip(constraints);
  }
  FL_Vec2 constrained_size = FL_BoxConstraints_Constrain(
      effective_constraints, (FL_Vec2){size.main, size.cross});
  return (AxisSize){constrained_size.x, constrained_size.y};
}

typedef struct LayoutSize {
  AxisSize size;
  FL_f32 main_axis_free_space;
  bool can_flex;
  FL_f32 space_per_flex;
} LayoutSize;

static LayoutSize FL_Flex_ComputeSize(
    FL_Widget *widget, FL_Axis direction, FL_MainAxisSize main_axis_size,
    FL_CrossAxisAlignment cross_axis_alignment, FL_f32 spacing,
    FL_BoxConstraints constraints) {
  // Determine used flex factor, size inflexible items, calculate free space.
  FL_f32 max_main_size;
  switch (direction) {
    case FL_Axis_Horizontal: {
      max_main_size =
          FL_BoxConstraints_ConstrainWidth(constraints, FL_INFINITY);
    } break;

    case FL_Axis_Vertical: {
      max_main_size =
          FL_BoxConstraints_ConstrainHeight(constraints, FL_INFINITY);
    } break;

    default:
      FL_UNREACHABLE;
  }

  bool can_flex = FL_IsFinite(max_main_size);
  FL_BoxConstraints non_flex_child_constraints = GetConstrainsForNonFlexChild(
      direction, cross_axis_alignment, constraints);
  // TODO: Baseline aligned

  // The first pass lays out non-flex children and computes total flex.
  FL_i32 total_flex = 0;
  FL_Widget *first_flex_child = 0;
  // Initially, accumulated_size is the sum of the spaces between children in
  // the main axis.
  AxisSize accumulated_size = {spacing * (FL_f32)(widget->child_count - 1),
                               0.0f};
  for (FL_Widget *child = widget->first; child; child = child->next) {
    FL_i32 child_flex = 0;
    if (can_flex) {
      FL_FlexContext *data =
          FL_Widget_GetContext(child, FL_FlexContext_ID, FL_FlexContext);
      if (data) {
        child_flex = data->flex;
      }
    }

    if (child_flex > 0) {
      total_flex += child_flex;
      if (!first_flex_child) {
        first_flex_child = child;
      }
    } else {
      FL_Widget_Layout(child, non_flex_child_constraints);
      AxisSize child_size = AxisSize_FromVec2(child->size, direction);

      accumulated_size.main += child_size.main;
      accumulated_size.cross = FL_Max(accumulated_size.cross, child_size.cross);
    }
  }

  FL_DEBUG_ASSERT((total_flex == 0) == (first_flex_child == 0));
  FL_DEBUG_ASSERT(first_flex_child == 0 || can_flex);

  // The second pass distributes free space to flexible children.
  FL_f32 flex_space = FL_Max(0.0f, max_main_size - accumulated_size.main);
  FL_f32 space_per_flex = flex_space / (FL_f32)total_flex;
  for (FL_Widget *child = widget->first; child && total_flex > 0;
       child = child->next) {
    FL_FlexContext *data =
        FL_Widget_GetContext(child, FL_FlexContext_ID, FL_FlexContext);
    if (!data || data->flex <= 0) {
      continue;
    }
    total_flex -= data->flex;
    FL_DEBUG_ASSERT(FL_IsFinite(space_per_flex));
    FL_f32 max_child_extent = space_per_flex * (FL_f32)data->flex;
    FL_DEBUG_ASSERT(data->fit == FL_FlexFit_Loose ||
                    max_child_extent < FL_INFINITY);
    FL_BoxConstraints child_constraints = GetConstrainsForFlexChild(
        direction, cross_axis_alignment, constraints, max_child_extent, data);
    FL_Widget_Layout(child, child_constraints);
    AxisSize child_size = AxisSize_FromVec2(child->size, direction);

    accumulated_size.main += child_size.main;
    accumulated_size.cross = FL_Max(accumulated_size.cross, child_size.cross);
  }
  FL_DEBUG_ASSERT(total_flex == 0);

  FL_f32 ideal_main_size;
  if (main_axis_size == FL_MainAxisSize_Max && FL_IsFinite(max_main_size)) {
    ideal_main_size = max_main_size;
  } else {
    ideal_main_size = accumulated_size.main;
  }

  AxisSize size = {ideal_main_size, accumulated_size.cross};
  AxisSize constrained_size = AxisSize_Constrains(size, constraints, direction);

  return (LayoutSize){
      .size = constrained_size,
      .main_axis_free_space = constrained_size.main - accumulated_size.main,
      .can_flex = can_flex,
      .space_per_flex = can_flex ? space_per_flex : 0,
  };
}

static void FL_Flex_DistributeSpace(FL_MainAxisAlignment main_axis_alignment,
                                    FL_f32 free_space, FL_isize item_count,
                                    bool flipped, FL_f32 spacing,
                                    FL_f32 *leading_space,
                                    FL_f32 *between_space) {
  switch (main_axis_alignment) {
    case FL_MainAxisAlignment_Start: {
      if (flipped) {
        *leading_space = free_space;
      } else {
        *leading_space = 0;
      }
      *between_space = spacing;
    } break;

    case FL_MainAxisAlignment_End: {
      FL_Flex_DistributeSpace(FL_MainAxisAlignment_Start, free_space,
                              item_count, !flipped, spacing, leading_space,
                              between_space);
    } break;

    case FL_MainAxisAlignment_SpaceBetween: {
      if (item_count < 2) {
        FL_Flex_DistributeSpace(FL_MainAxisAlignment_Start, free_space,
                                item_count, flipped, spacing, leading_space,
                                between_space);
      } else {
        *leading_space = 0;
        *between_space = free_space / (FL_f32)(item_count - 1) + spacing;
      }
    } break;

    case FL_MainAxisAlignment_SpaceAround: {
      if (item_count == 0) {
        FL_Flex_DistributeSpace(FL_MainAxisAlignment_Start, free_space,
                                item_count, flipped, spacing, leading_space,
                                between_space);
      } else {
        *leading_space = free_space / (FL_f32)item_count / 2;
        *between_space = free_space / (FL_f32)item_count + spacing;
      }
    } break;

    case FL_MainAxisAlignment_Center: {
      *leading_space = free_space / 2.0f;
      *between_space = spacing;
    } break;

    case FL_MainAxisAlignment_SpaceEvenly: {
      *leading_space = free_space / (FL_f32)(item_count + 1);
      *between_space = free_space / (FL_f32)(item_count + 1) + spacing;
    } break;

    default:
      FL_UNREACHABLE;
  }
}

static FL_f32 GetChildCrossAxisOffset(
    FL_CrossAxisAlignment cross_axis_alignment, FL_f32 free_space,
    bool flipped) {
  switch (cross_axis_alignment) {
    case FL_CrossAxisAlignment_Stretch:
    case FL_CrossAxisAlignment_Baseline: {
      return 0.0f;
    } break;

    case FL_CrossAxisAlignment_Start: {
      return flipped ? free_space : 0.0f;
    } break;

    case FL_CrossAxisAlignment_Center: {
      return free_space / 2.0f;
    } break;

    case FL_CrossAxisAlignment_End: {
      return GetChildCrossAxisOffset(FL_CrossAxisAlignment_Start, free_space,
                                     !flipped);
    } break;

    default:
      FL_UNREACHABLE;
  }
}

static FL_f32 GetCrossSize(FL_Vec2 size, FL_Axis direction) {
  if (direction == FL_Axis_Horizontal) {
    return size.y;
  } else {
    return size.x;
  }
}

static FL_f32 GetMainSize(FL_Vec2 size, FL_Axis direction) {
  if (direction == FL_Axis_Horizontal) {
    return size.x;
  } else {
    return size.y;
  }
}

static void FL_Flex_Layout(FL_Widget *widget, FL_BoxConstraints constraints) {
  FL_FlexProps *props = FL_Widget_GetProps(widget, FL_FlexProps);

  for (FL_WidgetListEntry *entry = props->children.first; entry;
       entry = entry->next) {
    FL_Widget_Mount(widget, entry->widget);
  }

  LayoutSize sizes = FL_Flex_ComputeSize(
      widget, props->direction, props->main_axis_size,
      props->cross_axis_alignment, props->spacing, constraints);
  FL_f32 cross_axis_extent = sizes.size.cross;
  widget->size = AxisSize_ToVec2(sizes.size, props->direction);
  // TODO: Handle overflow.

  FL_f32 remaining_space = FL_Max(0.0f, sizes.main_axis_free_space);
  // TODO: Handle text direction and vertical direction.
  FL_f32 leading_space;
  FL_f32 between_space;
  FL_Flex_DistributeSpace(props->main_axis_alignment, remaining_space,
                          widget->child_count, /* flipped= */ false,
                          props->spacing, &leading_space, &between_space);

  // Position all children in visual order: starting from the top-left child and
  // work towards the child that's farthest away from the origin.
  FL_f32 child_main_position = leading_space;
  for (FL_Widget *child = widget->first; child; child = child->next) {
    FL_f32 child_cross_position = GetChildCrossAxisOffset(
        props->cross_axis_alignment,
        cross_axis_extent - GetCrossSize(child->size, props->direction),
        /* flipped= */ false);
    if (props->direction == FL_Axis_Horizontal) {
      child->offset = (FL_Vec2){child_main_position, child_cross_position};
    } else {
      child->offset = (FL_Vec2){child_cross_position, child_main_position};
    }
    child_main_position +=
        GetMainSize(child->size, props->direction) + between_space;
  }
}

static FL_WidgetClass FL_FlexClass = {
    .name = "Flex",
    .props_size = FL_SIZE_OF(FL_FlexProps),
    .layout = FL_Flex_Layout,
};

FL_Widget *FL_Flex(const FL_FlexProps *props) {
  return FL_Widget_Create(&FL_FlexClass, props->key, props);
}

static FL_WidgetClass FL_ColumnClass = {
    .name = "Column",
    .props_size = FL_SIZE_OF(FL_FlexProps),
    .layout = FL_Flex_Layout,
};

FL_Widget *FL_Column(const FL_ColumnProps *props) {
  FL_FlexProps flex_props = {
      .key = props->key,
      .direction = FL_Axis_Vertical,
      .main_axis_alignment = props->main_axis_alignment,
      .main_axis_size = props->main_axis_size,
      .cross_axis_alignment = props->cross_axis_alignment,
      .spacing = props->spacing,
      .children = props->children,
  };
  return FL_Widget_Create(&FL_ColumnClass, flex_props.key, &flex_props);
}

static FL_WidgetClass FL_RowClass = {
    .name = "Row",
    .props_size = FL_SIZE_OF(FL_FlexProps),
    .layout = FL_Flex_Layout,
};

FL_Widget *FL_Row(const FL_RowProps *props) {
  FL_FlexProps flex_props = {
      .key = props->key,
      .direction = FL_Axis_Horizontal,
      .main_axis_alignment = props->main_axis_alignment,
      .main_axis_size = props->main_axis_size,
      .cross_axis_alignment = props->cross_axis_alignment,
      .spacing = props->spacing,
      .children = props->children,
  };
  return FL_Widget_Create(&FL_RowClass, flex_props.key, &flex_props);
}

static void FL_Flexible_Mount(FL_Widget *widget) {
  FL_FlexibleProps *props = FL_Widget_GetProps(widget, FL_FlexibleProps);
  FL_FlexContext *flex =
      FL_Widget_SetContext(widget, FL_FlexContext_ID, FL_FlexContext);
  *flex = (FL_FlexContext){
      .flex = props->flex,
      .fit = props->fit,
  };
  if (props->child) {
    FL_Widget_Mount(widget, props->child);
  }
}

static FL_WidgetClass FL_FlexibleClass = {
    .name = "Flexible",
    .props_size = FL_SIZE_OF(FL_FlexibleProps),
    .mount = FL_Flexible_Mount,
};

FL_Widget *FL_Flexible(const FL_FlexibleProps *props) {
  return FL_Widget_Create(&FL_FlexibleClass, props->key, props);
}

static void FL_Expanded_Mount(FL_Widget *widget) {
  FL_ExpandedProps *props = FL_Widget_GetProps(widget, FL_ExpandedProps);
  FL_FlexContext *flex =
      FL_Widget_SetContext(widget, FL_FlexContext_ID, FL_FlexContext);
  *flex = (FL_FlexContext){
      .flex = props->flex,
      .fit = FL_FlexFit_Tight,
  };
  if (props->child) {
    FL_Widget_Mount(widget, props->child);
  }
}

static FL_WidgetClass FL_ExpandedClass = {
    .name = "Expanded",
    .props_size = FL_SIZE_OF(FL_ExpandedProps),
    .mount = FL_Expanded_Mount,
};

FL_Widget *FL_Expanded(const FL_ExpandedProps *props) {
  return FL_Widget_Create(&FL_ExpandedClass, props->key, props);
}

void FL_Flex_Init(void) { FL_FlexContext_ID = FL_Context_Register(); }

#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>


static void FL_PointerListener_Mount(FL_Widget *widget) {
  FL_PointerListenerProps *props =
      FL_Widget_GetProps(widget, FL_PointerListenerProps);
  if (props->child) {
    FL_Widget_Mount(widget, props->child);
  }
}

static inline void MaybeCallCallback(FL_PointerListenerCallback *callback,
                                     void *context, FL_PointerEvent event) {
  if (callback) {
    callback(context, event);
  }
}

static bool FL_PointerListener_HitTest(FL_Widget *widget,
                                       FL_HitTestContext *context) {
  FL_PointerListenerProps *props =
      FL_Widget_GetProps(widget, FL_PointerListenerProps);
  return FL_Widget_HitTest_ByBehaviour(widget, context, props->behaviour);
}

static void FL_PointerListener_OnPointerEvent(FL_Widget *widget,
                                              FL_PointerEvent event) {
  FL_PointerListenerProps *props =
      FL_Widget_GetProps(widget, FL_PointerListenerProps);
  switch (event.type) {
    case FL_PointerEventType_Down: {
      MaybeCallCallback(props->on_down, props->context, event);
    } break;

    case FL_PointerEventType_Move: {
      MaybeCallCallback(props->on_move, props->context, event);
    } break;

    case FL_PointerEventType_Up: {
      MaybeCallCallback(props->on_up, props->context, event);
    } break;

    case FL_PointerEventType_Enter: {
      MaybeCallCallback(props->on_enter, props->context, event);
    } break;

    case FL_PointerEventType_Hover: {
      MaybeCallCallback(props->on_hover, props->context, event);
    } break;

    case FL_PointerEventType_Exit: {
      MaybeCallCallback(props->on_exit, props->context, event);
    } break;

    case FL_PointerEventType_Cancel: {
      MaybeCallCallback(props->on_cancel, props->context, event);
    } break;

    case FL_PointerEventType_Scroll: {
      MaybeCallCallback(props->on_scroll, props->context, event);
    } break;

    default: {
    } break;
  }
}

static FL_WidgetClass FL_PointerListenerClass = {
    .name = "PointerListener",
    .props_size = FL_SIZE_OF(FL_PointerListenerProps),
    .mount = FL_PointerListener_Mount,
    .hit_test = FL_PointerListener_HitTest,
    .on_pointer_event = FL_PointerListener_OnPointerEvent,
};

FL_Widget *FL_PointerListener(const FL_PointerListenerProps *props) {
  return FL_Widget_Create(&FL_PointerListenerClass, props->key, props);
}

typedef struct FL_TapGestureRecognizer {
  FL_Widget *widget;
  void *context;
  FL_GestureCallback *tap_down;
  FL_GestureCallback *tap_up;
  FL_GestureCallback *tap;
  FL_GestureCallback *tap_cancel;

  FL_GestureArenaEntry *entry;
  FL_PointerEvent down;
  FL_PointerEvent up;
  FL_f32 time;
  bool won_arena;
  bool sent_down;
} FL_TapGestureRecognizer;

static void FL_TapGestureRecognizer_Reset(FL_TapGestureRecognizer *state) {
  if (state->entry) {
    FL_GestureArena_Resolve(state->entry, /* accepted= */ false);
  }

  state->down = (FL_PointerEvent){0};
  state->up = (FL_PointerEvent){0};
  state->time = 0;
  state->won_arena = false;
  state->sent_down = false;
}

static void FL_TapGestureRecognizer_CheckDown(FL_TapGestureRecognizer *state) {
  if (state->sent_down) {
    return;
  }

  FL_ASSERT(state->down.type == FL_PointerEventType_Down);

  if (state->tap_down) {
    state->tap_down(state->context,
                    (FL_GestureDetails){
                        .local_position = state->down.local_position,
                    });
  }

  state->sent_down = true;
}

static void FL_TapGestureRecognizer_CheckUp(FL_TapGestureRecognizer *state) {
  if (!state->won_arena || state->up.type != FL_PointerEventType_Up) {
    return;
  }

  if (state->tap_up) {
    state->tap_up(state->context,
                  (FL_GestureDetails){
                      .local_position = state->up.local_position,
                  });
  }

  if (state->tap) {
    state->tap(state->context, (FL_GestureDetails){
                                   .local_position = state->up.local_position,
                               });
  }

  FL_TapGestureRecognizer_Reset(state);
}

static void FL_TapGestureRecognizer_Update(FL_TapGestureRecognizer *state) {
  if (state->entry) {
    FL_GestureArena_Update(state->entry, state);
  }

  if (state->down.type == FL_PointerEventType_Down && !state->sent_down) {
    state->time += FL_Widget_GetDeltaTime(state->widget);
    if (state->time >= 0.1f) {
      FL_TapGestureRecognizer_CheckDown(state);
    }
  }
}

static void FL_TapGestureRecognizer_Cancel(FL_TapGestureRecognizer *state) {
  if (state->entry) {
    FL_GestureArena_Resolve(state->entry, /* accepted= */ false);
  } else {
    if (state->sent_down) {
      if (state->tap_cancel) {
        state->tap_cancel(state->context, (FL_GestureDetails){0});
      }
    }
    FL_TapGestureRecognizer_Reset(state);
  }
}

static void FL_TapGestureRecognizer_Accept(void *ctx, FL_i32 pointer) {
  FL_TapGestureRecognizer *state = ctx;
  FL_ASSERT(state->down.type == FL_PointerEventType_Down);
  FL_ASSERT(state->down.pointer == pointer);

  state->entry = 0;
  state->won_arena = true;

  FL_TapGestureRecognizer_CheckDown(state);
  FL_TapGestureRecognizer_CheckUp(state);
}

static void FL_TapGestureRecognizer_Reject(void *ctx, FL_i32 pointer) {
  FL_TapGestureRecognizer *state = ctx;
  FL_ASSERT(state->down.type == FL_PointerEventType_Down);
  FL_ASSERT(state->down.pointer == pointer);

  state->entry = 0;
  FL_TapGestureRecognizer_Cancel(state);
}

static FL_GestureArenaMemberOps FL_TapGestureRecognizer_ops = {
    .accept = FL_TapGestureRecognizer_Accept,
    .reject = FL_TapGestureRecognizer_Reject,
};

static void FL_TapGestureRecognizer_OnPointerEvent(
    FL_TapGestureRecognizer *state, FL_PointerEvent event) {
  switch (event.type) {
    case FL_PointerEventType_Down: {
      if (state->down.type != FL_PointerEventType_Down) {
        FL_u32 allowed_button = 0;
        if (state->tap_down || state->tap_up || state->tap ||
            state->tap_cancel) {
          allowed_button |= FL_BUTTON_PRIMARY;
        }
        if (event.button & allowed_button) {
          state->down = event;
          state->entry = FL_GestureArena_Add(
              event.pointer, &FL_TapGestureRecognizer_ops, state);
        }
      }
    } break;

    case FL_PointerEventType_Move: {
      if (state->down.type == FL_PointerEventType_Down) {
        FL_f32 dist_squared = FL_Vec2_GetLenSquared(
            FL_Vec2_Sub(event.position, state->down.position));
        if (dist_squared > 18 * 18) {
          FL_TapGestureRecognizer_Cancel(state);
        }
      }
    } break;

    case FL_PointerEventType_Up: {
      if (state->down.type == FL_PointerEventType_Down &&
          state->down.pointer == event.pointer &&
          FL_Vec2_Contains(event.local_position, FL_Vec2_Zero(),
                           state->widget->size)) {
        state->up = event;
        FL_TapGestureRecognizer_CheckUp(state);
      } else {
        FL_TapGestureRecognizer_Cancel(state);
      }
    } break;

    case FL_PointerEventType_Cancel: {
      FL_TapGestureRecognizer_Cancel(state);
    } break;

    default: {
    } break;
  }
}

typedef struct FL_DragGestureRecognizer {
  FL_Widget *widget;
  void *context;
  FL_GestureCallback *drag_down;
  FL_GestureCallback *drag_start;
  FL_GestureCallback *drag_update;
  FL_GestureCallback *drag_end;
  FL_GestureCallback *drag_cancel;

  FL_GestureArenaEntry *entry;
  FL_PointerEvent down;
  FL_Vec2 last_position;
  FL_Vec2 position;
  FL_Vec2 local_position;
  bool won_arena;
} FL_DragGestureRecognizer;

static void FL_DragGestureRecognizer_Update(FL_DragGestureRecognizer *state) {
  if (state->entry) {
    FL_GestureArena_Update(state->entry, state);
  }
}

static void FL_DragGestureRecognizer_Cancel(FL_DragGestureRecognizer *state) {
  if (state->entry) {
    FL_GestureArena_Resolve(state->entry, /* accepted= */ false);
  } else {
    if (state->down.pointer) {
      if (state->won_arena) {
        if (state->drag_end) {
          state->drag_end(state->context,
                          (FL_GestureDetails){
                              .local_position = state->local_position,
                          });
        }
      } else {
        if (state->drag_cancel) {
          state->drag_cancel(state->context,
                             (FL_GestureDetails){
                                 .local_position = state->local_position,
                             });
        }
      }
    }

    FL_ASSERT(!state->entry);
    state->down = (FL_PointerEvent){0};
    state->last_position = (FL_Vec2){0};
    state->position = (FL_Vec2){0};
    state->local_position = (FL_Vec2){0};
    state->won_arena = false;
  }
}

static void FL_DragGestureRecognizer_Accept(void *ctx, FL_i32 pointer) {
  FL_DragGestureRecognizer *state = ctx;
  FL_ASSERT(state->down.type == FL_PointerEventType_Down);
  FL_ASSERT(state->down.pointer == pointer);

  state->won_arena = true;
  state->entry = 0;

  if (state->drag_start) {
    state->drag_start(state->context,
                      (FL_GestureDetails){
                          .local_position = state->down.local_position,
                      });
  }
}

static void FL_DragGestureRecognizer_Reject(void *ctx, FL_i32 pointer) {
  FL_DragGestureRecognizer *state = ctx;
  FL_ASSERT(state->down.type == FL_PointerEventType_Down);
  FL_ASSERT(state->down.pointer == pointer);

  state->entry = 0;

  FL_DragGestureRecognizer_Cancel(state);
}

static FL_GestureArenaMemberOps FL_DragGestureRecognizerOps = {
    .accept = FL_DragGestureRecognizer_Accept,
    .reject = FL_DragGestureRecognizer_Reject,
};

static void FL_DragGestureRecognizer_OnPointerEvent(
    FL_DragGestureRecognizer *state, FL_PointerEvent event) {
  switch (event.type) {
    case FL_PointerEventType_Down: {
      if (!state->down.pointer) {
        state->entry = FL_GestureArena_Add(event.pointer,
                                           &FL_DragGestureRecognizerOps, state);
        state->down = event;
        state->position = event.position;
        state->local_position = event.local_position;
        if (state->drag_down) {
          state->drag_down(state->context,
                           (FL_GestureDetails){
                               .local_position = event.local_position,
                           });
        }
      }
    } break;

    case FL_PointerEventType_Move: {
      if (state->down.pointer == event.pointer) {
        state->last_position = state->position;
        state->position = event.position;
        state->local_position = event.local_position;
        if (state->won_arena) {
          FL_Vec2 delta = FL_Vec2_Sub(state->position, state->last_position);
          if (state->drag_update) {
            state->drag_update(state->context,
                               (FL_GestureDetails){
                                   .local_position = state->local_position,
                                   .delta = delta,
                               });
          }
        } else if (FL_Vec2_GetLenSquared(FL_Vec2_Sub(
                       state->position, state->down.position)) > 4.0f) {
          FL_GestureArena_Resolve(state->entry, /* accepted= */ true);
        }
      } else {
        FL_DragGestureRecognizer_Cancel(state);
      }
    } break;

    case FL_PointerEventType_Up:
    case FL_PointerEventType_Cancel: {
      if (state->down.pointer == event.pointer) {
        state->last_position = state->position;
        state->position = event.position;
        state->local_position = event.local_position;
        FL_DragGestureRecognizer_Cancel(state);
      }
    } break;

    default: {
    } break;
  }
}

typedef struct FL_GestureDetectorState {
  FL_TapGestureRecognizer *tap;
  FL_DragGestureRecognizer *drag;
} FL_GestureDetectorState;

static void *DupOrPushZero_(FL_Arena *arena, void *src, FL_isize size,
                            FL_isize alignment) {
  if (src) {
    return FL_Arena_Dup(arena, src, size, alignment);
  }
  void *dst = FL_Arena_Push(arena, size, alignment);
  memset(dst, 0, size);
  return dst;
}

#define DupOrPushZero(arena, src) \
  (FL_TYPE_OF(src))(DupOrPushZero_(arena, src, sizeof(*src), alignof(*src)))

static void FL_GestureDetector_Mount(FL_Widget *widget) {
  FL_GestureDetectorProps *props =
      FL_Widget_GetProps(widget, FL_GestureDetectorProps);
  FL_GestureDetectorState *state =
      FL_Widget_GetState(widget, FL_GestureDetectorState);

  FL_Arena *arena = FL_Widget_GetArena(widget);
  if (props->tap_down || props->tap_up || props->tap || props->tap_cancel) {
    state->tap = DupOrPushZero(arena, state->tap);
    state->tap->widget = widget;
    state->tap->context = props->context;
    state->tap->tap_down = props->tap_down;
    state->tap->tap_up = props->tap_up;
    state->tap->tap = props->tap;
    state->tap->tap_cancel = props->tap_cancel;
    FL_TapGestureRecognizer_Update(state->tap);
  } else if (state->tap) {
    state->tap->widget = widget;
    state->tap->context = 0;
    state->tap->tap_down = 0;
    state->tap->tap_up = 0;
    state->tap->tap = 0;
    state->tap->tap_cancel = 0;
    FL_TapGestureRecognizer_Cancel(state->tap);
  }

  if (props->drag_down || props->drag_start || props->drag_update ||
      props->drag_end || props->drag_cancel) {
    state->drag = DupOrPushZero(arena, state->drag);
    state->drag->widget = widget;
    state->drag->context = props->context;
    state->drag->drag_down = props->drag_down;
    state->drag->drag_start = props->drag_start;
    state->drag->drag_update = props->drag_update;
    state->drag->drag_end = props->drag_end;
    state->drag->drag_cancel = props->drag_cancel;
    FL_DragGestureRecognizer_Update(state->drag);
  } else if (state->drag) {
    state->drag->widget = widget;
    state->drag->context = 0;
    state->drag->drag_down = 0;
    state->drag->drag_start = 0;
    state->drag->drag_update = 0;
    state->drag->drag_end = 0;
    state->drag->drag_cancel = 0;
    FL_DragGestureRecognizer_Cancel(state->drag);
  }

  if (props->child) {
    FL_Widget_Mount(widget, props->child);
  }
}

static void FL_GestureDetector_Unmount(FL_Widget *widget) {
  if (widget->link) {
    return;
  }

  FL_GestureDetectorState *state =
      FL_Widget_GetState(widget, FL_GestureDetectorState);

  if (state->tap) {
    FL_TapGestureRecognizer_Cancel(state->tap);
  }

  if (state->drag) {
    FL_DragGestureRecognizer_Cancel(state->drag);
  }
}

static bool FL_GestureDetector_HitTest(FL_Widget *widget,
                                       FL_HitTestContext *context) {
  FL_GestureDetectorProps *props =
      FL_Widget_GetProps(widget, FL_GestureDetectorProps);
  return FL_Widget_HitTest_ByBehaviour(widget, context, props->behaviour);
}

static void FL_GestureDetector_OnPointerEvent(FL_Widget *widget,
                                              FL_PointerEvent event) {
  FL_GestureDetectorState *state =
      FL_Widget_GetState(widget, FL_GestureDetectorState);
  if (state->tap) {
    FL_TapGestureRecognizer_OnPointerEvent(state->tap, event);
  }
  if (state->drag) {
    FL_DragGestureRecognizer_OnPointerEvent(state->drag, event);
  }
}

static FL_WidgetClass FL_GestureDetectorClass = {
    .name = "GestureDetector",
    .props_size = FL_SIZE_OF(FL_GestureDetectorProps),
    .state_size = FL_SIZE_OF(FL_GestureDetectorState),
    .mount = FL_GestureDetector_Mount,
    .unmount = FL_GestureDetector_Unmount,
    .hit_test = FL_GestureDetector_HitTest,
    .on_pointer_event = FL_GestureDetector_OnPointerEvent,
};

FL_Widget *FL_GestureDetector(const FL_GestureDetectorProps *props) {
  return FL_Widget_Create(&FL_GestureDetectorClass, props->key, props);
}

#include <stdint.h>


static FL_ContextID FL_PositionedContext_ID;

typedef struct FLStackState {
  bool has_visual_overflow;
} FLStackState;

typedef struct FLPositionedContext {
  FL_f32o left;
  FL_f32o right;
  FL_f32o top;
  FL_f32o bottom;
  FL_f32o width;
  FL_f32o height;
} FLPositionedContext;

static inline bool is_positioned(FLPositionedContext *self) {
  if (!self) {
    return false;
  }

  return self->left.present || self->right.present || self->top.present ||
         self->bottom.present || self->width.present || self->height.present;
}

static FL_Vec2 FL_Stack_compute_size(FL_Widget *widget, FL_StackFit fit,
                                     FL_BoxConstraints constraints) {
  if (!widget->first) {
    FL_Vec2 biggest = FL_BoxConstraints_GetBiggest(constraints);
    if (FL_Vec2_IsFinite(biggest)) {
      return biggest;
    }
    return FL_BoxConstraints_GetSmallest(constraints);
  }

  FL_f32 width = constraints.min_width;
  FL_f32 height = constraints.min_height;

  FL_BoxConstraints non_positioned_constraints;
  switch (fit) {
    case FL_StackFit_Loose: {
      non_positioned_constraints = FL_BoxConstraints_Loosen(constraints);
    } break;
    case FL_StackFit_Expand: {
      FL_Vec2 biggest = FL_BoxConstraints_GetBiggest(constraints);
      non_positioned_constraints =
          FL_BoxConstraints_Tight(biggest.x, biggest.y);
    } break;
    default: {
      non_positioned_constraints = constraints;
    } break;
  }

  bool has_non_positioned_child = false;
  for (FL_Widget *child = widget->first; child; child = child->next) {
    FLPositionedContext *positioned = FL_Widget_GetContext(
        child, FL_PositionedContext_ID, FLPositionedContext);

    if (!is_positioned(positioned)) {
      has_non_positioned_child = true;
      FL_Widget_Layout(child, non_positioned_constraints);
      FL_Vec2 child_size = child->size;

      width = FL_Max(width, child_size.x);
      height = FL_Max(height, child_size.y);
    }
  }

  FL_Vec2 size;
  if (has_non_positioned_child) {
    size = (FL_Vec2){width, height};
    FL_ASSERT(size.x == FL_BoxConstraints_ConstrainWidth(constraints, width));
    FL_ASSERT(size.y == FL_BoxConstraints_ConstrainHeight(constraints, height));
  } else {
    size = FL_BoxConstraints_GetBiggest(constraints);
  }

  FL_ASSERT(FL_Vec2_IsFinite(size));
  return size;
}

static FL_BoxConstraints get_positioned_child_constraints(
    FLPositionedContext *self, FL_Vec2 stack_size) {
  FL_f32o width = FL_f32_None();
  if (self->left.present && self->right.present) {
    width = FL_f32_Some(stack_size.x - self->right.value - self->left.value);
  } else {
    width = self->width;
  }

  FL_f32o height = FL_f32_None();
  if (self->top.present && self->bottom.present) {
    height = FL_f32_Some(stack_size.y - self->bottom.value - self->top.value);
  } else {
    height = self->height;
  }

  FL_ASSERT(!width.present || !FL_IsNaN(width.value));
  FL_ASSERT(!height.present || !FL_IsNaN(height.value));

  if (width.present) {
    width.value = FL_Max(0, width.value);
  }
  if (height.present) {
    height.value = FL_Max(0, height.value);
  }

  return FL_BoxConstraints_TightFor(width, height);
}

/** Returns true when the child has visual overflow. */
static bool layout_positioned_child(FL_Widget *child,
                                    FLPositionedContext *positioned,
                                    FL_Vec2 size) {
  FL_BoxConstraints child_constraints =
      get_positioned_child_constraints(positioned, size);
  FL_Widget_Layout(child, child_constraints);
  FL_Vec2 child_size = child->size;

  FL_f32 x;
  if (positioned->left.present) {
    x = positioned->left.value;
  } else if (positioned->right.present) {
    x = size.x - positioned->right.value - child_size.x;
  } else {
    // TODO: alignment
    x = 0;
  }

  FL_f32 y;
  if (positioned->top.present) {
    y = positioned->top.value;
  } else if (positioned->bottom.present) {
    y = size.y - positioned->bottom.value - child_size.y;
  } else {
    // TODO: alignment
    y = 0;
  }

  child->offset = (FL_Vec2){x, y};

  return x < 0 || x + child_size.x > size.x || y < 0 ||
         y + child_size.y > size.y;
}

static void FL_Stack_Layout(FL_Widget *widget, FL_BoxConstraints constraints) {
  FL_StackProps *props = FL_Widget_GetProps(widget, FL_StackProps);
  FLStackState *state = FL_Widget_GetState(widget, FLStackState);

  for (FL_WidgetListEntry *entry = props->children.first; entry;
       entry = entry->next) {
    FL_Widget_Mount(widget, entry->widget);
  }

  FL_Vec2 size = FL_Stack_compute_size(widget, props->fit, constraints);
  widget->size = size;

  state->has_visual_overflow = false;
  for (FL_Widget *child = widget->first; child; child = child->next) {
    FLPositionedContext *positioned = FL_Widget_GetContext(
        child, FL_PositionedContext_ID, FLPositionedContext);
    if (!is_positioned(positioned)) {
      // TODO: alignment
    } else {
      state->has_visual_overflow =
          layout_positioned_child(child, positioned, size) ||
          state->has_visual_overflow;
    }
  }
}

static void FL_Stack_Paint(FL_Widget *widget, FL_PaintingContext *context,
                           FL_Vec2 offset) {
  FLStackState *state = FL_Widget_GetState(widget, FLStackState);

  bool should_clip = state->has_visual_overflow;
  if (should_clip) {
    FL_Draw_PushClipRect(context, FL_Rect_FromMinSize(offset, widget->size));
  }

  FL_Widget_Paint_Default(widget, context, offset);

  if (should_clip) {
    FL_Draw_PopClipRect(context);
  }
}

static FL_WidgetClass FL_StackClass = {
    .name = "Stack",
    .props_size = FL_SIZE_OF(FL_StackProps),
    .state_size = FL_SIZE_OF(FLStackState),
    .layout = FL_Stack_Layout,
    .paint = FL_Stack_Paint,
};

FL_Widget *FL_Stack(const FL_StackProps *props) {
  return FL_Widget_Create(&FL_StackClass, props->key, props);
}

static void FL_Positioned_Mount(FL_Widget *widget) {
  FL_PositionedProps *props = FL_Widget_GetProps(widget, FL_PositionedProps);
  FLPositionedContext *positioned = FL_Widget_SetContext(
      widget, FL_PositionedContext_ID, FLPositionedContext);
  *positioned = (FLPositionedContext){
      .left = props->left,
      .right = props->right,
      .top = props->top,
      .bottom = props->bottom,
      .width = props->width,
      .height = props->height,
  };
  if (props->child) {
    FL_Widget_Mount(widget, props->child);
  }
}

FL_WidgetClass FL_PositionedClass = {
    .name = "Positioned",
    .props_size = FL_SIZE_OF(FL_PositionedProps),
    .mount = FL_Positioned_Mount,
};

FL_Widget *FL_Positioned(const FL_PositionedProps *props) {
  return FL_Widget_Create(&FL_PositionedClass, props->key, props);
}

void FL_Stack_Init(void) { FL_PositionedContext_ID = FL_Context_Register(); }


typedef struct FL_TextState {
  FL_f32 font_size;
  FL_Color color;
} FL_TextState;

static void FL_Text_Layout(FL_Widget *widget, FL_BoxConstraints constraints) {
  FL_TextProps *props = FL_Widget_GetProps(widget, FL_TextProps);
  FL_TextState *state = FL_Widget_GetState(widget, FL_TextState);

  // TODO: Get default text style from widget tree.
  FL_f32 font_size = 13;
  if (props->style.present) {
    if (props->style.value.font_size.present) {
      font_size = props->style.value.font_size.value;
    }
  }
  state->font_size = font_size;

  // TODO: Get default text style from widget tree.
  FL_Color color = FL_COLOR_RGBA(255, 255, 255, 255);
  if (props->style.present) {
    if (props->style.value.color.present) {
      color = props->style.value.color.value;
    }
  }
  state->color = color;

  FL_Font_TextMetrics metrics =
      FL_Font_MeasureText(0, props->text, state->font_size, FL_INFINITY);

  // TODO: Handle overflow.
  widget->size = FL_BoxConstraints_Constrain(
      constraints,
      (FL_Vec2){
          metrics.width,
          metrics.font_bounding_box_ascent + metrics.font_bounding_box_descent,
      });
}

static void FL_Text_Paint(FL_Widget *widget, FL_PaintingContext *context,
                          FL_Vec2 offset) {
  FL_TextProps *props = FL_Widget_GetProps(widget, FL_TextProps);
  FL_TextState *state = FL_Widget_GetState(widget, FL_TextState);
  FL_Draw_AddText(context, 0, props->text, offset.x, offset.y, state->color,
                  state->font_size);
}

static FL_WidgetClass FL_TextClass = {
    .name = "Text",
    .props_size = FL_SIZE_OF(FL_TextProps),
    .state_size = FL_SIZE_OF(FL_TextState),
    .layout = FL_Text_Layout,
    .paint = FL_Text_Paint,
    .hit_test = FL_Widget_HitTest_Opaque,
};

FL_Widget *FL_Text(const FL_TextProps *props) {
  return FL_Widget_Create(&FL_TextClass, props->key, props);
}

#include <stdint.h>


FL_ContextID FL_SliverContext_ID;

static FL_NotificationID FL_ScrollNotification_ID;

typedef struct FL_ScrollNotification {
  /** The number of viewports that this notification has bubbled through. */
  FL_i32 depth;
  FL_f32 scroll_offset;
  FL_f32 max_scroll_extent;

  FL_Widget *scrollable;
  void (*scroll_to)(FL_Widget *widget, FL_f32 to);
} FL_ScrollNotification;

typedef struct FL_ViewportState {
  bool has_visual_overflow;
  FL_f32 max_scroll_extent;
  FL_f32 max_scroll_offset;
} FL_ViewportState;

static FL_ScrollDirection FL_ScrollDirection_Flip(
    FL_ScrollDirection scroll_direction) {
  switch (scroll_direction) {
    case FL_ScrollDirection_Forward: {
      return FL_ScrollDirection_Reverse;
    } break;

    case FL_ScrollDirection_Reverse: {
      return FL_ScrollDirection_Forward;
    } break;

    default: {
      return scroll_direction;
    } break;
  }
}

static FL_ScrollDirection FL_ScrollDirection_ApplyGrowthDirection(
    FL_ScrollDirection scroll_direction, FL_GrowthDirection growth) {
  if (growth == FL_GrowthDirection_Reverse) {
    return FL_ScrollDirection_Flip(scroll_direction);
  }

  return scroll_direction;
}

static FL_AxisDirection FL_AxisDirection_Flip(FL_AxisDirection self) {
  switch (self) {
    case FL_AxisDirection_Up: {
      return FL_AxisDirection_Down;
    } break;

    case FL_AxisDirection_Down: {
      return FL_AxisDirection_Up;
    } break;

    case FL_AxisDirection_Left: {
      return FL_AxisDirection_Right;
    } break;

    case FL_AxisDirection_Right: {
      return FL_AxisDirection_Left;
    } break;

    default: {
      FL_UNREACHABLE;
    } break;
  }
}

static FL_AxisDirection FL_AxisDirection_ApplyGrowthDirection(
    FL_AxisDirection self, FL_GrowthDirection growth) {
  if (growth == FL_GrowthDirection_Reverse) {
    return FL_AxisDirection_Flip(self);
  }
  return self;
}

static FL_f32 FL_Viewport_LayoutChildren(
    FL_Widget *widget, FL_ViewportProps *props, FL_ViewportState *state,
    FL_Widget *child, FL_f32 scroll_offset, FL_f32 overlap,
    FL_f32 layout_offset, FL_f32 remaining_painting_extent,
    FL_f32 main_axis_extent, FL_f32 cross_axis_extent,
    FL_GrowthDirection growth_direction, FL_f32 remaining_cache_extent,
    FL_f32 cache_origin) {
  FL_ASSERT(FL_IsFinite(scroll_offset));
  FL_ASSERT(scroll_offset >= 0);
  FL_f32 initial_layout_offset = layout_offset;
  FL_ScrollDirection scroll_direction = FL_ScrollDirection_ApplyGrowthDirection(
      props->offset.scroll_direction, growth_direction);
  FL_f32 max_paint_offset = layout_offset + overlap;
  FL_f32 preceeding_scroll_extent = 0;

  while (child) {
    FL_f32 sliver_scroll_offset = scroll_offset < 0 ? 0 : scroll_offset;
    FL_f32 corrected_cache_origin = FL_Max(cache_origin, -sliver_scroll_offset);
    FL_f32 cache_extent_correction = cache_origin - corrected_cache_origin;
    FL_ASSERT(sliver_scroll_offset >= FL_Abs(corrected_cache_origin));
    FL_ASSERT(corrected_cache_origin <= 0);
    FL_ASSERT(sliver_scroll_offset >= 0);
    FL_ASSERT(cache_extent_correction <= 0);

    FL_SliverContext *sliver =
        FL_Widget_SetContext(child, FL_SliverContext_ID, FL_SliverContext);
    *sliver = (FL_SliverContext){
        .constraints =
            {
                .axis_direction = props->axis_direction,
                .growth_direction = growth_direction,
                .scroll_direction = scroll_direction,
                .scroll_offset = sliver_scroll_offset,
                .preceeding_scroll_extent = preceeding_scroll_extent,
                .overlap = max_paint_offset - layout_offset,
                .remaining_paint_extent =
                    FL_Max(0, remaining_painting_extent - layout_offset +
                                  initial_layout_offset),
                .cross_axis_extent = cross_axis_extent,
                .cross_axis_direction = props->cross_axis_direction,
                .main_axis_extent = main_axis_extent,
                .remaining_cache_extent =
                    FL_Max(0, remaining_cache_extent + cache_extent_correction),
                .cache_origin = corrected_cache_origin,
            },
        .layout_offset = layout_offset,
    };
    FL_Widget_Layout(child, FL_BoxConstraints_FromSliverConstraints(
                                sliver->constraints, 0, FL_INFINITY));

    if (sliver->geometry.scroll_offset_correction != 0) {
      return sliver->geometry.scroll_offset_correction;
    }

    state->has_visual_overflow |= sliver->geometry.has_visual_overflow;

    FL_f32 effective_layout_offset =
        layout_offset + sliver->geometry.paint_origin;
    switch (FL_AxisDirection_ApplyGrowthDirection(props->axis_direction,
                                                  growth_direction)) {
      case FL_AxisDirection_Up: {
        child->offset = (FL_Vec2){
            0, widget->size.y - layout_offset - sliver->geometry.paint_extent};
      } break;

      case FL_AxisDirection_Down: {
        child->offset = (FL_Vec2){0, layout_offset};
      } break;

      case FL_AxisDirection_Left: {
        child->offset = (FL_Vec2){
            widget->size.x - layout_offset - sliver->geometry.paint_extent, 0};
      } break;

      case FL_AxisDirection_Right: {
        child->offset = (FL_Vec2){layout_offset, 0};
      } break;

      default: {
        FL_UNREACHABLE;
      } break;
    }

    max_paint_offset =
        FL_Max(effective_layout_offset + sliver->geometry.paint_extent,
               max_paint_offset);
    scroll_offset -= sliver->geometry.scroll_extent;
    preceeding_scroll_extent += sliver->geometry.scroll_extent;
    layout_offset += sliver->geometry.layout_extent;
    if (sliver->geometry.cache_extent != 0) {
      remaining_cache_extent -=
          sliver->geometry.cache_extent - cache_extent_correction;
      cache_origin =
          FL_Min(corrected_cache_origin + sliver->geometry.cache_extent, 0);
    }

    child = child->next;
  }

  state->max_scroll_extent += preceeding_scroll_extent;

  return 0;
}

static FL_f32 FL_Viewport_AttemptLayout(
    FL_Widget *widget, FL_ViewportProps *props, FL_ViewportState *state,
    FL_f32 main_axis_extent, FL_f32 cross_axis_extent, FL_f32 offset) {
  FL_f32 center_offset = main_axis_extent * props->anchor - offset;
  FL_f32 reverse_direction_remaining_paint_extent =
      FL_Clamp(center_offset, 0, main_axis_extent);
  FL_f32 forward_direction_remaining_paint_extent =
      FL_Clamp(main_axis_extent - center_offset, 0, main_axis_extent);

  FL_f32 cache_extent = props->cache_extent;
  FL_f32 full_cache_extent = main_axis_extent + 2 * cache_extent;
  FL_f32 center_cache_offset = center_offset + cache_extent;
  FL_f32 reverse_direction_remaining_cache_extent =
      FL_Clamp(center_cache_offset, 0, full_cache_extent);
  // TODO: reverse scroll direction
  (void)reverse_direction_remaining_cache_extent;
  FL_f32 forward_direction_remaining_cache_extent =
      FL_Clamp(full_cache_extent - center_offset, 0, full_cache_extent);

  state->has_visual_overflow = false;
  state->max_scroll_extent = cache_extent;
  return FL_Viewport_LayoutChildren(
      widget, props, state, /* child= */ widget->first,
      /* scroll_offset= */ FL_Max(0, -center_offset),
      /* overlap= */ FL_Min(0, -center_offset),
      /* layout_offset= */ center_offset >= main_axis_extent
          ? center_offset
          : reverse_direction_remaining_paint_extent,
      forward_direction_remaining_paint_extent, main_axis_extent,
      cross_axis_extent, FL_GrowthDirection_Forward,
      forward_direction_remaining_cache_extent,
      FL_Clamp(center_offset, -cache_extent, 0));
}

static void FL_Viewport_Layout(FL_Widget *widget,
                               FL_BoxConstraints constraints) {
  FL_ViewportProps *props = FL_Widget_GetProps(widget, FL_ViewportProps);
  for (FL_WidgetListEntry *entry = props->slivers.first; entry;
       entry = entry->next) {
    FL_Widget_Mount(widget, entry->widget);
  }

  FL_Vec2 size = FL_BoxConstraints_GetBiggest(constraints);
  widget->size = size;

  if (!widget->first) {
    return;
  }

  if (FL_IsInfinite(size.x) || FL_IsInfinite(size.y)) {
    FL_DEBUG_ASSERT_F(false, "Cannot layout Viewport with infinity space.");
    return;
  }

  FL_ViewportState *state = FL_Widget_GetState(widget, FL_ViewportState);
  FL_f32 main_axis_extent = size.x;
  FL_f32 cross_axis_extent = size.y;
  if (FL_Axis_FromAxisDirection(props->axis_direction) == FL_Axis_Vertical) {
    main_axis_extent = size.y;
    cross_axis_extent = size.x;
  }

  FL_isize max_layout_counts = 10 * widget->child_count;
  FL_isize layout_index = 0;
  for (; layout_index < max_layout_counts; ++layout_index) {
    FL_f32 correction =
        FL_Viewport_AttemptLayout(widget, props, state, main_axis_extent,
                                  cross_axis_extent, props->offset.points);
    if (correction != 0.0f) {
      // TODO
      FL_UNREACHABLE;
    } else {
      break;
    }
  }
  FL_ASSERT(layout_index < max_layout_counts);

  state->max_scroll_offset =
      FL_Max(0, state->max_scroll_extent - main_axis_extent);
}

static void FL_Viewport_Paint(FL_Widget *widget, FL_PaintingContext *context,
                              FL_Vec2 offset) {
  FL_ViewportState *state = FL_Widget_GetState(widget, FL_ViewportState);

  bool should_clip = state->has_visual_overflow;
  if (should_clip) {
    FL_Draw_PushClipRect(context, FL_Rect_FromMinSize(offset, widget->size));
  }

  for (FL_Widget *child = widget->first; child; child = child->next) {
    FL_SliverContext *sliver =
        FL_Widget_GetContext(child, FL_SliverContext_ID, FL_SliverContext);
    FL_ASSERT(sliver);
    if (sliver->geometry.paint_extent > 0) {
      for (FL_Widget *child = widget->first; child; child = child->next) {
        FL_Widget_Paint(child, context, FL_Vec2_Add(offset, child->offset));
      }
    }
  }

  if (should_clip) {
    FL_Draw_PopClipRect(context);
  }
}

static bool FL_Viewport_OnNotification(FL_Widget *widget, FL_NotificationID id,
                                       void *data) {
  (void)widget;

  if (id == FL_ScrollNotification_ID) {
    FL_ScrollNotification *scroll = data;
    scroll->depth += 1;
  }

  return false;
}

static FL_WidgetClass FL_ViewportClass = {
    .name = "Viewport",
    .props_size = FL_SIZE_OF(FL_ViewportProps),
    .state_size = FL_SIZE_OF(FL_ViewportState),
    .layout = FL_Viewport_Layout,
    .paint = FL_Viewport_Paint,
    .hit_test = FL_Widget_HitTest_Opaque,
    .on_notification = FL_Viewport_OnNotification,
};

FL_Widget *FL_Viewport(const FL_ViewportProps *props) {
  return FL_Widget_Create(&FL_ViewportClass, props->key, props);
}

typedef struct FL_ScrollbarState {
  FL_ScrollNotification scroll;

  FL_f32 ratio;
  FL_f32 handle_padding_top;
  FL_f32 handle_extent;

  bool hovering;
} FL_ScrollbarState;

static void FL_Scrollbar_ScrollTo(void *ctx, FL_GestureDetails details) {
  FL_Widget *widget = ctx;
  FL_ScrollbarState *state = FL_Widget_GetState(widget, FL_ScrollbarState);

  FL_f32 offset = details.local_position.y - state->handle_extent / 2.0f;
  if (state->scroll.scroll_to) {
    state->scroll.scroll_to(state->scroll.scrollable, offset / state->ratio);
  }
}

static void FL_Scrollbar_OnEnterHandle(void *ctx, FL_PointerEvent event) {
  (void)event;
  FL_Widget *widget = ctx;
  FL_ScrollbarState *state = FL_Widget_GetState(widget, FL_ScrollbarState);
  state->hovering = true;
}

static void FL_Scrollbar_OnExitHandle(void *ctx, FL_PointerEvent event) {
  (void)event;
  FL_Widget *widget = ctx;
  FL_ScrollbarState *state = FL_Widget_GetState(widget, FL_ScrollbarState);
  state->hovering = false;
}

static bool FL_Scrollbar_OnNotification(FL_Widget *widget, FL_NotificationID id,
                                        void *data) {
  if (id == FL_ScrollNotification_ID) {
    FL_ScrollNotification *scroll = data;
    if (scroll->depth == 0) {
      FL_ScrollbarState *state = FL_Widget_GetState(widget, FL_ScrollbarState);
      state->scroll = *scroll;
      return true;
    }
  }

  return false;
}

static void FL_Scrollbar_Layout(FL_Widget *widget,
                                FL_BoxConstraints constraints) {
  FL_ScrollbarProps *props = FL_Widget_GetProps(widget, FL_ScrollbarProps);
  FL_ScrollbarState *state = FL_Widget_GetState(widget, FL_ScrollbarState);
  FL_Widget *child = props->child;

  if (!child) {
    return;
  }

  FL_Widget_Mount(widget, child);

  FL_f32 scrollbar_width = 10;
  FL_Widget_Layout(child,
                   FL_BoxConstraints_Deflate(constraints, scrollbar_width, 0));

  widget->size = FL_Vec2_Add(child->size, (FL_Vec2){scrollbar_width, 0});
  FL_Vec2 size = widget->size;

  state->ratio = size.y / state->scroll.max_scroll_extent;
  state->handle_extent = FL_Max(4, state->ratio * size.y);
  state->handle_padding_top = state->scroll.scroll_offset * state->ratio;
  FL_f32 handle_padding_bottom =
      size.y - state->handle_padding_top - state->handle_extent;

  FL_Widget *scrollbar = FL_GestureDetector(&(FL_GestureDetectorProps){
      .context = widget,
      .drag_start = FL_Scrollbar_ScrollTo,
      .drag_update = FL_Scrollbar_ScrollTo,
      .child = FL_Container(&(FL_ContainerProps){
          .color = FL_Color_Some(FL_COLOR_RGB(245, 245, 245)),
          .padding = FL_EdgeInsets_Some((FL_EdgeInsets){
              0, 0, state->handle_padding_top, handle_padding_bottom}),
          .child = FL_PointerListener(&(FL_PointerListenerProps){
              .context = widget,
              .on_enter = FL_Scrollbar_OnEnterHandle,
              .on_exit = FL_Scrollbar_OnExitHandle,
              .child = FL_Container(&(FL_ContainerProps){
                  .width = FL_f32_Some(size.x),
                  .color = FL_Color_Some(state->hovering
                                             ? FL_COLOR_RGB(148, 148, 148)
                                             : FL_COLOR_RGB(191, 191, 191)),
              }),
          }),
      }),
  });

  FL_Widget_Mount(widget, scrollbar);
  FL_Widget_Layout(scrollbar, FL_BoxConstraints_Tight(scrollbar_width, size.y));
  scrollbar->offset.x = child->size.x;
}

static FL_WidgetClass FL_ScrollbarClass = {
    .name = "Scrollbar",
    .props_size = FL_SIZE_OF(FL_ScrollbarProps),
    .state_size = FL_SIZE_OF(FL_ScrollbarState),
    .layout = FL_Scrollbar_Layout,
    .on_notification = FL_Scrollbar_OnNotification,
};

FL_Widget *FL_Scrollbar(const FL_ScrollbarProps *props) {
  return FL_Widget_Create(&FL_ScrollbarClass, props->key, props);
}

typedef struct FL_ScrollableState {
  FL_f32 target_scroll_offset;
  FL_f32 scroll_offset;
  FL_f32 max_scroll_offset;
} FL_ScrollableState;

static void FL_Scrollable_ScrollTo(FL_Widget *widget,
                                   FL_f32 target_scroll_offset) {
  FL_ScrollableProps *props = FL_Widget_GetProps(widget, FL_ScrollableProps);
  FL_ScrollableState *state = FL_Widget_GetState(widget, FL_ScrollableState);
  state->target_scroll_offset =
      FL_Clamp(target_scroll_offset, 0, state->max_scroll_offset);
  if (props->scroll) {
    *props->scroll = state->target_scroll_offset;
  }
}

static void FL_Scrollable_Layout(FL_Widget *widget,
                                 FL_BoxConstraints constraints) {
  FL_ScrollableProps *props = FL_Widget_GetProps(widget, FL_ScrollableProps);
  FL_ScrollableState *state = FL_Widget_GetState(widget, FL_ScrollableState);

  if (props->scroll) {
    FL_Scrollable_ScrollTo(widget, *props->scroll);
  }

  state->scroll_offset = FL_Widget_AnimateFast(widget, state->scroll_offset,
                                               state->target_scroll_offset);

  FL_Widget *viewport = FL_Viewport(&(FL_ViewportProps){
      // .axis_direction = props->axis_direction,
      // .cross_axis_direction = props->cross_axis_direction,
      .offset =
          {
              .points = state->scroll_offset,
          },
      // .cache_extent = props->cache_extent,
      .slivers = props->slivers,
  });
  FL_Widget_Mount(widget, viewport);

  FL_Widget_Layout(viewport, constraints);
  widget->size = viewport->size;

  FL_ViewportState *viewport_state =
      FL_Widget_GetState(viewport, FL_ViewportState);
  state->max_scroll_offset = viewport_state->max_scroll_offset;

  FL_ScrollNotification data = {
      .scroll_offset = state->scroll_offset,
      .max_scroll_extent = viewport_state->max_scroll_extent,
      .scrollable = widget,
      .scroll_to = FL_Scrollable_ScrollTo,
  };
  FL_Widget_SendNotification(widget, FL_ScrollNotification_ID, &data);
}

static void FL_Scrollable_OnPointerEvent(FL_Widget *widget,
                                         FL_PointerEvent event) {
  if (event.type == FL_PointerEventType_Scroll &&
      FL_PointerEventResolver_Register(widget)) {
    FL_ScrollableState *state = FL_Widget_GetState(widget, FL_ScrollableState);
    FL_Scrollable_ScrollTo(widget, state->target_scroll_offset + event.delta.y);
  }
}

static FL_WidgetClass FL_ScrollableClass = {
    .name = "Scrollable",
    .props_size = FL_SIZE_OF(FL_ScrollableProps),
    .state_size = FL_SIZE_OF(FL_ScrollableState),
    .layout = FL_Scrollable_Layout,
    .hit_test = FL_Widget_HitTest_Opaque,
    .on_pointer_event = FL_Scrollable_OnPointerEvent,
};

FL_Widget *FL_Scrollable(const FL_ScrollableProps *props) {
  return FL_Widget_Create(&FL_ScrollableClass, props->key, props);
}

static FL_Vec2 FL_Intersect(FL_f32 begin0, FL_f32 end0, FL_f32 begin1,
                            FL_f32 end1) {
  FL_ASSERT(begin0 <= end0 && begin1 <= end1);

  FL_Vec2 result = (FL_Vec2){0, 0};
  if (FL_Contains(begin1, begin0, end0)) {
    result.x = begin1;
    result.y = FL_Min(end0, end1);
  } else if (FL_Contains(end1, begin0, end0)) {
    result.x = FL_Max(begin0, begin1);
    result.y = end1;
  } else if (FL_Contains(begin0, begin1, end1)) {
    result.x = begin0;
    result.y = FL_Min(end0, end1);
  }
  return result;
}

static FL_i32 GetMinChildIndex(FL_f32 item_extent, FL_f32 scroll_offset) {
  if (item_extent <= 0.0f) {
    return 0;
  }
  FL_f32 actual = scroll_offset / item_extent;
  FL_f32 round = FL_Round(actual);
  if (FL_Abs(actual * item_extent - round * item_extent) <
      FL_PRECISION_ERROR_TOLERANCE) {
    return (FL_i32)round;
  }

  return (FL_i32)FL_Floor(actual);
}

static FL_i32 GetMaxChildIndex(FL_f32 item_extent, FL_f32 scroll_offset) {
  if (item_extent <= 0.0f) {
    return 0;
  }

  FL_f32 actual = scroll_offset / item_extent - 1;
  FL_f32 round = FL_Round(actual);
  if (FL_Abs(actual * item_extent - round * item_extent) <
      FL_PRECISION_ERROR_TOLERANCE) {
    return (FL_i32)FL_Max(0, round);
  }

  return (FL_i32)FL_Max(0, FL_Ceil(actual));
}

static void CalcItemCount(FL_f32 item_extent, FL_f32 scroll_offset,
                          FL_f32 remaining_extent, FL_i32 *first_index,
                          FL_i32 *target_last_index) {
  *first_index = GetMinChildIndex(item_extent, scroll_offset);
  FL_ASSERT(*first_index >= 0);

  FL_f32 target_end_scroll_offset = scroll_offset + remaining_extent;
  if (FL_IsFinite(target_end_scroll_offset)) {
    *target_last_index =
        GetMaxChildIndex(item_extent, target_end_scroll_offset);
  } else {
    *target_last_index = INT32_MAX;
  }
}

static void FL_SliverFixedExtentList_Layout(FL_Widget *widget,
                                            FL_BoxConstraints _constraints) {
  (void)_constraints;
  FL_SliverContext *sliver =
      FL_Widget_GetContext(widget, FL_SliverContext_ID, FL_SliverContext);
  FL_SliverConstraints constraints = sliver->constraints;
  FL_SliverFixedExtentListProps *props =
      FL_Widget_GetProps(widget, FL_SliverFixedExtentListProps);

  FL_f32 scroll_offset = constraints.scroll_offset + constraints.cache_origin;
  FL_ASSERT(scroll_offset >= 0.0f);
  FL_f32 remaining_extent = constraints.remaining_cache_extent;
  FL_ASSERT(remaining_extent >= 0.0f);

  FL_f32 item_extent = props->item_extent;
  FL_i32 first_index;
  FL_i32 target_last_index;
  CalcItemCount(item_extent, scroll_offset, remaining_extent, &first_index,
                &target_last_index);

  FL_i32 child_index = first_index;
  for (FL_i32 i = first_index; i <= target_last_index; i++) {
    if (i >= props->item_count) {
      break;
    }
    FL_Widget *child = props->item_builder.build(props->item_builder.ptr, i);
    if (!child) {
      break;
    }
    FL_Widget_Mount(widget, child);

    FL_BoxConstraints child_constraints =
        FL_BoxConstraints_FromSliverConstraints(constraints, item_extent,
                                                item_extent);
    FL_Widget_Layout(child, child_constraints);
    FL_f32 layout_offset = (FL_f32)child_index * item_extent;
    child->offset = (FL_Vec2){0, layout_offset - scroll_offset};

    child_index += 1;
  }

  FL_f32 leading_scroll_offset = scroll_offset;
  FL_f32 trailing_scroll_offset =
      scroll_offset +
      (FL_f32)(target_last_index - first_index + 1) * item_extent;

  FL_i32 item_count = props->item_count;
  FL_f32 scroll_extent = (FL_f32)item_count * item_extent;
  FL_f32 paint_extent = FL_SliverConstraints_CalcPaintOffset(
      constraints, leading_scroll_offset, trailing_scroll_offset);
  FL_f32 cache_extent = FL_SliverConstraints_CalcCacheOffset(
      constraints, leading_scroll_offset, trailing_scroll_offset);

  widget->size = (FL_Vec2){constraints.cross_axis_extent, paint_extent};

  FL_f32 target_end_scroll_offset_for_paint =
      constraints.scroll_offset + constraints.remaining_paint_extent;
  bool has_target_last_index_for_paint =
      FL_IsFinite(target_end_scroll_offset_for_paint);
  FL_i32 target_last_index_for_paint =
      has_target_last_index_for_paint
          ? GetMaxChildIndex(item_extent, target_end_scroll_offset_for_paint)
          : 0;

  sliver->geometry = (FL_SliverGeometry){
      .scroll_extent = scroll_extent,
      .paint_extent = paint_extent,
      .cache_extent = cache_extent,
      .layout_extent = paint_extent,
      .hit_test_extent = paint_extent,
      .max_paint_extent = scroll_extent,
      .has_visual_overflow = constraints.scroll_offset > 0 ||
                             (has_target_last_index_for_paint &&
                              child_index >= target_last_index_for_paint),
  };
}

static FL_Rect FL_Vec2_Intersect(FL_Vec2 begin0, FL_Vec2 end0, FL_Vec2 begin1,
                                 FL_Vec2 end1) {
  FL_Vec2 x_axis = FL_Intersect(begin0.x, end0.x, begin1.x, end1.x);
  FL_Vec2 y_axis = FL_Intersect(begin0.y, end0.y, begin1.y, end1.y);
  FL_Rect result = {
      x_axis.x,
      x_axis.y,
      y_axis.x,
      y_axis.y,
  };
  return result;
}

static void FL_SliverFixedExtentList_Paint(FL_Widget *widget,
                                           FL_PaintingContext *context,
                                           FL_Vec2 offset) {
  for (FL_Widget *child = widget->first; child; child = child->next) {
    FL_Rect intersection =
        FL_Vec2_Intersect(FL_Vec2_Zero(), widget->size, child->offset,
                          FL_Vec2_Add(child->offset, child->size));
    if (FL_Rect_GetArea(intersection) > 0) {
      FL_Widget_Paint(child, context, FL_Vec2_Add(offset, child->offset));
    }
  }
}

static FL_WidgetClass FL_SliverFixedExtentListClass = {
    .name = "SliverFixedExtentList",
    .props_size = FL_SIZE_OF(FL_SliverFixedExtentListProps),
    .layout = FL_SliverFixedExtentList_Layout,
    .paint = FL_SliverFixedExtentList_Paint,
};

FL_Widget *FL_SliverFixedExtentList(
    const FL_SliverFixedExtentListProps *props) {
  return FL_Widget_Create(&FL_SliverFixedExtentListClass, props->key, props);
}

FL_Widget *FL_ListView(const FL_ListViewProps *props) {
  return FL_Scrollable(&(FL_ScrollableProps){
      // .axis_direction = FL_AxisDirection_Down,
      // .cross_axis_direction = FL_AxisDirection_Right,
      .scroll = props->scroll,
      .slivers = FL_WidgetList_Make((FL_Widget *[]){
          FL_SliverFixedExtentList(&(FL_SliverFixedExtentListProps){
              .item_extent = props->item_extent,
              .item_count = props->item_count,
              .item_builder = props->item_builder,
          }),
          0}),
  });
}

void FL_Viewport_Init(void) {
  FL_SliverContext_ID = FL_Context_Register();
  FL_ScrollNotification_ID = FL_Notification_Register();
}

//////////////////////////////////////////////////////////////////////////////
//
//     IMPLEMENTATION SECTION
//

#ifdef STB_RECT_PACK_IMPLEMENTATION
#ifndef STBRP_SORT
#include <stdlib.h>
#define STBRP_SORT qsort
#endif

#ifndef STBRP_ASSERT
#include <assert.h>
#define STBRP_ASSERT assert
#endif

#ifdef _MSC_VER
#define STBRP__NOTUSED(v)  (void)(v)
#define STBRP__CDECL       __cdecl
#else
#define STBRP__NOTUSED(v)  (void)sizeof(v)
#define STBRP__CDECL
#endif

enum
{
   STBRP__INIT_skyline = 1
};

STBRP_DEF void stbrp_setup_heuristic(stbrp_context *context, int heuristic)
{
   switch (context->init_mode) {
      case STBRP__INIT_skyline:
         STBRP_ASSERT(heuristic == STBRP_HEURISTIC_Skyline_BL_sortHeight || heuristic == STBRP_HEURISTIC_Skyline_BF_sortHeight);
         context->heuristic = heuristic;
         break;
      default:
         STBRP_ASSERT(0);
   }
}

STBRP_DEF void stbrp_setup_allow_out_of_mem(stbrp_context *context, int allow_out_of_mem)
{
   if (allow_out_of_mem)
      // if it's ok to run out of memory, then don't bother aligning them;
      // this gives better packing, but may fail due to OOM (even though
      // the rectangles easily fit). @TODO a smarter approach would be to only
      // quantize once we've hit OOM, then we could get rid of this parameter.
      context->align = 1;
   else {
      // if it's not ok to run out of memory, then quantize the widths
      // so that num_nodes is always enough nodes.
      //
      // I.e. num_nodes * align >= width
      //                  align >= width / num_nodes
      //                  align = ceil(width/num_nodes)

      context->align = (context->width + context->num_nodes-1) / context->num_nodes;
   }
}

STBRP_DEF void stbrp_init_target(stbrp_context *context, int width, int height, stbrp_node *nodes, int num_nodes)
{
   int i;

   for (i=0; i < num_nodes-1; ++i)
      nodes[i].next = &nodes[i+1];
   nodes[i].next = NULL;
   context->init_mode = STBRP__INIT_skyline;
   context->heuristic = STBRP_HEURISTIC_Skyline_default;
   context->free_head = &nodes[0];
   context->active_head = &context->extra[0];
   context->width = width;
   context->height = height;
   context->num_nodes = num_nodes;
   stbrp_setup_allow_out_of_mem(context, 0);

   // node 0 is the full width, node 1 is the sentinel (lets us not store width explicitly)
   context->extra[0].x = 0;
   context->extra[0].y = 0;
   context->extra[0].next = &context->extra[1];
   context->extra[1].x = (stbrp_coord) width;
   context->extra[1].y = (1<<30);
   context->extra[1].next = NULL;
}

// find minimum y position if it starts at x1
static int stbrp__skyline_find_min_y(stbrp_context *c, stbrp_node *first, int x0, int width, int *pwaste)
{
   stbrp_node *node = first;
   int x1 = x0 + width;
   int min_y, visited_width, waste_area;

   STBRP__NOTUSED(c);

   STBRP_ASSERT(first->x <= x0);

   #if 0
   // skip in case we're past the node
   while (node->next->x <= x0)
      ++node;
   #else
   STBRP_ASSERT(node->next->x > x0); // we ended up handling this in the caller for efficiency
   #endif

   STBRP_ASSERT(node->x <= x0);

   min_y = 0;
   waste_area = 0;
   visited_width = 0;
   while (node->x < x1) {
      if (node->y > min_y) {
         // raise min_y higher.
         // we've accounted for all waste up to min_y,
         // but we'll now add more waste for everything we've visted
         waste_area += visited_width * (node->y - min_y);
         min_y = node->y;
         // the first time through, visited_width might be reduced
         if (node->x < x0)
            visited_width += node->next->x - x0;
         else
            visited_width += node->next->x - node->x;
      } else {
         // add waste area
         int under_width = node->next->x - node->x;
         if (under_width + visited_width > width)
            under_width = width - visited_width;
         waste_area += under_width * (min_y - node->y);
         visited_width += under_width;
      }
      node = node->next;
   }

   *pwaste = waste_area;
   return min_y;
}

typedef struct
{
   int x,y;
   stbrp_node **prev_link;
} stbrp__findresult;

static stbrp__findresult stbrp__skyline_find_best_pos(stbrp_context *c, int width, int height)
{
   int best_waste = (1<<30), best_x, best_y = (1 << 30);
   stbrp__findresult fr;
   stbrp_node **prev, *node, *tail, **best = NULL;

   // align to multiple of c->align
   width = (width + c->align - 1);
   width -= width % c->align;
   STBRP_ASSERT(width % c->align == 0);

   // if it can't possibly fit, bail immediately
   if (width > c->width || height > c->height) {
      fr.prev_link = NULL;
      fr.x = fr.y = 0;
      return fr;
   }

   node = c->active_head;
   prev = &c->active_head;
   while (node->x + width <= c->width) {
      int y,waste;
      y = stbrp__skyline_find_min_y(c, node, node->x, width, &waste);
      if (c->heuristic == STBRP_HEURISTIC_Skyline_BL_sortHeight) { // actually just want to test BL
         // bottom left
         if (y < best_y) {
            best_y = y;
            best = prev;
         }
      } else {
         // best-fit
         if (y + height <= c->height) {
            // can only use it if it first vertically
            if (y < best_y || (y == best_y && waste < best_waste)) {
               best_y = y;
               best_waste = waste;
               best = prev;
            }
         }
      }
      prev = &node->next;
      node = node->next;
   }

   best_x = (best == NULL) ? 0 : (*best)->x;

   // if doing best-fit (BF), we also have to try aligning right edge to each node position
   //
   // e.g, if fitting
   //
   //     ____________________
   //    |____________________|
   //
   //            into
   //
   //   |                         |
   //   |             ____________|
   //   |____________|
   //
   // then right-aligned reduces waste, but bottom-left BL is always chooses left-aligned
   //
   // This makes BF take about 2x the time

   if (c->heuristic == STBRP_HEURISTIC_Skyline_BF_sortHeight) {
      tail = c->active_head;
      node = c->active_head;
      prev = &c->active_head;
      // find first node that's admissible
      while (tail->x < width)
         tail = tail->next;
      while (tail) {
         int xpos = tail->x - width;
         int y,waste;
         STBRP_ASSERT(xpos >= 0);
         // find the left position that matches this
         while (node->next->x <= xpos) {
            prev = &node->next;
            node = node->next;
         }
         STBRP_ASSERT(node->next->x > xpos && node->x <= xpos);
         y = stbrp__skyline_find_min_y(c, node, xpos, width, &waste);
         if (y + height <= c->height) {
            if (y <= best_y) {
               if (y < best_y || waste < best_waste || (waste==best_waste && xpos < best_x)) {
                  best_x = xpos;
                  STBRP_ASSERT(y <= best_y);
                  best_y = y;
                  best_waste = waste;
                  best = prev;
               }
            }
         }
         tail = tail->next;
      }
   }

   fr.prev_link = best;
   fr.x = best_x;
   fr.y = best_y;
   return fr;
}

static stbrp__findresult stbrp__skyline_pack_rectangle(stbrp_context *context, int width, int height)
{
   // find best position according to heuristic
   stbrp__findresult res = stbrp__skyline_find_best_pos(context, width, height);
   stbrp_node *node, *cur;

   // bail if:
   //    1. it failed
   //    2. the best node doesn't fit (we don't always check this)
   //    3. we're out of memory
   if (res.prev_link == NULL || res.y + height > context->height || context->free_head == NULL) {
      res.prev_link = NULL;
      return res;
   }

   // on success, create new node
   node = context->free_head;
   node->x = (stbrp_coord) res.x;
   node->y = (stbrp_coord) (res.y + height);

   context->free_head = node->next;

   // insert the new node into the right starting point, and
   // let 'cur' point to the remaining nodes needing to be
   // stiched back in

   cur = *res.prev_link;
   if (cur->x < res.x) {
      // preserve the existing one, so start testing with the next one
      stbrp_node *next = cur->next;
      cur->next = node;
      cur = next;
   } else {
      *res.prev_link = node;
   }

   // from here, traverse cur and free the nodes, until we get to one
   // that shouldn't be freed
   while (cur->next && cur->next->x <= res.x + width) {
      stbrp_node *next = cur->next;
      // move the current node to the free list
      cur->next = context->free_head;
      context->free_head = cur;
      cur = next;
   }

   // stitch the list back in
   node->next = cur;

   if (cur->x < res.x + width)
      cur->x = (stbrp_coord) (res.x + width);

#ifdef _DEBUG
   cur = context->active_head;
   while (cur->x < context->width) {
      STBRP_ASSERT(cur->x < cur->next->x);
      cur = cur->next;
   }
   STBRP_ASSERT(cur->next == NULL);

   {
      int count=0;
      cur = context->active_head;
      while (cur) {
         cur = cur->next;
         ++count;
      }
      cur = context->free_head;
      while (cur) {
         cur = cur->next;
         ++count;
      }
      STBRP_ASSERT(count == context->num_nodes+2);
   }
#endif

   return res;
}

static int STBRP__CDECL rect_height_compare(const void *a, const void *b)
{
   const stbrp_rect *p = (const stbrp_rect *) a;
   const stbrp_rect *q = (const stbrp_rect *) b;
   if (p->h > q->h)
      return -1;
   if (p->h < q->h)
      return  1;
   return (p->w > q->w) ? -1 : (p->w < q->w);
}

static int STBRP__CDECL rect_original_order(const void *a, const void *b)
{
   const stbrp_rect *p = (const stbrp_rect *) a;
   const stbrp_rect *q = (const stbrp_rect *) b;
   return (p->was_packed < q->was_packed) ? -1 : (p->was_packed > q->was_packed);
}

STBRP_DEF int stbrp_pack_rects(stbrp_context *context, stbrp_rect *rects, int num_rects)
{
   int i, all_rects_packed = 1;

   // we use the 'was_packed' field internally to allow sorting/unsorting
   for (i=0; i < num_rects; ++i) {
      rects[i].was_packed = i;
   }

   // sort according to heuristic
   STBRP_SORT(rects, num_rects, sizeof(rects[0]), rect_height_compare);

   for (i=0; i < num_rects; ++i) {
      if (rects[i].w == 0 || rects[i].h == 0) {
         rects[i].x = rects[i].y = 0;  // empty rect needs no space
      } else {
         stbrp__findresult fr = stbrp__skyline_pack_rectangle(context, rects[i].w, rects[i].h);
         if (fr.prev_link) {
            rects[i].x = (stbrp_coord) fr.x;
            rects[i].y = (stbrp_coord) fr.y;
         } else {
            rects[i].x = rects[i].y = STBRP__MAXVAL;
         }
      }
   }

   // unsort
   STBRP_SORT(rects, num_rects, sizeof(rects[0]), rect_original_order);

   // set was_packed flags and all_rects_packed status
   for (i=0; i < num_rects; ++i) {
      rects[i].was_packed = !(rects[i].x == STBRP__MAXVAL && rects[i].y == STBRP__MAXVAL);
      if (!rects[i].was_packed)
         all_rects_packed = 0;
   }

   // return the all_rects_packed status
   return all_rects_packed;
}
#endif


#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wimplicit-int-float-conversion"
#endif


///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
////
////   IMPLEMENTATION
////
////

#ifdef STB_TRUETYPE_IMPLEMENTATION

#ifndef STBTT_MAX_OVERSAMPLE
#define STBTT_MAX_OVERSAMPLE   8
#endif

#if STBTT_MAX_OVERSAMPLE > 255
#error "STBTT_MAX_OVERSAMPLE cannot be > 255"
#endif

typedef int stbtt__test_oversample_pow2[(STBTT_MAX_OVERSAMPLE & (STBTT_MAX_OVERSAMPLE-1)) == 0 ? 1 : -1];

#ifndef STBTT_RASTERIZER_VERSION
#define STBTT_RASTERIZER_VERSION 2
#endif

#ifdef _MSC_VER
#define STBTT__NOTUSED(v)  (void)(v)
#else
#define STBTT__NOTUSED(v)  (void)sizeof(v)
#endif

//////////////////////////////////////////////////////////////////////////
//
// stbtt__buf helpers to parse data from file
//

static stbtt_uint8 stbtt__buf_get8(stbtt__buf *b)
{
   if (b->cursor >= b->size)
      return 0;
   return b->data[b->cursor++];
}

static stbtt_uint8 stbtt__buf_peek8(stbtt__buf *b)
{
   if (b->cursor >= b->size)
      return 0;
   return b->data[b->cursor];
}

static void stbtt__buf_seek(stbtt__buf *b, int o)
{
   STBTT_assert(!(o > b->size || o < 0));
   b->cursor = (o > b->size || o < 0) ? b->size : o;
}

static void stbtt__buf_skip(stbtt__buf *b, int o)
{
   stbtt__buf_seek(b, b->cursor + o);
}

static stbtt_uint32 stbtt__buf_get(stbtt__buf *b, int n)
{
   stbtt_uint32 v = 0;
   int i;
   STBTT_assert(n >= 1 && n <= 4);
   for (i = 0; i < n; i++)
      v = (v << 8) | stbtt__buf_get8(b);
   return v;
}

static stbtt__buf stbtt__new_buf(const void *p, size_t size)
{
   stbtt__buf r;
   STBTT_assert(size < 0x40000000);
   r.data = (stbtt_uint8*) p;
   r.size = (int) size;
   r.cursor = 0;
   return r;
}

#define stbtt__buf_get16(b)  stbtt__buf_get((b), 2)
#define stbtt__buf_get32(b)  stbtt__buf_get((b), 4)

static stbtt__buf stbtt__buf_range(const stbtt__buf *b, int o, int s)
{
   stbtt__buf r = stbtt__new_buf(NULL, 0);
   if (o < 0 || s < 0 || o > b->size || s > b->size - o) return r;
   r.data = b->data + o;
   r.size = s;
   return r;
}

static stbtt__buf stbtt__cff_get_index(stbtt__buf *b)
{
   int count, start, offsize;
   start = b->cursor;
   count = stbtt__buf_get16(b);
   if (count) {
      offsize = stbtt__buf_get8(b);
      STBTT_assert(offsize >= 1 && offsize <= 4);
      stbtt__buf_skip(b, offsize * count);
      stbtt__buf_skip(b, stbtt__buf_get(b, offsize) - 1);
   }
   return stbtt__buf_range(b, start, b->cursor - start);
}

static stbtt_uint32 stbtt__cff_int(stbtt__buf *b)
{
   int b0 = stbtt__buf_get8(b);
   if (b0 >= 32 && b0 <= 246)       return b0 - 139;
   else if (b0 >= 247 && b0 <= 250) return (b0 - 247)*256 + stbtt__buf_get8(b) + 108;
   else if (b0 >= 251 && b0 <= 254) return -(b0 - 251)*256 - stbtt__buf_get8(b) - 108;
   else if (b0 == 28)               return stbtt__buf_get16(b);
   else if (b0 == 29)               return stbtt__buf_get32(b);
   STBTT_assert(0);
   return 0;
}

static void stbtt__cff_skip_operand(stbtt__buf *b) {
   int v, b0 = stbtt__buf_peek8(b);
   STBTT_assert(b0 >= 28);
   if (b0 == 30) {
      stbtt__buf_skip(b, 1);
      while (b->cursor < b->size) {
         v = stbtt__buf_get8(b);
         if ((v & 0xF) == 0xF || (v >> 4) == 0xF)
            break;
      }
   } else {
      stbtt__cff_int(b);
   }
}

static stbtt__buf stbtt__dict_get(stbtt__buf *b, int key)
{
   stbtt__buf_seek(b, 0);
   while (b->cursor < b->size) {
      int start = b->cursor, end, op;
      while (stbtt__buf_peek8(b) >= 28)
         stbtt__cff_skip_operand(b);
      end = b->cursor;
      op = stbtt__buf_get8(b);
      if (op == 12)  op = stbtt__buf_get8(b) | 0x100;
      if (op == key) return stbtt__buf_range(b, start, end-start);
   }
   return stbtt__buf_range(b, 0, 0);
}

static void stbtt__dict_get_ints(stbtt__buf *b, int key, int outcount, stbtt_uint32 *out)
{
   int i;
   stbtt__buf operands = stbtt__dict_get(b, key);
   for (i = 0; i < outcount && operands.cursor < operands.size; i++)
      out[i] = stbtt__cff_int(&operands);
}

static int stbtt__cff_index_count(stbtt__buf *b)
{
   stbtt__buf_seek(b, 0);
   return stbtt__buf_get16(b);
}

static stbtt__buf stbtt__cff_index_get(stbtt__buf b, int i)
{
   int count, offsize, start, end;
   stbtt__buf_seek(&b, 0);
   count = stbtt__buf_get16(&b);
   offsize = stbtt__buf_get8(&b);
   STBTT_assert(i >= 0 && i < count);
   STBTT_assert(offsize >= 1 && offsize <= 4);
   stbtt__buf_skip(&b, i*offsize);
   start = stbtt__buf_get(&b, offsize);
   end = stbtt__buf_get(&b, offsize);
   return stbtt__buf_range(&b, 2+(count+1)*offsize+start, end - start);
}

//////////////////////////////////////////////////////////////////////////
//
// accessors to parse data from file
//

// on platforms that don't allow misaligned reads, if we want to allow
// truetype fonts that aren't padded to alignment, define ALLOW_UNALIGNED_TRUETYPE

#define ttBYTE(p)     (* (stbtt_uint8 *) (p))
#define ttCHAR(p)     (* (stbtt_int8 *) (p))
#define ttFixed(p)    ttLONG(p)

static stbtt_uint16 ttUSHORT(stbtt_uint8 *p) { return p[0]*256 + p[1]; }
static stbtt_int16 ttSHORT(stbtt_uint8 *p)   { return p[0]*256 + p[1]; }
static stbtt_uint32 ttULONG(stbtt_uint8 *p)  { return (p[0]<<24) + (p[1]<<16) + (p[2]<<8) + p[3]; }
static stbtt_int32 ttLONG(stbtt_uint8 *p)    { return (p[0]<<24) + (p[1]<<16) + (p[2]<<8) + p[3]; }

#define stbtt_tag4(p,c0,c1,c2,c3) ((p)[0] == (c0) && (p)[1] == (c1) && (p)[2] == (c2) && (p)[3] == (c3))
#define stbtt_tag(p,str)           stbtt_tag4(p,str[0],str[1],str[2],str[3])

static int stbtt__isfont(stbtt_uint8 *font)
{
   // check the version number
   if (stbtt_tag4(font, '1',0,0,0))  return 1; // TrueType 1
   if (stbtt_tag(font, "typ1"))   return 1; // TrueType with type 1 font -- we don't support this!
   if (stbtt_tag(font, "OTTO"))   return 1; // OpenType with CFF
   if (stbtt_tag4(font, 0,1,0,0)) return 1; // OpenType 1.0
   if (stbtt_tag(font, "true"))   return 1; // Apple specification for TrueType fonts
   return 0;
}

// @OPTIMIZE: binary search
static stbtt_uint32 stbtt__find_table(stbtt_uint8 *data, stbtt_uint32 fontstart, const char *tag)
{
   stbtt_int32 num_tables = ttUSHORT(data+fontstart+4);
   stbtt_uint32 tabledir = fontstart + 12;
   stbtt_int32 i;
   for (i=0; i < num_tables; ++i) {
      stbtt_uint32 loc = tabledir + 16*i;
      if (stbtt_tag(data+loc+0, tag))
         return ttULONG(data+loc+8);
   }
   return 0;
}

static int stbtt_GetFontOffsetForIndex_internal(unsigned char *font_collection, int index)
{
   // if it's just a font, there's only one valid index
   if (stbtt__isfont(font_collection))
      return index == 0 ? 0 : -1;

   // check if it's a TTC
   if (stbtt_tag(font_collection, "ttcf")) {
      // version 1?
      if (ttULONG(font_collection+4) == 0x00010000 || ttULONG(font_collection+4) == 0x00020000) {
         stbtt_int32 n = ttLONG(font_collection+8);
         if (index >= n)
            return -1;
         return ttULONG(font_collection+12+index*4);
      }
   }
   return -1;
}

static int stbtt_GetNumberOfFonts_internal(unsigned char *font_collection)
{
   // if it's just a font, there's only one valid font
   if (stbtt__isfont(font_collection))
      return 1;

   // check if it's a TTC
   if (stbtt_tag(font_collection, "ttcf")) {
      // version 1?
      if (ttULONG(font_collection+4) == 0x00010000 || ttULONG(font_collection+4) == 0x00020000) {
         return ttLONG(font_collection+8);
      }
   }
   return 0;
}

static stbtt__buf stbtt__get_subrs(stbtt__buf cff, stbtt__buf fontdict)
{
   stbtt_uint32 subrsoff = 0, private_loc[2] = { 0, 0 };
   stbtt__buf pdict;
   stbtt__dict_get_ints(&fontdict, 18, 2, private_loc);
   if (!private_loc[1] || !private_loc[0]) return stbtt__new_buf(NULL, 0);
   pdict = stbtt__buf_range(&cff, private_loc[1], private_loc[0]);
   stbtt__dict_get_ints(&pdict, 19, 1, &subrsoff);
   if (!subrsoff) return stbtt__new_buf(NULL, 0);
   stbtt__buf_seek(&cff, private_loc[1]+subrsoff);
   return stbtt__cff_get_index(&cff);
}

// since most people won't use this, find this table the first time it's needed
static int stbtt__get_svg(stbtt_fontinfo *info)
{
   stbtt_uint32 t;
   if (info->svg < 0) {
      t = stbtt__find_table(info->data, info->fontstart, "SVG ");
      if (t) {
         stbtt_uint32 offset = ttULONG(info->data + t + 2);
         info->svg = t + offset;
      } else {
         info->svg = 0;
      }
   }
   return info->svg;
}

static int stbtt_InitFont_internal(stbtt_fontinfo *info, unsigned char *data, int fontstart)
{
   stbtt_uint32 cmap, t;
   stbtt_int32 i,numTables;

   info->data = data;
   info->fontstart = fontstart;
   info->cff = stbtt__new_buf(NULL, 0);

   cmap = stbtt__find_table(data, fontstart, "cmap");       // required
   info->loca = stbtt__find_table(data, fontstart, "loca"); // required
   info->head = stbtt__find_table(data, fontstart, "head"); // required
   info->glyf = stbtt__find_table(data, fontstart, "glyf"); // required
   info->hhea = stbtt__find_table(data, fontstart, "hhea"); // required
   info->hmtx = stbtt__find_table(data, fontstart, "hmtx"); // required
   info->kern = stbtt__find_table(data, fontstart, "kern"); // not required
   info->gpos = stbtt__find_table(data, fontstart, "GPOS"); // not required

   if (!cmap || !info->head || !info->hhea || !info->hmtx)
      return 0;
   if (info->glyf) {
      // required for truetype
      if (!info->loca) return 0;
   } else {
      // initialization for CFF / Type2 fonts (OTF)
      stbtt__buf b, topdict, topdictidx;
      stbtt_uint32 cstype = 2, charstrings = 0, fdarrayoff = 0, fdselectoff = 0;
      stbtt_uint32 cff;

      cff = stbtt__find_table(data, fontstart, "CFF ");
      if (!cff) return 0;

      info->fontdicts = stbtt__new_buf(NULL, 0);
      info->fdselect = stbtt__new_buf(NULL, 0);

      // @TODO this should use size from table (not 512MB)
      info->cff = stbtt__new_buf(data+cff, 512*1024*1024);
      b = info->cff;

      // read the header
      stbtt__buf_skip(&b, 2);
      stbtt__buf_seek(&b, stbtt__buf_get8(&b)); // hdrsize

      // @TODO the name INDEX could list multiple fonts,
      // but we just use the first one.
      stbtt__cff_get_index(&b);  // name INDEX
      topdictidx = stbtt__cff_get_index(&b);
      topdict = stbtt__cff_index_get(topdictidx, 0);
      stbtt__cff_get_index(&b);  // string INDEX
      info->gsubrs = stbtt__cff_get_index(&b);

      stbtt__dict_get_ints(&topdict, 17, 1, &charstrings);
      stbtt__dict_get_ints(&topdict, 0x100 | 6, 1, &cstype);
      stbtt__dict_get_ints(&topdict, 0x100 | 36, 1, &fdarrayoff);
      stbtt__dict_get_ints(&topdict, 0x100 | 37, 1, &fdselectoff);
      info->subrs = stbtt__get_subrs(b, topdict);

      // we only support Type 2 charstrings
      if (cstype != 2) return 0;
      if (charstrings == 0) return 0;

      if (fdarrayoff) {
         // looks like a CID font
         if (!fdselectoff) return 0;
         stbtt__buf_seek(&b, fdarrayoff);
         info->fontdicts = stbtt__cff_get_index(&b);
         info->fdselect = stbtt__buf_range(&b, fdselectoff, b.size-fdselectoff);
      }

      stbtt__buf_seek(&b, charstrings);
      info->charstrings = stbtt__cff_get_index(&b);
   }

   t = stbtt__find_table(data, fontstart, "maxp");
   if (t)
      info->numGlyphs = ttUSHORT(data+t+4);
   else
      info->numGlyphs = 0xffff;

   info->svg = -1;

   // find a cmap encoding table we understand *now* to avoid searching
   // later. (todo: could make this installable)
   // the same regardless of glyph.
   numTables = ttUSHORT(data + cmap + 2);
   info->index_map = 0;
   for (i=0; i < numTables; ++i) {
      stbtt_uint32 encoding_record = cmap + 4 + 8 * i;
      // find an encoding we understand:
      switch(ttUSHORT(data+encoding_record)) {
         case STBTT_PLATFORM_ID_MICROSOFT:
            switch (ttUSHORT(data+encoding_record+2)) {
               case STBTT_MS_EID_UNICODE_BMP:
               case STBTT_MS_EID_UNICODE_FULL:
                  // MS/Unicode
                  info->index_map = cmap + ttULONG(data+encoding_record+4);
                  break;
            }
            break;
        case STBTT_PLATFORM_ID_UNICODE:
            // Mac/iOS has these
            // all the encodingIDs are unicode, so we don't bother to check it
            info->index_map = cmap + ttULONG(data+encoding_record+4);
            break;
      }
   }
   if (info->index_map == 0)
      return 0;

   info->indexToLocFormat = ttUSHORT(data+info->head + 50);
   return 1;
}

STBTT_DEF int stbtt_FindGlyphIndex(const stbtt_fontinfo *info, int unicode_codepoint)
{
   stbtt_uint8 *data = info->data;
   stbtt_uint32 index_map = info->index_map;

   stbtt_uint16 format = ttUSHORT(data + index_map + 0);
   if (format == 0) { // apple byte encoding
      stbtt_int32 bytes = ttUSHORT(data + index_map + 2);
      if (unicode_codepoint < bytes-6)
         return ttBYTE(data + index_map + 6 + unicode_codepoint);
      return 0;
   } else if (format == 6) {
      stbtt_uint32 first = ttUSHORT(data + index_map + 6);
      stbtt_uint32 count = ttUSHORT(data + index_map + 8);
      if ((stbtt_uint32) unicode_codepoint >= first && (stbtt_uint32) unicode_codepoint < first+count)
         return ttUSHORT(data + index_map + 10 + (unicode_codepoint - first)*2);
      return 0;
   } else if (format == 2) {
      STBTT_assert(0); // @TODO: high-byte mapping for japanese/chinese/korean
      return 0;
   } else if (format == 4) { // standard mapping for windows fonts: binary search collection of ranges
      stbtt_uint16 segcount = ttUSHORT(data+index_map+6) >> 1;
      stbtt_uint16 searchRange = ttUSHORT(data+index_map+8) >> 1;
      stbtt_uint16 entrySelector = ttUSHORT(data+index_map+10);
      stbtt_uint16 rangeShift = ttUSHORT(data+index_map+12) >> 1;

      // do a binary search of the segments
      stbtt_uint32 endCount = index_map + 14;
      stbtt_uint32 search = endCount;

      if (unicode_codepoint > 0xffff)
         return 0;

      // they lie from endCount .. endCount + segCount
      // but searchRange is the nearest power of two, so...
      if (unicode_codepoint >= ttUSHORT(data + search + rangeShift*2))
         search += rangeShift*2;

      // now decrement to bias correctly to find smallest
      search -= 2;
      while (entrySelector) {
         stbtt_uint16 end;
         searchRange >>= 1;
         end = ttUSHORT(data + search + searchRange*2);
         if (unicode_codepoint > end)
            search += searchRange*2;
         --entrySelector;
      }
      search += 2;

      {
         stbtt_uint16 offset, start, last;
         stbtt_uint16 item = (stbtt_uint16) ((search - endCount) >> 1);

         start = ttUSHORT(data + index_map + 14 + segcount*2 + 2 + 2*item);
         last = ttUSHORT(data + endCount + 2*item);
         if (unicode_codepoint < start || unicode_codepoint > last)
            return 0;

         offset = ttUSHORT(data + index_map + 14 + segcount*6 + 2 + 2*item);
         if (offset == 0)
            return (stbtt_uint16) (unicode_codepoint + ttSHORT(data + index_map + 14 + segcount*4 + 2 + 2*item));

         return ttUSHORT(data + offset + (unicode_codepoint-start)*2 + index_map + 14 + segcount*6 + 2 + 2*item);
      }
   } else if (format == 12 || format == 13) {
      stbtt_uint32 ngroups = ttULONG(data+index_map+12);
      stbtt_int32 low,high;
      low = 0; high = (stbtt_int32)ngroups;
      // Binary search the right group.
      while (low < high) {
         stbtt_int32 mid = low + ((high-low) >> 1); // rounds down, so low <= mid < high
         stbtt_uint32 start_char = ttULONG(data+index_map+16+mid*12);
         stbtt_uint32 end_char = ttULONG(data+index_map+16+mid*12+4);
         if ((stbtt_uint32) unicode_codepoint < start_char)
            high = mid;
         else if ((stbtt_uint32) unicode_codepoint > end_char)
            low = mid+1;
         else {
            stbtt_uint32 start_glyph = ttULONG(data+index_map+16+mid*12+8);
            if (format == 12)
               return start_glyph + unicode_codepoint-start_char;
            else // format == 13
               return start_glyph;
         }
      }
      return 0; // not found
   }
   // @TODO
   STBTT_assert(0);
   return 0;
}

STBTT_DEF int stbtt_GetCodepointShape(const stbtt_fontinfo *info, int unicode_codepoint, stbtt_vertex **vertices)
{
   return stbtt_GetGlyphShape(info, stbtt_FindGlyphIndex(info, unicode_codepoint), vertices);
}

static void stbtt_setvertex(stbtt_vertex *v, stbtt_uint8 type, stbtt_int32 x, stbtt_int32 y, stbtt_int32 cx, stbtt_int32 cy)
{
   v->type = type;
   v->x = (stbtt_int16) x;
   v->y = (stbtt_int16) y;
   v->cx = (stbtt_int16) cx;
   v->cy = (stbtt_int16) cy;
}

static int stbtt__GetGlyfOffset(const stbtt_fontinfo *info, int glyph_index)
{
   int g1,g2;

   STBTT_assert(!info->cff.size);

   if (glyph_index >= info->numGlyphs) return -1; // glyph index out of range
   if (info->indexToLocFormat >= 2)    return -1; // unknown index->glyph map format

   if (info->indexToLocFormat == 0) {
      g1 = info->glyf + ttUSHORT(info->data + info->loca + glyph_index * 2) * 2;
      g2 = info->glyf + ttUSHORT(info->data + info->loca + glyph_index * 2 + 2) * 2;
   } else {
      g1 = info->glyf + ttULONG (info->data + info->loca + glyph_index * 4);
      g2 = info->glyf + ttULONG (info->data + info->loca + glyph_index * 4 + 4);
   }

   return g1==g2 ? -1 : g1; // if length is 0, return -1
}

static int stbtt__GetGlyphInfoT2(const stbtt_fontinfo *info, int glyph_index, int *x0, int *y0, int *x1, int *y1);

STBTT_DEF int stbtt_GetGlyphBox(const stbtt_fontinfo *info, int glyph_index, int *x0, int *y0, int *x1, int *y1)
{
   if (info->cff.size) {
      stbtt__GetGlyphInfoT2(info, glyph_index, x0, y0, x1, y1);
   } else {
      int g = stbtt__GetGlyfOffset(info, glyph_index);
      if (g < 0) return 0;

      if (x0) *x0 = ttSHORT(info->data + g + 2);
      if (y0) *y0 = ttSHORT(info->data + g + 4);
      if (x1) *x1 = ttSHORT(info->data + g + 6);
      if (y1) *y1 = ttSHORT(info->data + g + 8);
   }
   return 1;
}

STBTT_DEF int stbtt_GetCodepointBox(const stbtt_fontinfo *info, int codepoint, int *x0, int *y0, int *x1, int *y1)
{
   return stbtt_GetGlyphBox(info, stbtt_FindGlyphIndex(info,codepoint), x0,y0,x1,y1);
}

STBTT_DEF int stbtt_IsGlyphEmpty(const stbtt_fontinfo *info, int glyph_index)
{
   stbtt_int16 numberOfContours;
   int g;
   if (info->cff.size)
      return stbtt__GetGlyphInfoT2(info, glyph_index, NULL, NULL, NULL, NULL) == 0;
   g = stbtt__GetGlyfOffset(info, glyph_index);
   if (g < 0) return 1;
   numberOfContours = ttSHORT(info->data + g);
   return numberOfContours == 0;
}

static int stbtt__close_shape(stbtt_vertex *vertices, int num_vertices, int was_off, int start_off,
    stbtt_int32 sx, stbtt_int32 sy, stbtt_int32 scx, stbtt_int32 scy, stbtt_int32 cx, stbtt_int32 cy)
{
   if (start_off) {
      if (was_off)
         stbtt_setvertex(&vertices[num_vertices++], STBTT_vcurve, (cx+scx)>>1, (cy+scy)>>1, cx,cy);
      stbtt_setvertex(&vertices[num_vertices++], STBTT_vcurve, sx,sy,scx,scy);
   } else {
      if (was_off)
         stbtt_setvertex(&vertices[num_vertices++], STBTT_vcurve,sx,sy,cx,cy);
      else
         stbtt_setvertex(&vertices[num_vertices++], STBTT_vline,sx,sy,0,0);
   }
   return num_vertices;
}

static int stbtt__GetGlyphShapeTT(const stbtt_fontinfo *info, int glyph_index, stbtt_vertex **pvertices)
{
   stbtt_int16 numberOfContours;
   stbtt_uint8 *endPtsOfContours;
   stbtt_uint8 *data = info->data;
   stbtt_vertex *vertices=0;
   int num_vertices=0;
   int g = stbtt__GetGlyfOffset(info, glyph_index);

   *pvertices = NULL;

   if (g < 0) return 0;

   numberOfContours = ttSHORT(data + g);

   if (numberOfContours > 0) {
      stbtt_uint8 flags=0,flagcount;
      stbtt_int32 ins, i,j=0,m,n, next_move, was_off=0, off, start_off=0;
      stbtt_int32 x,y,cx,cy,sx,sy, scx,scy;
      stbtt_uint8 *points;
      endPtsOfContours = (data + g + 10);
      ins = ttUSHORT(data + g + 10 + numberOfContours * 2);
      points = data + g + 10 + numberOfContours * 2 + 2 + ins;

      n = 1+ttUSHORT(endPtsOfContours + numberOfContours*2-2);

      m = n + 2*numberOfContours;  // a loose bound on how many vertices we might need
      vertices = (stbtt_vertex *) STBTT_malloc(m * sizeof(vertices[0]), info->userdata);
      if (vertices == 0)
         return 0;

      next_move = 0;
      flagcount=0;

      // in first pass, we load uninterpreted data into the allocated array
      // above, shifted to the end of the array so we won't overwrite it when
      // we create our final data starting from the front

      off = m - n; // starting offset for uninterpreted data, regardless of how m ends up being calculated

      // first load flags

      for (i=0; i < n; ++i) {
         if (flagcount == 0) {
            flags = *points++;
            if (flags & 8)
               flagcount = *points++;
         } else
            --flagcount;
         vertices[off+i].type = flags;
      }

      // now load x coordinates
      x=0;
      for (i=0; i < n; ++i) {
         flags = vertices[off+i].type;
         if (flags & 2) {
            stbtt_int16 dx = *points++;
            x += (flags & 16) ? dx : -dx; // ???
         } else {
            if (!(flags & 16)) {
               x = x + (stbtt_int16) (points[0]*256 + points[1]);
               points += 2;
            }
         }
         vertices[off+i].x = (stbtt_int16) x;
      }

      // now load y coordinates
      y=0;
      for (i=0; i < n; ++i) {
         flags = vertices[off+i].type;
         if (flags & 4) {
            stbtt_int16 dy = *points++;
            y += (flags & 32) ? dy : -dy; // ???
         } else {
            if (!(flags & 32)) {
               y = y + (stbtt_int16) (points[0]*256 + points[1]);
               points += 2;
            }
         }
         vertices[off+i].y = (stbtt_int16) y;
      }

      // now convert them to our format
      num_vertices=0;
      sx = sy = cx = cy = scx = scy = 0;
      for (i=0; i < n; ++i) {
         flags = vertices[off+i].type;
         x     = (stbtt_int16) vertices[off+i].x;
         y     = (stbtt_int16) vertices[off+i].y;

         if (next_move == i) {
            if (i != 0)
               num_vertices = stbtt__close_shape(vertices, num_vertices, was_off, start_off, sx,sy,scx,scy,cx,cy);

            // now start the new one
            start_off = !(flags & 1);
            if (start_off) {
               // if we start off with an off-curve point, then when we need to find a point on the curve
               // where we can start, and we need to save some state for when we wraparound.
               scx = x;
               scy = y;
               if (!(vertices[off+i+1].type & 1)) {
                  // next point is also a curve point, so interpolate an on-point curve
                  sx = (x + (stbtt_int32) vertices[off+i+1].x) >> 1;
                  sy = (y + (stbtt_int32) vertices[off+i+1].y) >> 1;
               } else {
                  // otherwise just use the next point as our start point
                  sx = (stbtt_int32) vertices[off+i+1].x;
                  sy = (stbtt_int32) vertices[off+i+1].y;
                  ++i; // we're using point i+1 as the starting point, so skip it
               }
            } else {
               sx = x;
               sy = y;
            }
            stbtt_setvertex(&vertices[num_vertices++], STBTT_vmove,sx,sy,0,0);
            was_off = 0;
            next_move = 1 + ttUSHORT(endPtsOfContours+j*2);
            ++j;
         } else {
            if (!(flags & 1)) { // if it's a curve
               if (was_off) // two off-curve control points in a row means interpolate an on-curve midpoint
                  stbtt_setvertex(&vertices[num_vertices++], STBTT_vcurve, (cx+x)>>1, (cy+y)>>1, cx, cy);
               cx = x;
               cy = y;
               was_off = 1;
            } else {
               if (was_off)
                  stbtt_setvertex(&vertices[num_vertices++], STBTT_vcurve, x,y, cx, cy);
               else
                  stbtt_setvertex(&vertices[num_vertices++], STBTT_vline, x,y,0,0);
               was_off = 0;
            }
         }
      }
      num_vertices = stbtt__close_shape(vertices, num_vertices, was_off, start_off, sx,sy,scx,scy,cx,cy);
   } else if (numberOfContours < 0) {
      // Compound shapes.
      int more = 1;
      stbtt_uint8 *comp = data + g + 10;
      num_vertices = 0;
      vertices = 0;
      while (more) {
         stbtt_uint16 flags, gidx;
         int comp_num_verts = 0, i;
         stbtt_vertex *comp_verts = 0, *tmp = 0;
         float mtx[6] = {1,0,0,1,0,0}, m, n;

         flags = ttSHORT(comp); comp+=2;
         gidx = ttSHORT(comp); comp+=2;

         if (flags & 2) { // XY values
            if (flags & 1) { // shorts
               mtx[4] = ttSHORT(comp); comp+=2;
               mtx[5] = ttSHORT(comp); comp+=2;
            } else {
               mtx[4] = ttCHAR(comp); comp+=1;
               mtx[5] = ttCHAR(comp); comp+=1;
            }
         }
         else {
            // @TODO handle matching point
            STBTT_assert(0);
         }
         if (flags & (1<<3)) { // WE_HAVE_A_SCALE
            mtx[0] = mtx[3] = ttSHORT(comp)/16384.0f; comp+=2;
            mtx[1] = mtx[2] = 0;
         } else if (flags & (1<<6)) { // WE_HAVE_AN_X_AND_YSCALE
            mtx[0] = ttSHORT(comp)/16384.0f; comp+=2;
            mtx[1] = mtx[2] = 0;
            mtx[3] = ttSHORT(comp)/16384.0f; comp+=2;
         } else if (flags & (1<<7)) { // WE_HAVE_A_TWO_BY_TWO
            mtx[0] = ttSHORT(comp)/16384.0f; comp+=2;
            mtx[1] = ttSHORT(comp)/16384.0f; comp+=2;
            mtx[2] = ttSHORT(comp)/16384.0f; comp+=2;
            mtx[3] = ttSHORT(comp)/16384.0f; comp+=2;
         }

         // Find transformation scales.
         m = (float) STBTT_sqrt(mtx[0]*mtx[0] + mtx[1]*mtx[1]);
         n = (float) STBTT_sqrt(mtx[2]*mtx[2] + mtx[3]*mtx[3]);

         // Get indexed glyph.
         comp_num_verts = stbtt_GetGlyphShape(info, gidx, &comp_verts);
         if (comp_num_verts > 0) {
            // Transform vertices.
            for (i = 0; i < comp_num_verts; ++i) {
               stbtt_vertex* v = &comp_verts[i];
               stbtt_vertex_type x,y;
               x=v->x; y=v->y;
               v->x = (stbtt_vertex_type)(m * (mtx[0]*x + mtx[2]*y + mtx[4]));
               v->y = (stbtt_vertex_type)(n * (mtx[1]*x + mtx[3]*y + mtx[5]));
               x=v->cx; y=v->cy;
               v->cx = (stbtt_vertex_type)(m * (mtx[0]*x + mtx[2]*y + mtx[4]));
               v->cy = (stbtt_vertex_type)(n * (mtx[1]*x + mtx[3]*y + mtx[5]));
            }
            // Append vertices.
            tmp = (stbtt_vertex*)STBTT_malloc((num_vertices+comp_num_verts)*sizeof(stbtt_vertex), info->userdata);
            if (!tmp) {
               if (vertices) STBTT_free(vertices, info->userdata);
               if (comp_verts) STBTT_free(comp_verts, info->userdata);
               return 0;
            }
            if (num_vertices > 0 && vertices) STBTT_memcpy(tmp, vertices, num_vertices*sizeof(stbtt_vertex));
            STBTT_memcpy(tmp+num_vertices, comp_verts, comp_num_verts*sizeof(stbtt_vertex));
            if (vertices) STBTT_free(vertices, info->userdata);
            vertices = tmp;
            STBTT_free(comp_verts, info->userdata);
            num_vertices += comp_num_verts;
         }
         // More components ?
         more = flags & (1<<5);
      }
   } else {
      // numberOfCounters == 0, do nothing
   }

   *pvertices = vertices;
   return num_vertices;
}

typedef struct
{
   int bounds;
   int started;
   float first_x, first_y;
   float x, y;
   stbtt_int32 min_x, max_x, min_y, max_y;

   stbtt_vertex *pvertices;
   int num_vertices;
} stbtt__csctx;

#define STBTT__CSCTX_INIT(bounds) {bounds,0, 0,0, 0,0, 0,0,0,0, NULL, 0}

static void stbtt__track_vertex(stbtt__csctx *c, stbtt_int32 x, stbtt_int32 y)
{
   if (x > c->max_x || !c->started) c->max_x = x;
   if (y > c->max_y || !c->started) c->max_y = y;
   if (x < c->min_x || !c->started) c->min_x = x;
   if (y < c->min_y || !c->started) c->min_y = y;
   c->started = 1;
}

static void stbtt__csctx_v(stbtt__csctx *c, stbtt_uint8 type, stbtt_int32 x, stbtt_int32 y, stbtt_int32 cx, stbtt_int32 cy, stbtt_int32 cx1, stbtt_int32 cy1)
{
   if (c->bounds) {
      stbtt__track_vertex(c, x, y);
      if (type == STBTT_vcubic) {
         stbtt__track_vertex(c, cx, cy);
         stbtt__track_vertex(c, cx1, cy1);
      }
   } else {
      stbtt_setvertex(&c->pvertices[c->num_vertices], type, x, y, cx, cy);
      c->pvertices[c->num_vertices].cx1 = (stbtt_int16) cx1;
      c->pvertices[c->num_vertices].cy1 = (stbtt_int16) cy1;
   }
   c->num_vertices++;
}

static void stbtt__csctx_close_shape(stbtt__csctx *ctx)
{
   if (ctx->first_x != ctx->x || ctx->first_y != ctx->y)
      stbtt__csctx_v(ctx, STBTT_vline, (int)ctx->first_x, (int)ctx->first_y, 0, 0, 0, 0);
}

static void stbtt__csctx_rmove_to(stbtt__csctx *ctx, float dx, float dy)
{
   stbtt__csctx_close_shape(ctx);
   ctx->first_x = ctx->x = ctx->x + dx;
   ctx->first_y = ctx->y = ctx->y + dy;
   stbtt__csctx_v(ctx, STBTT_vmove, (int)ctx->x, (int)ctx->y, 0, 0, 0, 0);
}

static void stbtt__csctx_rline_to(stbtt__csctx *ctx, float dx, float dy)
{
   ctx->x += dx;
   ctx->y += dy;
   stbtt__csctx_v(ctx, STBTT_vline, (int)ctx->x, (int)ctx->y, 0, 0, 0, 0);
}

static void stbtt__csctx_rccurve_to(stbtt__csctx *ctx, float dx1, float dy1, float dx2, float dy2, float dx3, float dy3)
{
   float cx1 = ctx->x + dx1;
   float cy1 = ctx->y + dy1;
   float cx2 = cx1 + dx2;
   float cy2 = cy1 + dy2;
   ctx->x = cx2 + dx3;
   ctx->y = cy2 + dy3;
   stbtt__csctx_v(ctx, STBTT_vcubic, (int)ctx->x, (int)ctx->y, (int)cx1, (int)cy1, (int)cx2, (int)cy2);
}

static stbtt__buf stbtt__get_subr(stbtt__buf idx, int n)
{
   int count = stbtt__cff_index_count(&idx);
   int bias = 107;
   if (count >= 33900)
      bias = 32768;
   else if (count >= 1240)
      bias = 1131;
   n += bias;
   if (n < 0 || n >= count)
      return stbtt__new_buf(NULL, 0);
   return stbtt__cff_index_get(idx, n);
}

static stbtt__buf stbtt__cid_get_glyph_subrs(const stbtt_fontinfo *info, int glyph_index)
{
   stbtt__buf fdselect = info->fdselect;
   int nranges, start, end, v, fmt, fdselector = -1, i;

   stbtt__buf_seek(&fdselect, 0);
   fmt = stbtt__buf_get8(&fdselect);
   if (fmt == 0) {
      // untested
      stbtt__buf_skip(&fdselect, glyph_index);
      fdselector = stbtt__buf_get8(&fdselect);
   } else if (fmt == 3) {
      nranges = stbtt__buf_get16(&fdselect);
      start = stbtt__buf_get16(&fdselect);
      for (i = 0; i < nranges; i++) {
         v = stbtt__buf_get8(&fdselect);
         end = stbtt__buf_get16(&fdselect);
         if (glyph_index >= start && glyph_index < end) {
            fdselector = v;
            break;
         }
         start = end;
      }
   }
   if (fdselector == -1) stbtt__new_buf(NULL, 0);
   return stbtt__get_subrs(info->cff, stbtt__cff_index_get(info->fontdicts, fdselector));
}

static int stbtt__run_charstring(const stbtt_fontinfo *info, int glyph_index, stbtt__csctx *c)
{
   int in_header = 1, maskbits = 0, subr_stack_height = 0, sp = 0, v, i, b0;
   int has_subrs = 0, clear_stack;
   float s[48];
   stbtt__buf subr_stack[10], subrs = info->subrs, b;
   float f;

#define STBTT__CSERR(s) (0)

   // this currently ignores the initial width value, which isn't needed if we have hmtx
   b = stbtt__cff_index_get(info->charstrings, glyph_index);
   while (b.cursor < b.size) {
      i = 0;
      clear_stack = 1;
      b0 = stbtt__buf_get8(&b);
      switch (b0) {
      // @TODO implement hinting
      case 0x13: // hintmask
      case 0x14: // cntrmask
         if (in_header)
            maskbits += (sp / 2); // implicit "vstem"
         in_header = 0;
         stbtt__buf_skip(&b, (maskbits + 7) / 8);
         break;

      case 0x01: // hstem
      case 0x03: // vstem
      case 0x12: // hstemhm
      case 0x17: // vstemhm
         maskbits += (sp / 2);
         break;

      case 0x15: // rmoveto
         in_header = 0;
         if (sp < 2) return STBTT__CSERR("rmoveto stack");
         stbtt__csctx_rmove_to(c, s[sp-2], s[sp-1]);
         break;
      case 0x04: // vmoveto
         in_header = 0;
         if (sp < 1) return STBTT__CSERR("vmoveto stack");
         stbtt__csctx_rmove_to(c, 0, s[sp-1]);
         break;
      case 0x16: // hmoveto
         in_header = 0;
         if (sp < 1) return STBTT__CSERR("hmoveto stack");
         stbtt__csctx_rmove_to(c, s[sp-1], 0);
         break;

      case 0x05: // rlineto
         if (sp < 2) return STBTT__CSERR("rlineto stack");
         for (; i + 1 < sp; i += 2)
            stbtt__csctx_rline_to(c, s[i], s[i+1]);
         break;

      // hlineto/vlineto and vhcurveto/hvcurveto alternate horizontal and vertical
      // starting from a different place.

      case 0x07: // vlineto
         if (sp < 1) return STBTT__CSERR("vlineto stack");
         goto vlineto;
      case 0x06: // hlineto
         if (sp < 1) return STBTT__CSERR("hlineto stack");
         for (;;) {
            if (i >= sp) break;
            stbtt__csctx_rline_to(c, s[i], 0);
            i++;
      vlineto:
            if (i >= sp) break;
            stbtt__csctx_rline_to(c, 0, s[i]);
            i++;
         }
         break;

      case 0x1F: // hvcurveto
         if (sp < 4) return STBTT__CSERR("hvcurveto stack");
         goto hvcurveto;
      case 0x1E: // vhcurveto
         if (sp < 4) return STBTT__CSERR("vhcurveto stack");
         for (;;) {
            if (i + 3 >= sp) break;
            stbtt__csctx_rccurve_to(c, 0, s[i], s[i+1], s[i+2], s[i+3], (sp - i == 5) ? s[i + 4] : 0.0f);
            i += 4;
      hvcurveto:
            if (i + 3 >= sp) break;
            stbtt__csctx_rccurve_to(c, s[i], 0, s[i+1], s[i+2], (sp - i == 5) ? s[i+4] : 0.0f, s[i+3]);
            i += 4;
         }
         break;

      case 0x08: // rrcurveto
         if (sp < 6) return STBTT__CSERR("rcurveline stack");
         for (; i + 5 < sp; i += 6)
            stbtt__csctx_rccurve_to(c, s[i], s[i+1], s[i+2], s[i+3], s[i+4], s[i+5]);
         break;

      case 0x18: // rcurveline
         if (sp < 8) return STBTT__CSERR("rcurveline stack");
         for (; i + 5 < sp - 2; i += 6)
            stbtt__csctx_rccurve_to(c, s[i], s[i+1], s[i+2], s[i+3], s[i+4], s[i+5]);
         if (i + 1 >= sp) return STBTT__CSERR("rcurveline stack");
         stbtt__csctx_rline_to(c, s[i], s[i+1]);
         break;

      case 0x19: // rlinecurve
         if (sp < 8) return STBTT__CSERR("rlinecurve stack");
         for (; i + 1 < sp - 6; i += 2)
            stbtt__csctx_rline_to(c, s[i], s[i+1]);
         if (i + 5 >= sp) return STBTT__CSERR("rlinecurve stack");
         stbtt__csctx_rccurve_to(c, s[i], s[i+1], s[i+2], s[i+3], s[i+4], s[i+5]);
         break;

      case 0x1A: // vvcurveto
      case 0x1B: // hhcurveto
         if (sp < 4) return STBTT__CSERR("(vv|hh)curveto stack");
         f = 0.0;
         if (sp & 1) { f = s[i]; i++; }
         for (; i + 3 < sp; i += 4) {
            if (b0 == 0x1B)
               stbtt__csctx_rccurve_to(c, s[i], f, s[i+1], s[i+2], s[i+3], 0.0);
            else
               stbtt__csctx_rccurve_to(c, f, s[i], s[i+1], s[i+2], 0.0, s[i+3]);
            f = 0.0;
         }
         break;

      case 0x0A: // callsubr
         if (!has_subrs) {
            if (info->fdselect.size)
               subrs = stbtt__cid_get_glyph_subrs(info, glyph_index);
            has_subrs = 1;
         }
         // FALLTHROUGH
      case 0x1D: // callgsubr
         if (sp < 1) return STBTT__CSERR("call(g|)subr stack");
         v = (int) s[--sp];
         if (subr_stack_height >= 10) return STBTT__CSERR("recursion limit");
         subr_stack[subr_stack_height++] = b;
         b = stbtt__get_subr(b0 == 0x0A ? subrs : info->gsubrs, v);
         if (b.size == 0) return STBTT__CSERR("subr not found");
         b.cursor = 0;
         clear_stack = 0;
         break;

      case 0x0B: // return
         if (subr_stack_height <= 0) return STBTT__CSERR("return outside subr");
         b = subr_stack[--subr_stack_height];
         clear_stack = 0;
         break;

      case 0x0E: // endchar
         stbtt__csctx_close_shape(c);
         return 1;

      case 0x0C: { // two-byte escape
         float dx1, dx2, dx3, dx4, dx5, dx6, dy1, dy2, dy3, dy4, dy5, dy6;
         float dx, dy;
         int b1 = stbtt__buf_get8(&b);
         switch (b1) {
         // @TODO These "flex" implementations ignore the flex-depth and resolution,
         // and always draw beziers.
         case 0x22: // hflex
            if (sp < 7) return STBTT__CSERR("hflex stack");
            dx1 = s[0];
            dx2 = s[1];
            dy2 = s[2];
            dx3 = s[3];
            dx4 = s[4];
            dx5 = s[5];
            dx6 = s[6];
            stbtt__csctx_rccurve_to(c, dx1, 0, dx2, dy2, dx3, 0);
            stbtt__csctx_rccurve_to(c, dx4, 0, dx5, -dy2, dx6, 0);
            break;

         case 0x23: // flex
            if (sp < 13) return STBTT__CSERR("flex stack");
            dx1 = s[0];
            dy1 = s[1];
            dx2 = s[2];
            dy2 = s[3];
            dx3 = s[4];
            dy3 = s[5];
            dx4 = s[6];
            dy4 = s[7];
            dx5 = s[8];
            dy5 = s[9];
            dx6 = s[10];
            dy6 = s[11];
            //fd is s[12]
            stbtt__csctx_rccurve_to(c, dx1, dy1, dx2, dy2, dx3, dy3);
            stbtt__csctx_rccurve_to(c, dx4, dy4, dx5, dy5, dx6, dy6);
            break;

         case 0x24: // hflex1
            if (sp < 9) return STBTT__CSERR("hflex1 stack");
            dx1 = s[0];
            dy1 = s[1];
            dx2 = s[2];
            dy2 = s[3];
            dx3 = s[4];
            dx4 = s[5];
            dx5 = s[6];
            dy5 = s[7];
            dx6 = s[8];
            stbtt__csctx_rccurve_to(c, dx1, dy1, dx2, dy2, dx3, 0);
            stbtt__csctx_rccurve_to(c, dx4, 0, dx5, dy5, dx6, -(dy1+dy2+dy5));
            break;

         case 0x25: // flex1
            if (sp < 11) return STBTT__CSERR("flex1 stack");
            dx1 = s[0];
            dy1 = s[1];
            dx2 = s[2];
            dy2 = s[3];
            dx3 = s[4];
            dy3 = s[5];
            dx4 = s[6];
            dy4 = s[7];
            dx5 = s[8];
            dy5 = s[9];
            dx6 = dy6 = s[10];
            dx = dx1+dx2+dx3+dx4+dx5;
            dy = dy1+dy2+dy3+dy4+dy5;
            if (STBTT_fabs(dx) > STBTT_fabs(dy))
               dy6 = -dy;
            else
               dx6 = -dx;
            stbtt__csctx_rccurve_to(c, dx1, dy1, dx2, dy2, dx3, dy3);
            stbtt__csctx_rccurve_to(c, dx4, dy4, dx5, dy5, dx6, dy6);
            break;

         default:
            return STBTT__CSERR("unimplemented");
         }
      } break;

      default:
         if (b0 != 255 && b0 != 28 && b0 < 32)
            return STBTT__CSERR("reserved operator");

         // push immediate
         if (b0 == 255) {
            f = (float)(stbtt_int32)stbtt__buf_get32(&b) / 0x10000;
         } else {
            stbtt__buf_skip(&b, -1);
            f = (float)(stbtt_int16)stbtt__cff_int(&b);
         }
         if (sp >= 48) return STBTT__CSERR("push stack overflow");
         s[sp++] = f;
         clear_stack = 0;
         break;
      }
      if (clear_stack) sp = 0;
   }
   return STBTT__CSERR("no endchar");

#undef STBTT__CSERR
}

static int stbtt__GetGlyphShapeT2(const stbtt_fontinfo *info, int glyph_index, stbtt_vertex **pvertices)
{
   // runs the charstring twice, once to count and once to output (to avoid realloc)
   stbtt__csctx count_ctx = STBTT__CSCTX_INIT(1);
   stbtt__csctx output_ctx = STBTT__CSCTX_INIT(0);
   if (stbtt__run_charstring(info, glyph_index, &count_ctx)) {
      *pvertices = (stbtt_vertex*)STBTT_malloc(count_ctx.num_vertices*sizeof(stbtt_vertex), info->userdata);
      output_ctx.pvertices = *pvertices;
      if (stbtt__run_charstring(info, glyph_index, &output_ctx)) {
         STBTT_assert(output_ctx.num_vertices == count_ctx.num_vertices);
         return output_ctx.num_vertices;
      }
   }
   *pvertices = NULL;
   return 0;
}

static int stbtt__GetGlyphInfoT2(const stbtt_fontinfo *info, int glyph_index, int *x0, int *y0, int *x1, int *y1)
{
   stbtt__csctx c = STBTT__CSCTX_INIT(1);
   int r = stbtt__run_charstring(info, glyph_index, &c);
   if (x0)  *x0 = r ? c.min_x : 0;
   if (y0)  *y0 = r ? c.min_y : 0;
   if (x1)  *x1 = r ? c.max_x : 0;
   if (y1)  *y1 = r ? c.max_y : 0;
   return r ? c.num_vertices : 0;
}

STBTT_DEF int stbtt_GetGlyphShape(const stbtt_fontinfo *info, int glyph_index, stbtt_vertex **pvertices)
{
   if (!info->cff.size)
      return stbtt__GetGlyphShapeTT(info, glyph_index, pvertices);
   else
      return stbtt__GetGlyphShapeT2(info, glyph_index, pvertices);
}

STBTT_DEF void stbtt_GetGlyphHMetrics(const stbtt_fontinfo *info, int glyph_index, int *advanceWidth, int *leftSideBearing)
{
   stbtt_uint16 numOfLongHorMetrics = ttUSHORT(info->data+info->hhea + 34);
   if (glyph_index < numOfLongHorMetrics) {
      if (advanceWidth)     *advanceWidth    = ttSHORT(info->data + info->hmtx + 4*glyph_index);
      if (leftSideBearing)  *leftSideBearing = ttSHORT(info->data + info->hmtx + 4*glyph_index + 2);
   } else {
      if (advanceWidth)     *advanceWidth    = ttSHORT(info->data + info->hmtx + 4*(numOfLongHorMetrics-1));
      if (leftSideBearing)  *leftSideBearing = ttSHORT(info->data + info->hmtx + 4*numOfLongHorMetrics + 2*(glyph_index - numOfLongHorMetrics));
   }
}

STBTT_DEF int  stbtt_GetKerningTableLength(const stbtt_fontinfo *info)
{
   stbtt_uint8 *data = info->data + info->kern;

   // we only look at the first table. it must be 'horizontal' and format 0.
   if (!info->kern)
      return 0;
   if (ttUSHORT(data+2) < 1) // number of tables, need at least 1
      return 0;
   if (ttUSHORT(data+8) != 1) // horizontal flag must be set in format
      return 0;

   return ttUSHORT(data+10);
}

STBTT_DEF int stbtt_GetKerningTable(const stbtt_fontinfo *info, stbtt_kerningentry* table, int table_length)
{
   stbtt_uint8 *data = info->data + info->kern;
   int k, length;

   // we only look at the first table. it must be 'horizontal' and format 0.
   if (!info->kern)
      return 0;
   if (ttUSHORT(data+2) < 1) // number of tables, need at least 1
      return 0;
   if (ttUSHORT(data+8) != 1) // horizontal flag must be set in format
      return 0;

   length = ttUSHORT(data+10);
   if (table_length < length)
      length = table_length;

   for (k = 0; k < length; k++)
   {
      table[k].glyph1 = ttUSHORT(data+18+(k*6));
      table[k].glyph2 = ttUSHORT(data+20+(k*6));
      table[k].advance = ttSHORT(data+22+(k*6));
   }

   return length;
}

static int stbtt__GetGlyphKernInfoAdvance(const stbtt_fontinfo *info, int glyph1, int glyph2)
{
   stbtt_uint8 *data = info->data + info->kern;
   stbtt_uint32 needle, straw;
   int l, r, m;

   // we only look at the first table. it must be 'horizontal' and format 0.
   if (!info->kern)
      return 0;
   if (ttUSHORT(data+2) < 1) // number of tables, need at least 1
      return 0;
   if (ttUSHORT(data+8) != 1) // horizontal flag must be set in format
      return 0;

   l = 0;
   r = ttUSHORT(data+10) - 1;
   needle = glyph1 << 16 | glyph2;
   while (l <= r) {
      m = (l + r) >> 1;
      straw = ttULONG(data+18+(m*6)); // note: unaligned read
      if (needle < straw)
         r = m - 1;
      else if (needle > straw)
         l = m + 1;
      else
         return ttSHORT(data+22+(m*6));
   }
   return 0;
}

static stbtt_int32 stbtt__GetCoverageIndex(stbtt_uint8 *coverageTable, int glyph)
{
   stbtt_uint16 coverageFormat = ttUSHORT(coverageTable);
   switch (coverageFormat) {
      case 1: {
         stbtt_uint16 glyphCount = ttUSHORT(coverageTable + 2);

         // Binary search.
         stbtt_int32 l=0, r=glyphCount-1, m;
         int straw, needle=glyph;
         while (l <= r) {
            stbtt_uint8 *glyphArray = coverageTable + 4;
            stbtt_uint16 glyphID;
            m = (l + r) >> 1;
            glyphID = ttUSHORT(glyphArray + 2 * m);
            straw = glyphID;
            if (needle < straw)
               r = m - 1;
            else if (needle > straw)
               l = m + 1;
            else {
               return m;
            }
         }
         break;
      }

      case 2: {
         stbtt_uint16 rangeCount = ttUSHORT(coverageTable + 2);
         stbtt_uint8 *rangeArray = coverageTable + 4;

         // Binary search.
         stbtt_int32 l=0, r=rangeCount-1, m;
         int strawStart, strawEnd, needle=glyph;
         while (l <= r) {
            stbtt_uint8 *rangeRecord;
            m = (l + r) >> 1;
            rangeRecord = rangeArray + 6 * m;
            strawStart = ttUSHORT(rangeRecord);
            strawEnd = ttUSHORT(rangeRecord + 2);
            if (needle < strawStart)
               r = m - 1;
            else if (needle > strawEnd)
               l = m + 1;
            else {
               stbtt_uint16 startCoverageIndex = ttUSHORT(rangeRecord + 4);
               return startCoverageIndex + glyph - strawStart;
            }
         }
         break;
      }

      default: return -1; // unsupported
   }

   return -1;
}

static stbtt_int32  stbtt__GetGlyphClass(stbtt_uint8 *classDefTable, int glyph)
{
   stbtt_uint16 classDefFormat = ttUSHORT(classDefTable);
   switch (classDefFormat)
   {
      case 1: {
         stbtt_uint16 startGlyphID = ttUSHORT(classDefTable + 2);
         stbtt_uint16 glyphCount = ttUSHORT(classDefTable + 4);
         stbtt_uint8 *classDef1ValueArray = classDefTable + 6;

         if (glyph >= startGlyphID && glyph < startGlyphID + glyphCount)
            return (stbtt_int32)ttUSHORT(classDef1ValueArray + 2 * (glyph - startGlyphID));
         break;
      }

      case 2: {
         stbtt_uint16 classRangeCount = ttUSHORT(classDefTable + 2);
         stbtt_uint8 *classRangeRecords = classDefTable + 4;

         // Binary search.
         stbtt_int32 l=0, r=classRangeCount-1, m;
         int strawStart, strawEnd, needle=glyph;
         while (l <= r) {
            stbtt_uint8 *classRangeRecord;
            m = (l + r) >> 1;
            classRangeRecord = classRangeRecords + 6 * m;
            strawStart = ttUSHORT(classRangeRecord);
            strawEnd = ttUSHORT(classRangeRecord + 2);
            if (needle < strawStart)
               r = m - 1;
            else if (needle > strawEnd)
               l = m + 1;
            else
               return (stbtt_int32)ttUSHORT(classRangeRecord + 4);
         }
         break;
      }

      default:
         return -1; // Unsupported definition type, return an error.
   }

   // "All glyphs not assigned to a class fall into class 0". (OpenType spec)
   return 0;
}

// Define to STBTT_assert(x) if you want to break on unimplemented formats.
#define STBTT_GPOS_TODO_assert(x)

static stbtt_int32 stbtt__GetGlyphGPOSInfoAdvance(const stbtt_fontinfo *info, int glyph1, int glyph2)
{
   stbtt_uint16 lookupListOffset;
   stbtt_uint8 *lookupList;
   stbtt_uint16 lookupCount;
   stbtt_uint8 *data;
   stbtt_int32 i, sti;

   if (!info->gpos) return 0;

   data = info->data + info->gpos;

   if (ttUSHORT(data+0) != 1) return 0; // Major version 1
   if (ttUSHORT(data+2) != 0) return 0; // Minor version 0

   lookupListOffset = ttUSHORT(data+8);
   lookupList = data + lookupListOffset;
   lookupCount = ttUSHORT(lookupList);

   for (i=0; i<lookupCount; ++i) {
      stbtt_uint16 lookupOffset = ttUSHORT(lookupList + 2 + 2 * i);
      stbtt_uint8 *lookupTable = lookupList + lookupOffset;

      stbtt_uint16 lookupType = ttUSHORT(lookupTable);
      stbtt_uint16 subTableCount = ttUSHORT(lookupTable + 4);
      stbtt_uint8 *subTableOffsets = lookupTable + 6;
      if (lookupType != 2) // Pair Adjustment Positioning Subtable
         continue;

      for (sti=0; sti<subTableCount; sti++) {
         stbtt_uint16 subtableOffset = ttUSHORT(subTableOffsets + 2 * sti);
         stbtt_uint8 *table = lookupTable + subtableOffset;
         stbtt_uint16 posFormat = ttUSHORT(table);
         stbtt_uint16 coverageOffset = ttUSHORT(table + 2);
         stbtt_int32 coverageIndex = stbtt__GetCoverageIndex(table + coverageOffset, glyph1);
         if (coverageIndex == -1) continue;

         switch (posFormat) {
            case 1: {
               stbtt_int32 l, r, m;
               int straw, needle;
               stbtt_uint16 valueFormat1 = ttUSHORT(table + 4);
               stbtt_uint16 valueFormat2 = ttUSHORT(table + 6);
               if (valueFormat1 == 4 && valueFormat2 == 0) { // Support more formats?
                  stbtt_int32 valueRecordPairSizeInBytes = 2;
                  stbtt_uint16 pairSetCount = ttUSHORT(table + 8);
                  stbtt_uint16 pairPosOffset = ttUSHORT(table + 10 + 2 * coverageIndex);
                  stbtt_uint8 *pairValueTable = table + pairPosOffset;
                  stbtt_uint16 pairValueCount = ttUSHORT(pairValueTable);
                  stbtt_uint8 *pairValueArray = pairValueTable + 2;

                  if (coverageIndex >= pairSetCount) return 0;

                  needle=glyph2;
                  r=pairValueCount-1;
                  l=0;

                  // Binary search.
                  while (l <= r) {
                     stbtt_uint16 secondGlyph;
                     stbtt_uint8 *pairValue;
                     m = (l + r) >> 1;
                     pairValue = pairValueArray + (2 + valueRecordPairSizeInBytes) * m;
                     secondGlyph = ttUSHORT(pairValue);
                     straw = secondGlyph;
                     if (needle < straw)
                        r = m - 1;
                     else if (needle > straw)
                        l = m + 1;
                     else {
                        stbtt_int16 xAdvance = ttSHORT(pairValue + 2);
                        return xAdvance;
                     }
                  }
               } else
                  return 0;
               break;
            }

            case 2: {
               stbtt_uint16 valueFormat1 = ttUSHORT(table + 4);
               stbtt_uint16 valueFormat2 = ttUSHORT(table + 6);
               if (valueFormat1 == 4 && valueFormat2 == 0) { // Support more formats?
                  stbtt_uint16 classDef1Offset = ttUSHORT(table + 8);
                  stbtt_uint16 classDef2Offset = ttUSHORT(table + 10);
                  int glyph1class = stbtt__GetGlyphClass(table + classDef1Offset, glyph1);
                  int glyph2class = stbtt__GetGlyphClass(table + classDef2Offset, glyph2);

                  stbtt_uint16 class1Count = ttUSHORT(table + 12);
                  stbtt_uint16 class2Count = ttUSHORT(table + 14);
                  stbtt_uint8 *class1Records, *class2Records;
                  stbtt_int16 xAdvance;

                  if (glyph1class < 0 || glyph1class >= class1Count) return 0; // malformed
                  if (glyph2class < 0 || glyph2class >= class2Count) return 0; // malformed

                  class1Records = table + 16;
                  class2Records = class1Records + 2 * (glyph1class * class2Count);
                  xAdvance = ttSHORT(class2Records + 2 * glyph2class);
                  return xAdvance;
               } else
                  return 0;
               break;
            }

            default:
               return 0; // Unsupported position format
         }
      }
   }

   return 0;
}

STBTT_DEF int  stbtt_GetGlyphKernAdvance(const stbtt_fontinfo *info, int g1, int g2)
{
   int xAdvance = 0;

   if (info->gpos)
      xAdvance += stbtt__GetGlyphGPOSInfoAdvance(info, g1, g2);
   else if (info->kern)
      xAdvance += stbtt__GetGlyphKernInfoAdvance(info, g1, g2);

   return xAdvance;
}

STBTT_DEF int  stbtt_GetCodepointKernAdvance(const stbtt_fontinfo *info, int ch1, int ch2)
{
   if (!info->kern && !info->gpos) // if no kerning table, don't waste time looking up both codepoint->glyphs
      return 0;
   return stbtt_GetGlyphKernAdvance(info, stbtt_FindGlyphIndex(info,ch1), stbtt_FindGlyphIndex(info,ch2));
}

STBTT_DEF void stbtt_GetCodepointHMetrics(const stbtt_fontinfo *info, int codepoint, int *advanceWidth, int *leftSideBearing)
{
   stbtt_GetGlyphHMetrics(info, stbtt_FindGlyphIndex(info,codepoint), advanceWidth, leftSideBearing);
}

STBTT_DEF void stbtt_GetFontVMetrics(const stbtt_fontinfo *info, int *ascent, int *descent, int *lineGap)
{
   if (ascent ) *ascent  = ttSHORT(info->data+info->hhea + 4);
   if (descent) *descent = ttSHORT(info->data+info->hhea + 6);
   if (lineGap) *lineGap = ttSHORT(info->data+info->hhea + 8);
}

STBTT_DEF int  stbtt_GetFontVMetricsOS2(const stbtt_fontinfo *info, int *typoAscent, int *typoDescent, int *typoLineGap)
{
   int tab = stbtt__find_table(info->data, info->fontstart, "OS/2");
   if (!tab)
      return 0;
   if (typoAscent ) *typoAscent  = ttSHORT(info->data+tab + 68);
   if (typoDescent) *typoDescent = ttSHORT(info->data+tab + 70);
   if (typoLineGap) *typoLineGap = ttSHORT(info->data+tab + 72);
   return 1;
}

STBTT_DEF void stbtt_GetFontBoundingBox(const stbtt_fontinfo *info, int *x0, int *y0, int *x1, int *y1)
{
   *x0 = ttSHORT(info->data + info->head + 36);
   *y0 = ttSHORT(info->data + info->head + 38);
   *x1 = ttSHORT(info->data + info->head + 40);
   *y1 = ttSHORT(info->data + info->head + 42);
}

STBTT_DEF float stbtt_ScaleForPixelHeight(const stbtt_fontinfo *info, float height)
{
   int fheight = ttSHORT(info->data + info->hhea + 4) - ttSHORT(info->data + info->hhea + 6);
   return (float) height / fheight;
}

STBTT_DEF float stbtt_ScaleForMappingEmToPixels(const stbtt_fontinfo *info, float pixels)
{
   int unitsPerEm = ttUSHORT(info->data + info->head + 18);
   return pixels / unitsPerEm;
}

STBTT_DEF void stbtt_FreeShape(const stbtt_fontinfo *info, stbtt_vertex *v)
{
   STBTT_free(v, info->userdata);
}

STBTT_DEF stbtt_uint8 *stbtt_FindSVGDoc(const stbtt_fontinfo *info, int gl)
{
   int i;
   stbtt_uint8 *data = info->data;
   stbtt_uint8 *svg_doc_list = data + stbtt__get_svg((stbtt_fontinfo *) info);

   int numEntries = ttUSHORT(svg_doc_list);
   stbtt_uint8 *svg_docs = svg_doc_list + 2;

   for(i=0; i<numEntries; i++) {
      stbtt_uint8 *svg_doc = svg_docs + (12 * i);
      if ((gl >= ttUSHORT(svg_doc)) && (gl <= ttUSHORT(svg_doc + 2)))
         return svg_doc;
   }
   return 0;
}

STBTT_DEF int stbtt_GetGlyphSVG(const stbtt_fontinfo *info, int gl, const char **svg)
{
   stbtt_uint8 *data = info->data;
   stbtt_uint8 *svg_doc;

   if (info->svg == 0)
      return 0;

   svg_doc = stbtt_FindSVGDoc(info, gl);
   if (svg_doc != NULL) {
      *svg = (char *) data + info->svg + ttULONG(svg_doc + 4);
      return ttULONG(svg_doc + 8);
   } else {
      return 0;
   }
}

STBTT_DEF int stbtt_GetCodepointSVG(const stbtt_fontinfo *info, int unicode_codepoint, const char **svg)
{
   return stbtt_GetGlyphSVG(info, stbtt_FindGlyphIndex(info, unicode_codepoint), svg);
}

//////////////////////////////////////////////////////////////////////////////
//
// antialiasing software rasterizer
//

STBTT_DEF void stbtt_GetGlyphBitmapBoxSubpixel(const stbtt_fontinfo *font, int glyph, float scale_x, float scale_y,float shift_x, float shift_y, int *ix0, int *iy0, int *ix1, int *iy1)
{
   int x0=0,y0=0,x1,y1; // =0 suppresses compiler warning
   if (!stbtt_GetGlyphBox(font, glyph, &x0,&y0,&x1,&y1)) {
      // e.g. space character
      if (ix0) *ix0 = 0;
      if (iy0) *iy0 = 0;
      if (ix1) *ix1 = 0;
      if (iy1) *iy1 = 0;
   } else {
      // move to integral bboxes (treating pixels as little squares, what pixels get touched)?
      if (ix0) *ix0 = STBTT_ifloor( x0 * scale_x + shift_x);
      if (iy0) *iy0 = STBTT_ifloor(-y1 * scale_y + shift_y);
      if (ix1) *ix1 = STBTT_iceil ( x1 * scale_x + shift_x);
      if (iy1) *iy1 = STBTT_iceil (-y0 * scale_y + shift_y);
   }
}

STBTT_DEF void stbtt_GetGlyphBitmapBox(const stbtt_fontinfo *font, int glyph, float scale_x, float scale_y, int *ix0, int *iy0, int *ix1, int *iy1)
{
   stbtt_GetGlyphBitmapBoxSubpixel(font, glyph, scale_x, scale_y,0.0f,0.0f, ix0, iy0, ix1, iy1);
}

STBTT_DEF void stbtt_GetCodepointBitmapBoxSubpixel(const stbtt_fontinfo *font, int codepoint, float scale_x, float scale_y, float shift_x, float shift_y, int *ix0, int *iy0, int *ix1, int *iy1)
{
   stbtt_GetGlyphBitmapBoxSubpixel(font, stbtt_FindGlyphIndex(font,codepoint), scale_x, scale_y,shift_x,shift_y, ix0,iy0,ix1,iy1);
}

STBTT_DEF void stbtt_GetCodepointBitmapBox(const stbtt_fontinfo *font, int codepoint, float scale_x, float scale_y, int *ix0, int *iy0, int *ix1, int *iy1)
{
   stbtt_GetCodepointBitmapBoxSubpixel(font, codepoint, scale_x, scale_y,0.0f,0.0f, ix0,iy0,ix1,iy1);
}

//////////////////////////////////////////////////////////////////////////////
//
//  Rasterizer

typedef struct stbtt__hheap_chunk
{
   struct stbtt__hheap_chunk *next;
} stbtt__hheap_chunk;

typedef struct stbtt__hheap
{
   struct stbtt__hheap_chunk *head;
   void   *first_free;
   int    num_remaining_in_head_chunk;
} stbtt__hheap;

static void *stbtt__hheap_alloc(stbtt__hheap *hh, size_t size, void *userdata)
{
   if (hh->first_free) {
      void *p = hh->first_free;
      hh->first_free = * (void **) p;
      return p;
   } else {
      if (hh->num_remaining_in_head_chunk == 0) {
         int count = (size < 32 ? 2000 : size < 128 ? 800 : 100);
         stbtt__hheap_chunk *c = (stbtt__hheap_chunk *) STBTT_malloc(sizeof(stbtt__hheap_chunk) + size * count, userdata);
         if (c == NULL)
            return NULL;
         c->next = hh->head;
         hh->head = c;
         hh->num_remaining_in_head_chunk = count;
      }
      --hh->num_remaining_in_head_chunk;
      return (char *) (hh->head) + sizeof(stbtt__hheap_chunk) + size * hh->num_remaining_in_head_chunk;
   }
}

static void stbtt__hheap_free(stbtt__hheap *hh, void *p)
{
   *(void **) p = hh->first_free;
   hh->first_free = p;
}

static void stbtt__hheap_cleanup(stbtt__hheap *hh, void *userdata)
{
   stbtt__hheap_chunk *c = hh->head;
   while (c) {
      stbtt__hheap_chunk *n = c->next;
      STBTT_free(c, userdata);
      c = n;
   }
}

typedef struct stbtt__edge {
   float x0,y0, x1,y1;
   int invert;
} stbtt__edge;


typedef struct stbtt__active_edge
{
   struct stbtt__active_edge *next;
   #if STBTT_RASTERIZER_VERSION==1
   int x,dx;
   float ey;
   int direction;
   #elif STBTT_RASTERIZER_VERSION==2
   float fx,fdx,fdy;
   float direction;
   float sy;
   float ey;
   #else
   #error "Unrecognized value of STBTT_RASTERIZER_VERSION"
   #endif
} stbtt__active_edge;

#if STBTT_RASTERIZER_VERSION == 1
#define STBTT_FIXSHIFT   10
#define STBTT_FIX        (1 << STBTT_FIXSHIFT)
#define STBTT_FIXMASK    (STBTT_FIX-1)

static stbtt__active_edge *stbtt__new_active(stbtt__hheap *hh, stbtt__edge *e, int off_x, float start_point, void *userdata)
{
   stbtt__active_edge *z = (stbtt__active_edge *) stbtt__hheap_alloc(hh, sizeof(*z), userdata);
   float dxdy = (e->x1 - e->x0) / (e->y1 - e->y0);
   STBTT_assert(z != NULL);
   if (!z) return z;

   // round dx down to avoid overshooting
   if (dxdy < 0)
      z->dx = -STBTT_ifloor(STBTT_FIX * -dxdy);
   else
      z->dx = STBTT_ifloor(STBTT_FIX * dxdy);

   z->x = STBTT_ifloor(STBTT_FIX * e->x0 + z->dx * (start_point - e->y0)); // use z->dx so when we offset later it's by the same amount
   z->x -= off_x * STBTT_FIX;

   z->ey = e->y1;
   z->next = 0;
   z->direction = e->invert ? 1 : -1;
   return z;
}
#elif STBTT_RASTERIZER_VERSION == 2
static stbtt__active_edge *stbtt__new_active(stbtt__hheap *hh, stbtt__edge *e, int off_x, float start_point, void *userdata)
{
   stbtt__active_edge *z = (stbtt__active_edge *) stbtt__hheap_alloc(hh, sizeof(*z), userdata);
   float dxdy = (e->x1 - e->x0) / (e->y1 - e->y0);
   STBTT_assert(z != NULL);
   //STBTT_assert(e->y0 <= start_point);
   if (!z) return z;
   z->fdx = dxdy;
   z->fdy = dxdy != 0.0f ? (1.0f/dxdy) : 0.0f;
   z->fx = e->x0 + dxdy * (start_point - e->y0);
   z->fx -= off_x;
   z->direction = e->invert ? 1.0f : -1.0f;
   z->sy = e->y0;
   z->ey = e->y1;
   z->next = 0;
   return z;
}
#else
#error "Unrecognized value of STBTT_RASTERIZER_VERSION"
#endif

#if STBTT_RASTERIZER_VERSION == 1
// note: this routine clips fills that extend off the edges... ideally this
// wouldn't happen, but it could happen if the truetype glyph bounding boxes
// are wrong, or if the user supplies a too-small bitmap
static void stbtt__fill_active_edges(unsigned char *scanline, int len, stbtt__active_edge *e, int max_weight)
{
   // non-zero winding fill
   int x0=0, w=0;

   while (e) {
      if (w == 0) {
         // if we're currently at zero, we need to record the edge start point
         x0 = e->x; w += e->direction;
      } else {
         int x1 = e->x; w += e->direction;
         // if we went to zero, we need to draw
         if (w == 0) {
            int i = x0 >> STBTT_FIXSHIFT;
            int j = x1 >> STBTT_FIXSHIFT;

            if (i < len && j >= 0) {
               if (i == j) {
                  // x0,x1 are the same pixel, so compute combined coverage
                  scanline[i] = scanline[i] + (stbtt_uint8) ((x1 - x0) * max_weight >> STBTT_FIXSHIFT);
               } else {
                  if (i >= 0) // add antialiasing for x0
                     scanline[i] = scanline[i] + (stbtt_uint8) (((STBTT_FIX - (x0 & STBTT_FIXMASK)) * max_weight) >> STBTT_FIXSHIFT);
                  else
                     i = -1; // clip

                  if (j < len) // add antialiasing for x1
                     scanline[j] = scanline[j] + (stbtt_uint8) (((x1 & STBTT_FIXMASK) * max_weight) >> STBTT_FIXSHIFT);
                  else
                     j = len; // clip

                  for (++i; i < j; ++i) // fill pixels between x0 and x1
                     scanline[i] = scanline[i] + (stbtt_uint8) max_weight;
               }
            }
         }
      }

      e = e->next;
   }
}

static void stbtt__rasterize_sorted_edges(stbtt__bitmap *result, stbtt__edge *e, int n, int vsubsample, int off_x, int off_y, void *userdata)
{
   stbtt__hheap hh = { 0, 0, 0 };
   stbtt__active_edge *active = NULL;
   int y,j=0;
   int max_weight = (255 / vsubsample);  // weight per vertical scanline
   int s; // vertical subsample index
   unsigned char scanline_data[512], *scanline;

   if (result->w > 512)
      scanline = (unsigned char *) STBTT_malloc(result->w, userdata);
   else
      scanline = scanline_data;

   y = off_y * vsubsample;
   e[n].y0 = (off_y + result->h) * (float) vsubsample + 1;

   while (j < result->h) {
      STBTT_memset(scanline, 0, result->w);
      for (s=0; s < vsubsample; ++s) {
         // find center of pixel for this scanline
         float scan_y = y + 0.5f;
         stbtt__active_edge **step = &active;

         // update all active edges;
         // remove all active edges that terminate before the center of this scanline
         while (*step) {
            stbtt__active_edge * z = *step;
            if (z->ey <= scan_y) {
               *step = z->next; // delete from list
               STBTT_assert(z->direction);
               z->direction = 0;
               stbtt__hheap_free(&hh, z);
            } else {
               z->x += z->dx; // advance to position for current scanline
               step = &((*step)->next); // advance through list
            }
         }

         // resort the list if needed
         for(;;) {
            int changed=0;
            step = &active;
            while (*step && (*step)->next) {
               if ((*step)->x > (*step)->next->x) {
                  stbtt__active_edge *t = *step;
                  stbtt__active_edge *q = t->next;

                  t->next = q->next;
                  q->next = t;
                  *step = q;
                  changed = 1;
               }
               step = &(*step)->next;
            }
            if (!changed) break;
         }

         // insert all edges that start before the center of this scanline -- omit ones that also end on this scanline
         while (e->y0 <= scan_y) {
            if (e->y1 > scan_y) {
               stbtt__active_edge *z = stbtt__new_active(&hh, e, off_x, scan_y, userdata);
               if (z != NULL) {
                  // find insertion point
                  if (active == NULL)
                     active = z;
                  else if (z->x < active->x) {
                     // insert at front
                     z->next = active;
                     active = z;
                  } else {
                     // find thing to insert AFTER
                     stbtt__active_edge *p = active;
                     while (p->next && p->next->x < z->x)
                        p = p->next;
                     // at this point, p->next->x is NOT < z->x
                     z->next = p->next;
                     p->next = z;
                  }
               }
            }
            ++e;
         }

         // now process all active edges in XOR fashion
         if (active)
            stbtt__fill_active_edges(scanline, result->w, active, max_weight);

         ++y;
      }
      STBTT_memcpy(result->pixels + j * result->stride, scanline, result->w);
      ++j;
   }

   stbtt__hheap_cleanup(&hh, userdata);

   if (scanline != scanline_data)
      STBTT_free(scanline, userdata);
}

#elif STBTT_RASTERIZER_VERSION == 2

// the edge passed in here does not cross the vertical line at x or the vertical line at x+1
// (i.e. it has already been clipped to those)
static void stbtt__handle_clipped_edge(float *scanline, int x, stbtt__active_edge *e, float x0, float y0, float x1, float y1)
{
   if (y0 == y1) return;
   STBTT_assert(y0 < y1);
   STBTT_assert(e->sy <= e->ey);
   if (y0 > e->ey) return;
   if (y1 < e->sy) return;
   if (y0 < e->sy) {
      x0 += (x1-x0) * (e->sy - y0) / (y1-y0);
      y0 = e->sy;
   }
   if (y1 > e->ey) {
      x1 += (x1-x0) * (e->ey - y1) / (y1-y0);
      y1 = e->ey;
   }

   if (x0 == x)
      STBTT_assert(x1 <= x+1);
   else if (x0 == x+1)
      STBTT_assert(x1 >= x);
   else if (x0 <= x)
      STBTT_assert(x1 <= x);
   else if (x0 >= x+1)
      STBTT_assert(x1 >= x+1);
   else
      STBTT_assert(x1 >= x && x1 <= x+1);

   if (x0 <= x && x1 <= x)
      scanline[x] += e->direction * (y1-y0);
   else if (x0 >= x+1 && x1 >= x+1)
      ;
   else {
      STBTT_assert(x0 >= x && x0 <= x+1 && x1 >= x && x1 <= x+1);
      scanline[x] += e->direction * (y1-y0) * (1-((x0-x)+(x1-x))/2); // coverage = 1 - average x position
   }
}

static float stbtt__sized_trapezoid_area(float height, float top_width, float bottom_width)
{
   STBTT_assert(top_width >= 0);
   STBTT_assert(bottom_width >= 0);
   return (top_width + bottom_width) / 2.0f * height;
}

static float stbtt__position_trapezoid_area(float height, float tx0, float tx1, float bx0, float bx1)
{
   return stbtt__sized_trapezoid_area(height, tx1 - tx0, bx1 - bx0);
}

static float stbtt__sized_triangle_area(float height, float width)
{
   return height * width / 2;
}

static void stbtt__fill_active_edges_new(float *scanline, float *scanline_fill, int len, stbtt__active_edge *e, float y_top)
{
   float y_bottom = y_top+1;

   while (e) {
      // brute force every pixel

      // compute intersection points with top & bottom
      STBTT_assert(e->ey >= y_top);

      if (e->fdx == 0) {
         float x0 = e->fx;
         if (x0 < len) {
            if (x0 >= 0) {
               stbtt__handle_clipped_edge(scanline,(int) x0,e, x0,y_top, x0,y_bottom);
               stbtt__handle_clipped_edge(scanline_fill-1,(int) x0+1,e, x0,y_top, x0,y_bottom);
            } else {
               stbtt__handle_clipped_edge(scanline_fill-1,0,e, x0,y_top, x0,y_bottom);
            }
         }
      } else {
         float x0 = e->fx;
         float dx = e->fdx;
         float xb = x0 + dx;
         float x_top, x_bottom;
         float sy0,sy1;
         float dy = e->fdy;
         STBTT_assert(e->sy <= y_bottom && e->ey >= y_top);

         // compute endpoints of line segment clipped to this scanline (if the
         // line segment starts on this scanline. x0 is the intersection of the
         // line with y_top, but that may be off the line segment.
         if (e->sy > y_top) {
            x_top = x0 + dx * (e->sy - y_top);
            sy0 = e->sy;
         } else {
            x_top = x0;
            sy0 = y_top;
         }
         if (e->ey < y_bottom) {
            x_bottom = x0 + dx * (e->ey - y_top);
            sy1 = e->ey;
         } else {
            x_bottom = xb;
            sy1 = y_bottom;
         }

         if (x_top >= 0 && x_bottom >= 0 && x_top < len && x_bottom < len) {
            // from here on, we don't have to range check x values

            if ((int) x_top == (int) x_bottom) {
               float height;
               // simple case, only spans one pixel
               int x = (int) x_top;
               height = (sy1 - sy0) * e->direction;
               STBTT_assert(x >= 0 && x < len);
               scanline[x]      += stbtt__position_trapezoid_area(height, x_top, x+1.0f, x_bottom, x+1.0f);
               scanline_fill[x] += height; // everything right of this pixel is filled
            } else {
               int x,x1,x2;
               float y_crossing, y_final, step, sign, area;
               // covers 2+ pixels
               if (x_top > x_bottom) {
                  // flip scanline vertically; signed area is the same
                  float t;
                  sy0 = y_bottom - (sy0 - y_top);
                  sy1 = y_bottom - (sy1 - y_top);
                  t = sy0, sy0 = sy1, sy1 = t;
                  t = x_bottom, x_bottom = x_top, x_top = t;
                  dx = -dx;
                  dy = -dy;
                  t = x0, x0 = xb, xb = t;
               }
               STBTT_assert(dy >= 0);
               STBTT_assert(dx >= 0);

               x1 = (int) x_top;
               x2 = (int) x_bottom;
               // compute intersection with y axis at x1+1
               y_crossing = y_top + dy * (x1+1 - x0);

               // compute intersection with y axis at x2
               y_final = y_top + dy * (x2 - x0);

               //           x1    x_top                            x2    x_bottom
               //     y_top  +------|-----+------------+------------+--------|---+------------+
               //            |            |            |            |            |            |
               //            |            |            |            |            |            |
               //       sy0  |      Txxxxx|............|............|............|............|
               // y_crossing |            *xxxxx.......|............|............|............|
               //            |            |     xxxxx..|............|............|............|
               //            |            |     /-   xx*xxxx........|............|............|
               //            |            | dy <       |    xxxxxx..|............|............|
               //   y_final  |            |     \-     |          xx*xxx.........|............|
               //       sy1  |            |            |            |   xxxxxB...|............|
               //            |            |            |            |            |            |
               //            |            |            |            |            |            |
               //  y_bottom  +------------+------------+------------+------------+------------+
               //
               // goal is to measure the area covered by '.' in each pixel

               // if x2 is right at the right edge of x1, y_crossing can blow up, github #1057
               // @TODO: maybe test against sy1 rather than y_bottom?
               if (y_crossing > y_bottom)
                  y_crossing = y_bottom;

               sign = e->direction;

               // area of the rectangle covered from sy0..y_crossing
               area = sign * (y_crossing-sy0);

               // area of the triangle (x_top,sy0), (x1+1,sy0), (x1+1,y_crossing)
               scanline[x1] += stbtt__sized_triangle_area(area, x1+1 - x_top);

               // check if final y_crossing is blown up; no test case for this
               if (y_final > y_bottom) {
                  y_final = y_bottom;
                  dy = (y_final - y_crossing ) / (x2 - (x1+1)); // if denom=0, y_final = y_crossing, so y_final <= y_bottom
               }

               // in second pixel, area covered by line segment found in first pixel
               // is always a rectangle 1 wide * the height of that line segment; this
               // is exactly what the variable 'area' stores. it also gets a contribution
               // from the line segment within it. the THIRD pixel will get the first
               // pixel's rectangle contribution, the second pixel's rectangle contribution,
               // and its own contribution. the 'own contribution' is the same in every pixel except
               // the leftmost and rightmost, a trapezoid that slides down in each pixel.
               // the second pixel's contribution to the third pixel will be the
               // rectangle 1 wide times the height change in the second pixel, which is dy.

               step = sign * dy * 1; // dy is dy/dx, change in y for every 1 change in x,
               // which multiplied by 1-pixel-width is how much pixel area changes for each step in x
               // so the area advances by 'step' every time

               for (x = x1+1; x < x2; ++x) {
                  scanline[x] += area + step/2; // area of trapezoid is 1*step/2
                  area += step;
               }
               STBTT_assert(STBTT_fabs(area) <= 1.01f); // accumulated error from area += step unless we round step down
               STBTT_assert(sy1 > y_final-0.01f);

               // area covered in the last pixel is the rectangle from all the pixels to the left,
               // plus the trapezoid filled by the line segment in this pixel all the way to the right edge
               scanline[x2] += area + sign * stbtt__position_trapezoid_area(sy1-y_final, (float) x2, x2+1.0f, x_bottom, x2+1.0f);

               // the rest of the line is filled based on the total height of the line segment in this pixel
               scanline_fill[x2] += sign * (sy1-sy0);
            }
         } else {
            // if edge goes outside of box we're drawing, we require
            // clipping logic. since this does not match the intended use
            // of this library, we use a different, very slow brute
            // force implementation
            // note though that this does happen some of the time because
            // x_top and x_bottom can be extrapolated at the top & bottom of
            // the shape and actually lie outside the bounding box
            int x;
            for (x=0; x < len; ++x) {
               // cases:
               //
               // there can be up to two intersections with the pixel. any intersection
               // with left or right edges can be handled by splitting into two (or three)
               // regions. intersections with top & bottom do not necessitate case-wise logic.
               //
               // the old way of doing this found the intersections with the left & right edges,
               // then used some simple logic to produce up to three segments in sorted order
               // from top-to-bottom. however, this had a problem: if an x edge was epsilon
               // across the x border, then the corresponding y position might not be distinct
               // from the other y segment, and it might ignored as an empty segment. to avoid
               // that, we need to explicitly produce segments based on x positions.

               // rename variables to clearly-defined pairs
               float y0 = y_top;
               float x1 = (float) (x);
               float x2 = (float) (x+1);
               float x3 = xb;
               float y3 = y_bottom;

               // x = e->x + e->dx * (y-y_top)
               // (y-y_top) = (x - e->x) / e->dx
               // y = (x - e->x) / e->dx + y_top
               float y1 = (x - x0) / dx + y_top;
               float y2 = (x+1 - x0) / dx + y_top;

               if (x0 < x1 && x3 > x2) {         // three segments descending down-right
                  stbtt__handle_clipped_edge(scanline,x,e, x0,y0, x1,y1);
                  stbtt__handle_clipped_edge(scanline,x,e, x1,y1, x2,y2);
                  stbtt__handle_clipped_edge(scanline,x,e, x2,y2, x3,y3);
               } else if (x3 < x1 && x0 > x2) {  // three segments descending down-left
                  stbtt__handle_clipped_edge(scanline,x,e, x0,y0, x2,y2);
                  stbtt__handle_clipped_edge(scanline,x,e, x2,y2, x1,y1);
                  stbtt__handle_clipped_edge(scanline,x,e, x1,y1, x3,y3);
               } else if (x0 < x1 && x3 > x1) {  // two segments across x, down-right
                  stbtt__handle_clipped_edge(scanline,x,e, x0,y0, x1,y1);
                  stbtt__handle_clipped_edge(scanline,x,e, x1,y1, x3,y3);
               } else if (x3 < x1 && x0 > x1) {  // two segments across x, down-left
                  stbtt__handle_clipped_edge(scanline,x,e, x0,y0, x1,y1);
                  stbtt__handle_clipped_edge(scanline,x,e, x1,y1, x3,y3);
               } else if (x0 < x2 && x3 > x2) {  // two segments across x+1, down-right
                  stbtt__handle_clipped_edge(scanline,x,e, x0,y0, x2,y2);
                  stbtt__handle_clipped_edge(scanline,x,e, x2,y2, x3,y3);
               } else if (x3 < x2 && x0 > x2) {  // two segments across x+1, down-left
                  stbtt__handle_clipped_edge(scanline,x,e, x0,y0, x2,y2);
                  stbtt__handle_clipped_edge(scanline,x,e, x2,y2, x3,y3);
               } else {  // one segment
                  stbtt__handle_clipped_edge(scanline,x,e, x0,y0, x3,y3);
               }
            }
         }
      }
      e = e->next;
   }
}

// directly AA rasterize edges w/o supersampling
static void stbtt__rasterize_sorted_edges(stbtt__bitmap *result, stbtt__edge *e, int n, int vsubsample, int off_x, int off_y, void *userdata)
{
   stbtt__hheap hh = { 0, 0, 0 };
   stbtt__active_edge *active = NULL;
   int y,j=0, i;
   float scanline_data[129], *scanline, *scanline2;

   STBTT__NOTUSED(vsubsample);

   if (result->w > 64)
      scanline = (float *) STBTT_malloc((result->w*2+1) * sizeof(float), userdata);
   else
      scanline = scanline_data;

   scanline2 = scanline + result->w;

   y = off_y;
   e[n].y0 = (float) (off_y + result->h) + 1;

   while (j < result->h) {
      // find center of pixel for this scanline
      float scan_y_top    = y + 0.0f;
      float scan_y_bottom = y + 1.0f;
      stbtt__active_edge **step = &active;

      STBTT_memset(scanline , 0, result->w*sizeof(scanline[0]));
      STBTT_memset(scanline2, 0, (result->w+1)*sizeof(scanline[0]));

      // update all active edges;
      // remove all active edges that terminate before the top of this scanline
      while (*step) {
         stbtt__active_edge * z = *step;
         if (z->ey <= scan_y_top) {
            *step = z->next; // delete from list
            STBTT_assert(z->direction);
            z->direction = 0;
            stbtt__hheap_free(&hh, z);
         } else {
            step = &((*step)->next); // advance through list
         }
      }

      // insert all edges that start before the bottom of this scanline
      while (e->y0 <= scan_y_bottom) {
         if (e->y0 != e->y1) {
            stbtt__active_edge *z = stbtt__new_active(&hh, e, off_x, scan_y_top, userdata);
            if (z != NULL) {
               if (j == 0 && off_y != 0) {
                  if (z->ey < scan_y_top) {
                     // this can happen due to subpixel positioning and some kind of fp rounding error i think
                     z->ey = scan_y_top;
                  }
               }
               STBTT_assert(z->ey >= scan_y_top); // if we get really unlucky a tiny bit of an edge can be out of bounds
               // insert at front
               z->next = active;
               active = z;
            }
         }
         ++e;
      }

      // now process all active edges
      if (active)
         stbtt__fill_active_edges_new(scanline, scanline2+1, result->w, active, scan_y_top);

      {
         float sum = 0;
         for (i=0; i < result->w; ++i) {
            float k;
            int m;
            sum += scanline2[i];
            k = scanline[i] + sum;
            k = (float) STBTT_fabs(k)*255 + 0.5f;
            m = (int) k;
            if (m > 255) m = 255;
            result->pixels[j*result->stride + i] = (unsigned char) m;
         }
      }
      // advance all the edges
      step = &active;
      while (*step) {
         stbtt__active_edge *z = *step;
         z->fx += z->fdx; // advance to position for current scanline
         step = &((*step)->next); // advance through list
      }

      ++y;
      ++j;
   }

   stbtt__hheap_cleanup(&hh, userdata);

   if (scanline != scanline_data)
      STBTT_free(scanline, userdata);
}
#else
#error "Unrecognized value of STBTT_RASTERIZER_VERSION"
#endif

#define STBTT__COMPARE(a,b)  ((a)->y0 < (b)->y0)

static void stbtt__sort_edges_ins_sort(stbtt__edge *p, int n)
{
   int i,j;
   for (i=1; i < n; ++i) {
      stbtt__edge t = p[i], *a = &t;
      j = i;
      while (j > 0) {
         stbtt__edge *b = &p[j-1];
         int c = STBTT__COMPARE(a,b);
         if (!c) break;
         p[j] = p[j-1];
         --j;
      }
      if (i != j)
         p[j] = t;
   }
}

static void stbtt__sort_edges_quicksort(stbtt__edge *p, int n)
{
   /* threshold for transitioning to insertion sort */
   while (n > 12) {
      stbtt__edge t;
      int c01,c12,c,m,i,j;

      /* compute median of three */
      m = n >> 1;
      c01 = STBTT__COMPARE(&p[0],&p[m]);
      c12 = STBTT__COMPARE(&p[m],&p[n-1]);
      /* if 0 >= mid >= end, or 0 < mid < end, then use mid */
      if (c01 != c12) {
         /* otherwise, we'll need to swap something else to middle */
         int z;
         c = STBTT__COMPARE(&p[0],&p[n-1]);
         /* 0>mid && mid<n:  0>n => n; 0<n => 0 */
         /* 0<mid && mid>n:  0>n => 0; 0<n => n */
         z = (c == c12) ? 0 : n-1;
         t = p[z];
         p[z] = p[m];
         p[m] = t;
      }
      /* now p[m] is the median-of-three */
      /* swap it to the beginning so it won't move around */
      t = p[0];
      p[0] = p[m];
      p[m] = t;

      /* partition loop */
      i=1;
      j=n-1;
      for(;;) {
         /* handling of equality is crucial here */
         /* for sentinels & efficiency with duplicates */
         for (;;++i) {
            if (!STBTT__COMPARE(&p[i], &p[0])) break;
         }
         for (;;--j) {
            if (!STBTT__COMPARE(&p[0], &p[j])) break;
         }
         /* make sure we haven't crossed */
         if (i >= j) break;
         t = p[i];
         p[i] = p[j];
         p[j] = t;

         ++i;
         --j;
      }
      /* recurse on smaller side, iterate on larger */
      if (j < (n-i)) {
         stbtt__sort_edges_quicksort(p,j);
         p = p+i;
         n = n-i;
      } else {
         stbtt__sort_edges_quicksort(p+i, n-i);
         n = j;
      }
   }
}

static void stbtt__sort_edges(stbtt__edge *p, int n)
{
   stbtt__sort_edges_quicksort(p, n);
   stbtt__sort_edges_ins_sort(p, n);
}

typedef struct
{
   float x,y;
} stbtt__point;

static void stbtt__rasterize(stbtt__bitmap *result, stbtt__point *pts, int *wcount, int windings, float scale_x, float scale_y, float shift_x, float shift_y, int off_x, int off_y, int invert, void *userdata)
{
   float y_scale_inv = invert ? -scale_y : scale_y;
   stbtt__edge *e;
   int n,i,j,k,m;
#if STBTT_RASTERIZER_VERSION == 1
   int vsubsample = result->h < 8 ? 15 : 5;
#elif STBTT_RASTERIZER_VERSION == 2
   int vsubsample = 1;
#else
   #error "Unrecognized value of STBTT_RASTERIZER_VERSION"
#endif
   // vsubsample should divide 255 evenly; otherwise we won't reach full opacity

   // now we have to blow out the windings into explicit edge lists
   n = 0;
   for (i=0; i < windings; ++i)
      n += wcount[i];

   e = (stbtt__edge *) STBTT_malloc(sizeof(*e) * (n+1), userdata); // add an extra one as a sentinel
   if (e == 0) return;
   n = 0;

   m=0;
   for (i=0; i < windings; ++i) {
      stbtt__point *p = pts + m;
      m += wcount[i];
      j = wcount[i]-1;
      for (k=0; k < wcount[i]; j=k++) {
         int a=k,b=j;
         // skip the edge if horizontal
         if (p[j].y == p[k].y)
            continue;
         // add edge from j to k to the list
         e[n].invert = 0;
         if (invert ? p[j].y > p[k].y : p[j].y < p[k].y) {
            e[n].invert = 1;
            a=j,b=k;
         }
         e[n].x0 = p[a].x * scale_x + shift_x;
         e[n].y0 = (p[a].y * y_scale_inv + shift_y) * vsubsample;
         e[n].x1 = p[b].x * scale_x + shift_x;
         e[n].y1 = (p[b].y * y_scale_inv + shift_y) * vsubsample;
         ++n;
      }
   }

   // now sort the edges by their highest point (should snap to integer, and then by x)
   //STBTT_sort(e, n, sizeof(e[0]), stbtt__edge_compare);
   stbtt__sort_edges(e, n);

   // now, traverse the scanlines and find the intersections on each scanline, use xor winding rule
   stbtt__rasterize_sorted_edges(result, e, n, vsubsample, off_x, off_y, userdata);

   STBTT_free(e, userdata);
}

static void stbtt__add_point(stbtt__point *points, int n, float x, float y)
{
   if (!points) return; // during first pass, it's unallocated
   points[n].x = x;
   points[n].y = y;
}

// tessellate until threshold p is happy... @TODO warped to compensate for non-linear stretching
static int stbtt__tesselate_curve(stbtt__point *points, int *num_points, float x0, float y0, float x1, float y1, float x2, float y2, float objspace_flatness_squared, int n)
{
   // midpoint
   float mx = (x0 + 2*x1 + x2)/4;
   float my = (y0 + 2*y1 + y2)/4;
   // versus directly drawn line
   float dx = (x0+x2)/2 - mx;
   float dy = (y0+y2)/2 - my;
   if (n > 16) // 65536 segments on one curve better be enough!
      return 1;
   if (dx*dx+dy*dy > objspace_flatness_squared) { // half-pixel error allowed... need to be smaller if AA
      stbtt__tesselate_curve(points, num_points, x0,y0, (x0+x1)/2.0f,(y0+y1)/2.0f, mx,my, objspace_flatness_squared,n+1);
      stbtt__tesselate_curve(points, num_points, mx,my, (x1+x2)/2.0f,(y1+y2)/2.0f, x2,y2, objspace_flatness_squared,n+1);
   } else {
      stbtt__add_point(points, *num_points,x2,y2);
      *num_points = *num_points+1;
   }
   return 1;
}

static void stbtt__tesselate_cubic(stbtt__point *points, int *num_points, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, float objspace_flatness_squared, int n)
{
   // @TODO this "flatness" calculation is just made-up nonsense that seems to work well enough
   float dx0 = x1-x0;
   float dy0 = y1-y0;
   float dx1 = x2-x1;
   float dy1 = y2-y1;
   float dx2 = x3-x2;
   float dy2 = y3-y2;
   float dx = x3-x0;
   float dy = y3-y0;
   float longlen = (float) (STBTT_sqrt(dx0*dx0+dy0*dy0)+STBTT_sqrt(dx1*dx1+dy1*dy1)+STBTT_sqrt(dx2*dx2+dy2*dy2));
   float shortlen = (float) STBTT_sqrt(dx*dx+dy*dy);
   float flatness_squared = longlen*longlen-shortlen*shortlen;

   if (n > 16) // 65536 segments on one curve better be enough!
      return;

   if (flatness_squared > objspace_flatness_squared) {
      float x01 = (x0+x1)/2;
      float y01 = (y0+y1)/2;
      float x12 = (x1+x2)/2;
      float y12 = (y1+y2)/2;
      float x23 = (x2+x3)/2;
      float y23 = (y2+y3)/2;

      float xa = (x01+x12)/2;
      float ya = (y01+y12)/2;
      float xb = (x12+x23)/2;
      float yb = (y12+y23)/2;

      float mx = (xa+xb)/2;
      float my = (ya+yb)/2;

      stbtt__tesselate_cubic(points, num_points, x0,y0, x01,y01, xa,ya, mx,my, objspace_flatness_squared,n+1);
      stbtt__tesselate_cubic(points, num_points, mx,my, xb,yb, x23,y23, x3,y3, objspace_flatness_squared,n+1);
   } else {
      stbtt__add_point(points, *num_points,x3,y3);
      *num_points = *num_points+1;
   }
}

// returns number of contours
static stbtt__point *stbtt_FlattenCurves(stbtt_vertex *vertices, int num_verts, float objspace_flatness, int **contour_lengths, int *num_contours, void *userdata)
{
   stbtt__point *points=0;
   int num_points=0;

   float objspace_flatness_squared = objspace_flatness * objspace_flatness;
   int i,n=0,start=0, pass;

   // count how many "moves" there are to get the contour count
   for (i=0; i < num_verts; ++i)
      if (vertices[i].type == STBTT_vmove)
         ++n;

   *num_contours = n;
   if (n == 0) return 0;

   *contour_lengths = (int *) STBTT_malloc(sizeof(**contour_lengths) * n, userdata);

   if (*contour_lengths == 0) {
      *num_contours = 0;
      return 0;
   }

   // make two passes through the points so we don't need to realloc
   for (pass=0; pass < 2; ++pass) {
      float x=0,y=0;
      if (pass == 1) {
         points = (stbtt__point *) STBTT_malloc(num_points * sizeof(points[0]), userdata);
         if (points == NULL) goto error;
      }
      num_points = 0;
      n= -1;
      for (i=0; i < num_verts; ++i) {
         switch (vertices[i].type) {
            case STBTT_vmove:
               // start the next contour
               if (n >= 0)
                  (*contour_lengths)[n] = num_points - start;
               ++n;
               start = num_points;

               x = vertices[i].x, y = vertices[i].y;
               stbtt__add_point(points, num_points++, x,y);
               break;
            case STBTT_vline:
               x = vertices[i].x, y = vertices[i].y;
               stbtt__add_point(points, num_points++, x, y);
               break;
            case STBTT_vcurve:
               stbtt__tesselate_curve(points, &num_points, x,y,
                                        vertices[i].cx, vertices[i].cy,
                                        vertices[i].x,  vertices[i].y,
                                        objspace_flatness_squared, 0);
               x = vertices[i].x, y = vertices[i].y;
               break;
            case STBTT_vcubic:
               stbtt__tesselate_cubic(points, &num_points, x,y,
                                        vertices[i].cx, vertices[i].cy,
                                        vertices[i].cx1, vertices[i].cy1,
                                        vertices[i].x,  vertices[i].y,
                                        objspace_flatness_squared, 0);
               x = vertices[i].x, y = vertices[i].y;
               break;
         }
      }
      (*contour_lengths)[n] = num_points - start;
   }

   return points;
error:
   STBTT_free(points, userdata);
   STBTT_free(*contour_lengths, userdata);
   *contour_lengths = 0;
   *num_contours = 0;
   return NULL;
}

STBTT_DEF void stbtt_Rasterize(stbtt__bitmap *result, float flatness_in_pixels, stbtt_vertex *vertices, int num_verts, float scale_x, float scale_y, float shift_x, float shift_y, int x_off, int y_off, int invert, void *userdata)
{
   float scale            = scale_x > scale_y ? scale_y : scale_x;
   int winding_count      = 0;
   int *winding_lengths   = NULL;
   stbtt__point *windings = stbtt_FlattenCurves(vertices, num_verts, flatness_in_pixels / scale, &winding_lengths, &winding_count, userdata);
   if (windings) {
      stbtt__rasterize(result, windings, winding_lengths, winding_count, scale_x, scale_y, shift_x, shift_y, x_off, y_off, invert, userdata);
      STBTT_free(winding_lengths, userdata);
      STBTT_free(windings, userdata);
   }
}

STBTT_DEF void stbtt_FreeBitmap(unsigned char *bitmap, void *userdata)
{
   STBTT_free(bitmap, userdata);
}

STBTT_DEF unsigned char *stbtt_GetGlyphBitmapSubpixel(const stbtt_fontinfo *info, float scale_x, float scale_y, float shift_x, float shift_y, int glyph, int *width, int *height, int *xoff, int *yoff)
{
   int ix0,iy0,ix1,iy1;
   stbtt__bitmap gbm;
   stbtt_vertex *vertices;
   int num_verts = stbtt_GetGlyphShape(info, glyph, &vertices);

   if (scale_x == 0) scale_x = scale_y;
   if (scale_y == 0) {
      if (scale_x == 0) {
         STBTT_free(vertices, info->userdata);
         return NULL;
      }
      scale_y = scale_x;
   }

   stbtt_GetGlyphBitmapBoxSubpixel(info, glyph, scale_x, scale_y, shift_x, shift_y, &ix0,&iy0,&ix1,&iy1);

   // now we get the size
   gbm.w = (ix1 - ix0);
   gbm.h = (iy1 - iy0);
   gbm.pixels = NULL; // in case we error

   if (width ) *width  = gbm.w;
   if (height) *height = gbm.h;
   if (xoff  ) *xoff   = ix0;
   if (yoff  ) *yoff   = iy0;

   if (gbm.w && gbm.h) {
      gbm.pixels = (unsigned char *) STBTT_malloc(gbm.w * gbm.h, info->userdata);
      if (gbm.pixels) {
         gbm.stride = gbm.w;

         stbtt_Rasterize(&gbm, 0.35f, vertices, num_verts, scale_x, scale_y, shift_x, shift_y, ix0, iy0, 1, info->userdata);
      }
   }
   STBTT_free(vertices, info->userdata);
   return gbm.pixels;
}

STBTT_DEF unsigned char *stbtt_GetGlyphBitmap(const stbtt_fontinfo *info, float scale_x, float scale_y, int glyph, int *width, int *height, int *xoff, int *yoff)
{
   return stbtt_GetGlyphBitmapSubpixel(info, scale_x, scale_y, 0.0f, 0.0f, glyph, width, height, xoff, yoff);
}

STBTT_DEF void stbtt_MakeGlyphBitmapSubpixel(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, float shift_x, float shift_y, int glyph)
{
   int ix0,iy0;
   stbtt_vertex *vertices;
   int num_verts = stbtt_GetGlyphShape(info, glyph, &vertices);
   stbtt__bitmap gbm;

   stbtt_GetGlyphBitmapBoxSubpixel(info, glyph, scale_x, scale_y, shift_x, shift_y, &ix0,&iy0,0,0);
   gbm.pixels = output;
   gbm.w = out_w;
   gbm.h = out_h;
   gbm.stride = out_stride;

   if (gbm.w && gbm.h)
      stbtt_Rasterize(&gbm, 0.35f, vertices, num_verts, scale_x, scale_y, shift_x, shift_y, ix0,iy0, 1, info->userdata);

   STBTT_free(vertices, info->userdata);
}

STBTT_DEF void stbtt_MakeGlyphBitmap(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, int glyph)
{
   stbtt_MakeGlyphBitmapSubpixel(info, output, out_w, out_h, out_stride, scale_x, scale_y, 0.0f,0.0f, glyph);
}

STBTT_DEF unsigned char *stbtt_GetCodepointBitmapSubpixel(const stbtt_fontinfo *info, float scale_x, float scale_y, float shift_x, float shift_y, int codepoint, int *width, int *height, int *xoff, int *yoff)
{
   return stbtt_GetGlyphBitmapSubpixel(info, scale_x, scale_y,shift_x,shift_y, stbtt_FindGlyphIndex(info,codepoint), width,height,xoff,yoff);
}

STBTT_DEF void stbtt_MakeCodepointBitmapSubpixelPrefilter(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, float shift_x, float shift_y, int oversample_x, int oversample_y, float *sub_x, float *sub_y, int codepoint)
{
   stbtt_MakeGlyphBitmapSubpixelPrefilter(info, output, out_w, out_h, out_stride, scale_x, scale_y, shift_x, shift_y, oversample_x, oversample_y, sub_x, sub_y, stbtt_FindGlyphIndex(info,codepoint));
}

STBTT_DEF void stbtt_MakeCodepointBitmapSubpixel(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, float shift_x, float shift_y, int codepoint)
{
   stbtt_MakeGlyphBitmapSubpixel(info, output, out_w, out_h, out_stride, scale_x, scale_y, shift_x, shift_y, stbtt_FindGlyphIndex(info,codepoint));
}

STBTT_DEF unsigned char *stbtt_GetCodepointBitmap(const stbtt_fontinfo *info, float scale_x, float scale_y, int codepoint, int *width, int *height, int *xoff, int *yoff)
{
   return stbtt_GetCodepointBitmapSubpixel(info, scale_x, scale_y, 0.0f,0.0f, codepoint, width,height,xoff,yoff);
}

STBTT_DEF void stbtt_MakeCodepointBitmap(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, int codepoint)
{
   stbtt_MakeCodepointBitmapSubpixel(info, output, out_w, out_h, out_stride, scale_x, scale_y, 0.0f,0.0f, codepoint);
}

//////////////////////////////////////////////////////////////////////////////
//
// bitmap baking
//
// This is SUPER-CRAPPY packing to keep source code small

static int stbtt_BakeFontBitmap_internal(unsigned char *data, int offset,  // font location (use offset=0 for plain .ttf)
                                float pixel_height,                     // height of font in pixels
                                unsigned char *pixels, int pw, int ph,  // bitmap to be filled in
                                int first_char, int num_chars,          // characters to bake
                                stbtt_bakedchar *chardata)
{
   float scale;
   int x,y,bottom_y, i;
   stbtt_fontinfo f;
   f.userdata = NULL;
   if (!stbtt_InitFont(&f, data, offset))
      return -1;
   STBTT_memset(pixels, 0, pw*ph); // background of 0 around pixels
   x=y=1;
   bottom_y = 1;

   scale = stbtt_ScaleForPixelHeight(&f, pixel_height);

   for (i=0; i < num_chars; ++i) {
      int advance, lsb, x0,y0,x1,y1,gw,gh;
      int g = stbtt_FindGlyphIndex(&f, first_char + i);
      stbtt_GetGlyphHMetrics(&f, g, &advance, &lsb);
      stbtt_GetGlyphBitmapBox(&f, g, scale,scale, &x0,&y0,&x1,&y1);
      gw = x1-x0;
      gh = y1-y0;
      if (x + gw + 1 >= pw)
         y = bottom_y, x = 1; // advance to next row
      if (y + gh + 1 >= ph) // check if it fits vertically AFTER potentially moving to next row
         return -i;
      STBTT_assert(x+gw < pw);
      STBTT_assert(y+gh < ph);
      stbtt_MakeGlyphBitmap(&f, pixels+x+y*pw, gw,gh,pw, scale,scale, g);
      chardata[i].x0 = (stbtt_int16) x;
      chardata[i].y0 = (stbtt_int16) y;
      chardata[i].x1 = (stbtt_int16) (x + gw);
      chardata[i].y1 = (stbtt_int16) (y + gh);
      chardata[i].xadvance = scale * advance;
      chardata[i].xoff     = (float) x0;
      chardata[i].yoff     = (float) y0;
      x = x + gw + 1;
      if (y+gh+1 > bottom_y)
         bottom_y = y+gh+1;
   }
   return bottom_y;
}

STBTT_DEF void stbtt_GetBakedQuad(const stbtt_bakedchar *chardata, int pw, int ph, int char_index, float *xpos, float *ypos, stbtt_aligned_quad *q, int opengl_fillrule)
{
   float d3d_bias = opengl_fillrule ? 0 : -0.5f;
   float ipw = 1.0f / pw, iph = 1.0f / ph;
   const stbtt_bakedchar *b = chardata + char_index;
   int round_x = STBTT_ifloor((*xpos + b->xoff) + 0.5f);
   int round_y = STBTT_ifloor((*ypos + b->yoff) + 0.5f);

   q->x0 = round_x + d3d_bias;
   q->y0 = round_y + d3d_bias;
   q->x1 = round_x + b->x1 - b->x0 + d3d_bias;
   q->y1 = round_y + b->y1 - b->y0 + d3d_bias;

   q->s0 = b->x0 * ipw;
   q->t0 = b->y0 * iph;
   q->s1 = b->x1 * ipw;
   q->t1 = b->y1 * iph;

   *xpos += b->xadvance;
}

//////////////////////////////////////////////////////////////////////////////
//
// rectangle packing replacement routines if you don't have stb_rect_pack.h
//

#ifndef STB_RECT_PACK_VERSION

typedef int stbrp_coord;

////////////////////////////////////////////////////////////////////////////////////
//                                                                                //
//                                                                                //
// COMPILER WARNING ?!?!?                                                         //
//                                                                                //
//                                                                                //
// if you get a compile warning due to these symbols being defined more than      //
// once, move #include "stb_rect_pack.h" before #include "stb_truetype.h"         //
//                                                                                //
////////////////////////////////////////////////////////////////////////////////////

typedef struct
{
   int width,height;
   int x,y,bottom_y;
} stbrp_context;

typedef struct
{
   unsigned char x;
} stbrp_node;

struct stbrp_rect
{
   stbrp_coord x,y;
   int id,w,h,was_packed;
};

static void stbrp_init_target(stbrp_context *con, int pw, int ph, stbrp_node *nodes, int num_nodes)
{
   con->width  = pw;
   con->height = ph;
   con->x = 0;
   con->y = 0;
   con->bottom_y = 0;
   STBTT__NOTUSED(nodes);
   STBTT__NOTUSED(num_nodes);
}

static void stbrp_pack_rects(stbrp_context *con, stbrp_rect *rects, int num_rects)
{
   int i;
   for (i=0; i < num_rects; ++i) {
      if (con->x + rects[i].w > con->width) {
         con->x = 0;
         con->y = con->bottom_y;
      }
      if (con->y + rects[i].h > con->height)
         break;
      rects[i].x = con->x;
      rects[i].y = con->y;
      rects[i].was_packed = 1;
      con->x += rects[i].w;
      if (con->y + rects[i].h > con->bottom_y)
         con->bottom_y = con->y + rects[i].h;
   }
   for (   ; i < num_rects; ++i)
      rects[i].was_packed = 0;
}
#endif

//////////////////////////////////////////////////////////////////////////////
//
// bitmap baking
//
// This is SUPER-AWESOME (tm Ryan Gordon) packing using stb_rect_pack.h. If
// stb_rect_pack.h isn't available, it uses the BakeFontBitmap strategy.

STBTT_DEF int stbtt_PackBegin(stbtt_pack_context *spc, unsigned char *pixels, int pw, int ph, int stride_in_bytes, int padding, void *alloc_context)
{
   stbrp_context *context = (stbrp_context *) STBTT_malloc(sizeof(*context)            ,alloc_context);
   int            num_nodes = pw - padding;
   stbrp_node    *nodes   = (stbrp_node    *) STBTT_malloc(sizeof(*nodes  ) * num_nodes,alloc_context);

   if (context == NULL || nodes == NULL) {
      if (context != NULL) STBTT_free(context, alloc_context);
      if (nodes   != NULL) STBTT_free(nodes  , alloc_context);
      return 0;
   }

   spc->user_allocator_context = alloc_context;
   spc->width = pw;
   spc->height = ph;
   spc->pixels = pixels;
   spc->pack_info = context;
   spc->nodes = nodes;
   spc->padding = padding;
   spc->stride_in_bytes = stride_in_bytes != 0 ? stride_in_bytes : pw;
   spc->h_oversample = 1;
   spc->v_oversample = 1;
   spc->skip_missing = 0;

   stbrp_init_target(context, pw-padding, ph-padding, nodes, num_nodes);

   if (pixels)
      STBTT_memset(pixels, 0, pw*ph); // background of 0 around pixels

   return 1;
}

STBTT_DEF void stbtt_PackEnd  (stbtt_pack_context *spc)
{
   STBTT_free(spc->nodes    , spc->user_allocator_context);
   STBTT_free(spc->pack_info, spc->user_allocator_context);
}

STBTT_DEF void stbtt_PackSetOversampling(stbtt_pack_context *spc, unsigned int h_oversample, unsigned int v_oversample)
{
   STBTT_assert(h_oversample <= STBTT_MAX_OVERSAMPLE);
   STBTT_assert(v_oversample <= STBTT_MAX_OVERSAMPLE);
   if (h_oversample <= STBTT_MAX_OVERSAMPLE)
      spc->h_oversample = h_oversample;
   if (v_oversample <= STBTT_MAX_OVERSAMPLE)
      spc->v_oversample = v_oversample;
}

STBTT_DEF void stbtt_PackSetSkipMissingCodepoints(stbtt_pack_context *spc, int skip)
{
   spc->skip_missing = skip;
}

#define STBTT__OVER_MASK  (STBTT_MAX_OVERSAMPLE-1)

static void stbtt__h_prefilter(unsigned char *pixels, int w, int h, int stride_in_bytes, unsigned int kernel_width)
{
   unsigned char buffer[STBTT_MAX_OVERSAMPLE];
   int safe_w = w - kernel_width;
   int j;
   STBTT_memset(buffer, 0, STBTT_MAX_OVERSAMPLE); // suppress bogus warning from VS2013 -analyze
   for (j=0; j < h; ++j) {
      int i;
      unsigned int total;
      STBTT_memset(buffer, 0, kernel_width);

      total = 0;

      // make kernel_width a constant in common cases so compiler can optimize out the divide
      switch (kernel_width) {
         case 2:
            for (i=0; i <= safe_w; ++i) {
               total += pixels[i] - buffer[i & STBTT__OVER_MASK];
               buffer[(i+kernel_width) & STBTT__OVER_MASK] = pixels[i];
               pixels[i] = (unsigned char) (total / 2);
            }
            break;
         case 3:
            for (i=0; i <= safe_w; ++i) {
               total += pixels[i] - buffer[i & STBTT__OVER_MASK];
               buffer[(i+kernel_width) & STBTT__OVER_MASK] = pixels[i];
               pixels[i] = (unsigned char) (total / 3);
            }
            break;
         case 4:
            for (i=0; i <= safe_w; ++i) {
               total += pixels[i] - buffer[i & STBTT__OVER_MASK];
               buffer[(i+kernel_width) & STBTT__OVER_MASK] = pixels[i];
               pixels[i] = (unsigned char) (total / 4);
            }
            break;
         case 5:
            for (i=0; i <= safe_w; ++i) {
               total += pixels[i] - buffer[i & STBTT__OVER_MASK];
               buffer[(i+kernel_width) & STBTT__OVER_MASK] = pixels[i];
               pixels[i] = (unsigned char) (total / 5);
            }
            break;
         default:
            for (i=0; i <= safe_w; ++i) {
               total += pixels[i] - buffer[i & STBTT__OVER_MASK];
               buffer[(i+kernel_width) & STBTT__OVER_MASK] = pixels[i];
               pixels[i] = (unsigned char) (total / kernel_width);
            }
            break;
      }

      for (; i < w; ++i) {
         STBTT_assert(pixels[i] == 0);
         total -= buffer[i & STBTT__OVER_MASK];
         pixels[i] = (unsigned char) (total / kernel_width);
      }

      pixels += stride_in_bytes;
   }
}

static void stbtt__v_prefilter(unsigned char *pixels, int w, int h, int stride_in_bytes, unsigned int kernel_width)
{
   unsigned char buffer[STBTT_MAX_OVERSAMPLE];
   int safe_h = h - kernel_width;
   int j;
   STBTT_memset(buffer, 0, STBTT_MAX_OVERSAMPLE); // suppress bogus warning from VS2013 -analyze
   for (j=0; j < w; ++j) {
      int i;
      unsigned int total;
      STBTT_memset(buffer, 0, kernel_width);

      total = 0;

      // make kernel_width a constant in common cases so compiler can optimize out the divide
      switch (kernel_width) {
         case 2:
            for (i=0; i <= safe_h; ++i) {
               total += pixels[i*stride_in_bytes] - buffer[i & STBTT__OVER_MASK];
               buffer[(i+kernel_width) & STBTT__OVER_MASK] = pixels[i*stride_in_bytes];
               pixels[i*stride_in_bytes] = (unsigned char) (total / 2);
            }
            break;
         case 3:
            for (i=0; i <= safe_h; ++i) {
               total += pixels[i*stride_in_bytes] - buffer[i & STBTT__OVER_MASK];
               buffer[(i+kernel_width) & STBTT__OVER_MASK] = pixels[i*stride_in_bytes];
               pixels[i*stride_in_bytes] = (unsigned char) (total / 3);
            }
            break;
         case 4:
            for (i=0; i <= safe_h; ++i) {
               total += pixels[i*stride_in_bytes] - buffer[i & STBTT__OVER_MASK];
               buffer[(i+kernel_width) & STBTT__OVER_MASK] = pixels[i*stride_in_bytes];
               pixels[i*stride_in_bytes] = (unsigned char) (total / 4);
            }
            break;
         case 5:
            for (i=0; i <= safe_h; ++i) {
               total += pixels[i*stride_in_bytes] - buffer[i & STBTT__OVER_MASK];
               buffer[(i+kernel_width) & STBTT__OVER_MASK] = pixels[i*stride_in_bytes];
               pixels[i*stride_in_bytes] = (unsigned char) (total / 5);
            }
            break;
         default:
            for (i=0; i <= safe_h; ++i) {
               total += pixels[i*stride_in_bytes] - buffer[i & STBTT__OVER_MASK];
               buffer[(i+kernel_width) & STBTT__OVER_MASK] = pixels[i*stride_in_bytes];
               pixels[i*stride_in_bytes] = (unsigned char) (total / kernel_width);
            }
            break;
      }

      for (; i < h; ++i) {
         STBTT_assert(pixels[i*stride_in_bytes] == 0);
         total -= buffer[i & STBTT__OVER_MASK];
         pixels[i*stride_in_bytes] = (unsigned char) (total / kernel_width);
      }

      pixels += 1;
   }
}

static float stbtt__oversample_shift(int oversample)
{
   if (!oversample)
      return 0.0f;

   // The prefilter is a box filter of width "oversample",
   // which shifts phase by (oversample - 1)/2 pixels in
   // oversampled space. We want to shift in the opposite
   // direction to counter this.
   return (float)-(oversample - 1) / (2.0f * (float)oversample);
}

// rects array must be big enough to accommodate all characters in the given ranges
STBTT_DEF int stbtt_PackFontRangesGatherRects(stbtt_pack_context *spc, const stbtt_fontinfo *info, stbtt_pack_range *ranges, int num_ranges, stbrp_rect *rects)
{
   int i,j,k;
   int missing_glyph_added = 0;

   k=0;
   for (i=0; i < num_ranges; ++i) {
      float fh = ranges[i].font_size;
      float scale = fh > 0 ? stbtt_ScaleForPixelHeight(info, fh) : stbtt_ScaleForMappingEmToPixels(info, -fh);
      ranges[i].h_oversample = (unsigned char) spc->h_oversample;
      ranges[i].v_oversample = (unsigned char) spc->v_oversample;
      for (j=0; j < ranges[i].num_chars; ++j) {
         int x0,y0,x1,y1;
         int codepoint = ranges[i].array_of_unicode_codepoints == NULL ? ranges[i].first_unicode_codepoint_in_range + j : ranges[i].array_of_unicode_codepoints[j];
         int glyph = stbtt_FindGlyphIndex(info, codepoint);
         if (glyph == 0 && (spc->skip_missing || missing_glyph_added)) {
            rects[k].w = rects[k].h = 0;
         } else {
            stbtt_GetGlyphBitmapBoxSubpixel(info,glyph,
                                            scale * spc->h_oversample,
                                            scale * spc->v_oversample,
                                            0,0,
                                            &x0,&y0,&x1,&y1);
            rects[k].w = (stbrp_coord) (x1-x0 + spc->padding + spc->h_oversample-1);
            rects[k].h = (stbrp_coord) (y1-y0 + spc->padding + spc->v_oversample-1);
            if (glyph == 0)
               missing_glyph_added = 1;
         }
         ++k;
      }
   }

   return k;
}

STBTT_DEF void stbtt_MakeGlyphBitmapSubpixelPrefilter(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, float shift_x, float shift_y, int prefilter_x, int prefilter_y, float *sub_x, float *sub_y, int glyph)
{
   stbtt_MakeGlyphBitmapSubpixel(info,
                                 output,
                                 out_w - (prefilter_x - 1),
                                 out_h - (prefilter_y - 1),
                                 out_stride,
                                 scale_x,
                                 scale_y,
                                 shift_x,
                                 shift_y,
                                 glyph);

   if (prefilter_x > 1)
      stbtt__h_prefilter(output, out_w, out_h, out_stride, prefilter_x);

   if (prefilter_y > 1)
      stbtt__v_prefilter(output, out_w, out_h, out_stride, prefilter_y);

   *sub_x = stbtt__oversample_shift(prefilter_x);
   *sub_y = stbtt__oversample_shift(prefilter_y);
}

// rects array must be big enough to accommodate all characters in the given ranges
STBTT_DEF int stbtt_PackFontRangesRenderIntoRects(stbtt_pack_context *spc, const stbtt_fontinfo *info, stbtt_pack_range *ranges, int num_ranges, stbrp_rect *rects)
{
   int i,j,k, missing_glyph = -1, return_value = 1;

   // save current values
   int old_h_over = spc->h_oversample;
   int old_v_over = spc->v_oversample;

   k = 0;
   for (i=0; i < num_ranges; ++i) {
      float fh = ranges[i].font_size;
      float scale = fh > 0 ? stbtt_ScaleForPixelHeight(info, fh) : stbtt_ScaleForMappingEmToPixels(info, -fh);
      float recip_h,recip_v,sub_x,sub_y;
      spc->h_oversample = ranges[i].h_oversample;
      spc->v_oversample = ranges[i].v_oversample;
      recip_h = 1.0f / spc->h_oversample;
      recip_v = 1.0f / spc->v_oversample;
      sub_x = stbtt__oversample_shift(spc->h_oversample);
      sub_y = stbtt__oversample_shift(spc->v_oversample);
      for (j=0; j < ranges[i].num_chars; ++j) {
         stbrp_rect *r = &rects[k];
         if (r->was_packed && r->w != 0 && r->h != 0) {
            stbtt_packedchar *bc = &ranges[i].chardata_for_range[j];
            int advance, lsb, x0,y0,x1,y1;
            int codepoint = ranges[i].array_of_unicode_codepoints == NULL ? ranges[i].first_unicode_codepoint_in_range + j : ranges[i].array_of_unicode_codepoints[j];
            int glyph = stbtt_FindGlyphIndex(info, codepoint);
            stbrp_coord pad = (stbrp_coord) spc->padding;

            // pad on left and top
            r->x += pad;
            r->y += pad;
            r->w -= pad;
            r->h -= pad;
            stbtt_GetGlyphHMetrics(info, glyph, &advance, &lsb);
            stbtt_GetGlyphBitmapBox(info, glyph,
                                    scale * spc->h_oversample,
                                    scale * spc->v_oversample,
                                    &x0,&y0,&x1,&y1);
            stbtt_MakeGlyphBitmapSubpixel(info,
                                          spc->pixels + r->x + r->y*spc->stride_in_bytes,
                                          r->w - spc->h_oversample+1,
                                          r->h - spc->v_oversample+1,
                                          spc->stride_in_bytes,
                                          scale * spc->h_oversample,
                                          scale * spc->v_oversample,
                                          0,0,
                                          glyph);

            if (spc->h_oversample > 1)
               stbtt__h_prefilter(spc->pixels + r->x + r->y*spc->stride_in_bytes,
                                  r->w, r->h, spc->stride_in_bytes,
                                  spc->h_oversample);

            if (spc->v_oversample > 1)
               stbtt__v_prefilter(spc->pixels + r->x + r->y*spc->stride_in_bytes,
                                  r->w, r->h, spc->stride_in_bytes,
                                  spc->v_oversample);

            bc->x0       = (stbtt_int16)  r->x;
            bc->y0       = (stbtt_int16)  r->y;
            bc->x1       = (stbtt_int16) (r->x + r->w);
            bc->y1       = (stbtt_int16) (r->y + r->h);
            bc->xadvance =                scale * advance;
            bc->xoff     =       (float)  x0 * recip_h + sub_x;
            bc->yoff     =       (float)  y0 * recip_v + sub_y;
            bc->xoff2    =                (x0 + r->w) * recip_h + sub_x;
            bc->yoff2    =                (y0 + r->h) * recip_v + sub_y;

            if (glyph == 0)
               missing_glyph = j;
         } else if (spc->skip_missing) {
            return_value = 0;
         } else if (r->was_packed && r->w == 0 && r->h == 0 && missing_glyph >= 0) {
            ranges[i].chardata_for_range[j] = ranges[i].chardata_for_range[missing_glyph];
         } else {
            return_value = 0; // if any fail, report failure
         }

         ++k;
      }
   }

   // restore original values
   spc->h_oversample = old_h_over;
   spc->v_oversample = old_v_over;

   return return_value;
}

STBTT_DEF void stbtt_PackFontRangesPackRects(stbtt_pack_context *spc, stbrp_rect *rects, int num_rects)
{
   stbrp_pack_rects((stbrp_context *) spc->pack_info, rects, num_rects);
}

STBTT_DEF int stbtt_PackFontRanges(stbtt_pack_context *spc, const unsigned char *fontdata, int font_index, stbtt_pack_range *ranges, int num_ranges)
{
   stbtt_fontinfo info;
   int i,j,n, return_value = 1;
   //stbrp_context *context = (stbrp_context *) spc->pack_info;
   stbrp_rect    *rects;

   // flag all characters as NOT packed
   for (i=0; i < num_ranges; ++i)
      for (j=0; j < ranges[i].num_chars; ++j)
         ranges[i].chardata_for_range[j].x0 =
         ranges[i].chardata_for_range[j].y0 =
         ranges[i].chardata_for_range[j].x1 =
         ranges[i].chardata_for_range[j].y1 = 0;

   n = 0;
   for (i=0; i < num_ranges; ++i)
      n += ranges[i].num_chars;

   rects = (stbrp_rect *) STBTT_malloc(sizeof(*rects) * n, spc->user_allocator_context);
   if (rects == NULL)
      return 0;

   info.userdata = spc->user_allocator_context;
   stbtt_InitFont(&info, fontdata, stbtt_GetFontOffsetForIndex(fontdata,font_index));

   n = stbtt_PackFontRangesGatherRects(spc, &info, ranges, num_ranges, rects);

   stbtt_PackFontRangesPackRects(spc, rects, n);

   return_value = stbtt_PackFontRangesRenderIntoRects(spc, &info, ranges, num_ranges, rects);

   STBTT_free(rects, spc->user_allocator_context);
   return return_value;
}

STBTT_DEF int stbtt_PackFontRange(stbtt_pack_context *spc, const unsigned char *fontdata, int font_index, float font_size,
            int first_unicode_codepoint_in_range, int num_chars_in_range, stbtt_packedchar *chardata_for_range)
{
   stbtt_pack_range range;
   range.first_unicode_codepoint_in_range = first_unicode_codepoint_in_range;
   range.array_of_unicode_codepoints = NULL;
   range.num_chars                   = num_chars_in_range;
   range.chardata_for_range          = chardata_for_range;
   range.font_size                   = font_size;
   return stbtt_PackFontRanges(spc, fontdata, font_index, &range, 1);
}

STBTT_DEF void stbtt_GetScaledFontVMetrics(const unsigned char *fontdata, int index, float size, float *ascent, float *descent, float *lineGap)
{
   int i_ascent, i_descent, i_lineGap;
   float scale;
   stbtt_fontinfo info;
   stbtt_InitFont(&info, fontdata, stbtt_GetFontOffsetForIndex(fontdata, index));
   scale = size > 0 ? stbtt_ScaleForPixelHeight(&info, size) : stbtt_ScaleForMappingEmToPixels(&info, -size);
   stbtt_GetFontVMetrics(&info, &i_ascent, &i_descent, &i_lineGap);
   *ascent  = (float) i_ascent  * scale;
   *descent = (float) i_descent * scale;
   *lineGap = (float) i_lineGap * scale;
}

STBTT_DEF void stbtt_GetPackedQuad(const stbtt_packedchar *chardata, int pw, int ph, int char_index, float *xpos, float *ypos, stbtt_aligned_quad *q, int align_to_integer)
{
   float ipw = 1.0f / pw, iph = 1.0f / ph;
   const stbtt_packedchar *b = chardata + char_index;

   if (align_to_integer) {
      float x = (float) STBTT_ifloor((*xpos + b->xoff) + 0.5f);
      float y = (float) STBTT_ifloor((*ypos + b->yoff) + 0.5f);
      q->x0 = x;
      q->y0 = y;
      q->x1 = x + b->xoff2 - b->xoff;
      q->y1 = y + b->yoff2 - b->yoff;
   } else {
      q->x0 = *xpos + b->xoff;
      q->y0 = *ypos + b->yoff;
      q->x1 = *xpos + b->xoff2;
      q->y1 = *ypos + b->yoff2;
   }

   q->s0 = b->x0 * ipw;
   q->t0 = b->y0 * iph;
   q->s1 = b->x1 * ipw;
   q->t1 = b->y1 * iph;

   *xpos += b->xadvance;
}

//////////////////////////////////////////////////////////////////////////////
//
// sdf computation
//

#define STBTT_min(a,b)  ((a) < (b) ? (a) : (b))
#define STBTT_max(a,b)  ((a) < (b) ? (b) : (a))

static int stbtt__ray_intersect_bezier(float orig[2], float ray[2], float q0[2], float q1[2], float q2[2], float hits[2][2])
{
   float q0perp = q0[1]*ray[0] - q0[0]*ray[1];
   float q1perp = q1[1]*ray[0] - q1[0]*ray[1];
   float q2perp = q2[1]*ray[0] - q2[0]*ray[1];
   float roperp = orig[1]*ray[0] - orig[0]*ray[1];

   float a = q0perp - 2*q1perp + q2perp;
   float b = q1perp - q0perp;
   float c = q0perp - roperp;

   float s0 = 0., s1 = 0.;
   int num_s = 0;

   if (a != 0.0) {
      float discr = b*b - a*c;
      if (discr > 0.0) {
         float rcpna = -1 / a;
         float d = (float) STBTT_sqrt(discr);
         s0 = (b+d) * rcpna;
         s1 = (b-d) * rcpna;
         if (s0 >= 0.0 && s0 <= 1.0)
            num_s = 1;
         if (d > 0.0 && s1 >= 0.0 && s1 <= 1.0) {
            if (num_s == 0) s0 = s1;
            ++num_s;
         }
      }
   } else {
      // 2*b*s + c = 0
      // s = -c / (2*b)
      s0 = c / (-2 * b);
      if (s0 >= 0.0 && s0 <= 1.0)
         num_s = 1;
   }

   if (num_s == 0)
      return 0;
   else {
      float rcp_len2 = 1 / (ray[0]*ray[0] + ray[1]*ray[1]);
      float rayn_x = ray[0] * rcp_len2, rayn_y = ray[1] * rcp_len2;

      float q0d =   q0[0]*rayn_x +   q0[1]*rayn_y;
      float q1d =   q1[0]*rayn_x +   q1[1]*rayn_y;
      float q2d =   q2[0]*rayn_x +   q2[1]*rayn_y;
      float rod = orig[0]*rayn_x + orig[1]*rayn_y;

      float q10d = q1d - q0d;
      float q20d = q2d - q0d;
      float q0rd = q0d - rod;

      hits[0][0] = q0rd + s0*(2.0f - 2.0f*s0)*q10d + s0*s0*q20d;
      hits[0][1] = a*s0+b;

      if (num_s > 1) {
         hits[1][0] = q0rd + s1*(2.0f - 2.0f*s1)*q10d + s1*s1*q20d;
         hits[1][1] = a*s1+b;
         return 2;
      } else {
         return 1;
      }
   }
}

static int equal(float *a, float *b)
{
   return (a[0] == b[0] && a[1] == b[1]);
}

static int stbtt__compute_crossings_x(float x, float y, int nverts, stbtt_vertex *verts)
{
   int i;
   float orig[2], ray[2] = { 1, 0 };
   float y_frac;
   int winding = 0;

   // make sure y never passes through a vertex of the shape
   y_frac = (float) STBTT_fmod(y, 1.0f);
   if (y_frac < 0.01f)
      y += 0.01f;
   else if (y_frac > 0.99f)
      y -= 0.01f;

   orig[0] = x;
   orig[1] = y;

   // test a ray from (-infinity,y) to (x,y)
   for (i=0; i < nverts; ++i) {
      if (verts[i].type == STBTT_vline) {
         int x0 = (int) verts[i-1].x, y0 = (int) verts[i-1].y;
         int x1 = (int) verts[i  ].x, y1 = (int) verts[i  ].y;
         if (y > STBTT_min(y0,y1) && y < STBTT_max(y0,y1) && x > STBTT_min(x0,x1)) {
            float x_inter = (y - y0) / (y1 - y0) * (x1-x0) + x0;
            if (x_inter < x)
               winding += (y0 < y1) ? 1 : -1;
         }
      }
      if (verts[i].type == STBTT_vcurve) {
         int x0 = (int) verts[i-1].x , y0 = (int) verts[i-1].y ;
         int x1 = (int) verts[i  ].cx, y1 = (int) verts[i  ].cy;
         int x2 = (int) verts[i  ].x , y2 = (int) verts[i  ].y ;
         int ax = STBTT_min(x0,STBTT_min(x1,x2)), ay = STBTT_min(y0,STBTT_min(y1,y2));
         int by = STBTT_max(y0,STBTT_max(y1,y2));
         if (y > ay && y < by && x > ax) {
            float q0[2],q1[2],q2[2];
            float hits[2][2];
            q0[0] = (float)x0;
            q0[1] = (float)y0;
            q1[0] = (float)x1;
            q1[1] = (float)y1;
            q2[0] = (float)x2;
            q2[1] = (float)y2;
            if (equal(q0,q1) || equal(q1,q2)) {
               x0 = (int)verts[i-1].x;
               y0 = (int)verts[i-1].y;
               x1 = (int)verts[i  ].x;
               y1 = (int)verts[i  ].y;
               if (y > STBTT_min(y0,y1) && y < STBTT_max(y0,y1) && x > STBTT_min(x0,x1)) {
                  float x_inter = (y - y0) / (y1 - y0) * (x1-x0) + x0;
                  if (x_inter < x)
                     winding += (y0 < y1) ? 1 : -1;
               }
            } else {
               int num_hits = stbtt__ray_intersect_bezier(orig, ray, q0, q1, q2, hits);
               if (num_hits >= 1)
                  if (hits[0][0] < 0)
                     winding += (hits[0][1] < 0 ? -1 : 1);
               if (num_hits >= 2)
                  if (hits[1][0] < 0)
                     winding += (hits[1][1] < 0 ? -1 : 1);
            }
         }
      }
   }
   return winding;
}

static float stbtt__cuberoot( float x )
{
   if (x<0)
      return -(float) STBTT_pow(-x,1.0f/3.0f);
   else
      return  (float) STBTT_pow( x,1.0f/3.0f);
}

// x^3 + a*x^2 + b*x + c = 0
static int stbtt__solve_cubic(float a, float b, float c, float* r)
{
   float s = -a / 3;
   float p = b - a*a / 3;
   float q = a * (2*a*a - 9*b) / 27 + c;
   float p3 = p*p*p;
   float d = q*q + 4*p3 / 27;
   if (d >= 0) {
      float z = (float) STBTT_sqrt(d);
      float u = (-q + z) / 2;
      float v = (-q - z) / 2;
      u = stbtt__cuberoot(u);
      v = stbtt__cuberoot(v);
      r[0] = s + u + v;
      return 1;
   } else {
      float u = (float) STBTT_sqrt(-p/3);
      float v = (float) STBTT_acos(-STBTT_sqrt(-27/p3) * q / 2) / 3; // p3 must be negative, since d is negative
      float m = (float) STBTT_cos(v);
      float n = (float) STBTT_cos(v-3.141592/2)*1.732050808f;
      r[0] = s + u * 2 * m;
      r[1] = s - u * (m + n);
      r[2] = s - u * (m - n);

      //STBTT_assert( STBTT_fabs(((r[0]+a)*r[0]+b)*r[0]+c) < 0.05f);  // these asserts may not be safe at all scales, though they're in bezier t parameter units so maybe?
      //STBTT_assert( STBTT_fabs(((r[1]+a)*r[1]+b)*r[1]+c) < 0.05f);
      //STBTT_assert( STBTT_fabs(((r[2]+a)*r[2]+b)*r[2]+c) < 0.05f);
      return 3;
   }
}

STBTT_DEF unsigned char * stbtt_GetGlyphSDF(const stbtt_fontinfo *info, float scale, int glyph, int padding, unsigned char onedge_value, float pixel_dist_scale, int *width, int *height, int *xoff, int *yoff)
{
   float scale_x = scale, scale_y = scale;
   int ix0,iy0,ix1,iy1;
   int w,h;
   unsigned char *data;

   if (scale == 0) return NULL;

   stbtt_GetGlyphBitmapBoxSubpixel(info, glyph, scale, scale, 0.0f,0.0f, &ix0,&iy0,&ix1,&iy1);

   // if empty, return NULL
   if (ix0 == ix1 || iy0 == iy1)
      return NULL;

   ix0 -= padding;
   iy0 -= padding;
   ix1 += padding;
   iy1 += padding;

   w = (ix1 - ix0);
   h = (iy1 - iy0);

   if (width ) *width  = w;
   if (height) *height = h;
   if (xoff  ) *xoff   = ix0;
   if (yoff  ) *yoff   = iy0;

   // invert for y-downwards bitmaps
   scale_y = -scale_y;

   {
      // distance from singular values (in the same units as the pixel grid)
      const float eps = 1./1024, eps2 = eps*eps;
      int x,y,i,j;
      float *precompute;
      stbtt_vertex *verts;
      int num_verts = stbtt_GetGlyphShape(info, glyph, &verts);
      data = (unsigned char *) STBTT_malloc(w * h, info->userdata);
      precompute = (float *) STBTT_malloc(num_verts * sizeof(float), info->userdata);

      for (i=0,j=num_verts-1; i < num_verts; j=i++) {
         if (verts[i].type == STBTT_vline) {
            float x0 = verts[i].x*scale_x, y0 = verts[i].y*scale_y;
            float x1 = verts[j].x*scale_x, y1 = verts[j].y*scale_y;
            float dist = (float) STBTT_sqrt((x1-x0)*(x1-x0) + (y1-y0)*(y1-y0));
            precompute[i] = (dist < eps) ? 0.0f : 1.0f / dist;
         } else if (verts[i].type == STBTT_vcurve) {
            float x2 = verts[j].x *scale_x, y2 = verts[j].y *scale_y;
            float x1 = verts[i].cx*scale_x, y1 = verts[i].cy*scale_y;
            float x0 = verts[i].x *scale_x, y0 = verts[i].y *scale_y;
            float bx = x0 - 2*x1 + x2, by = y0 - 2*y1 + y2;
            float len2 = bx*bx + by*by;
            if (len2 >= eps2)
               precompute[i] = 1.0f / len2;
            else
               precompute[i] = 0.0f;
         } else
            precompute[i] = 0.0f;
      }

      for (y=iy0; y < iy1; ++y) {
         for (x=ix0; x < ix1; ++x) {
            float val;
            float min_dist = 999999.0f;
            float sx = (float) x + 0.5f;
            float sy = (float) y + 0.5f;
            float x_gspace = (sx / scale_x);
            float y_gspace = (sy / scale_y);

            int winding = stbtt__compute_crossings_x(x_gspace, y_gspace, num_verts, verts); // @OPTIMIZE: this could just be a rasterization, but needs to be line vs. non-tesselated curves so a new path

            for (i=0; i < num_verts; ++i) {
               float x0 = verts[i].x*scale_x, y0 = verts[i].y*scale_y;

               if (verts[i].type == STBTT_vline && precompute[i] != 0.0f) {
                  float x1 = verts[i-1].x*scale_x, y1 = verts[i-1].y*scale_y;

                  float dist,dist2 = (x0-sx)*(x0-sx) + (y0-sy)*(y0-sy);
                  if (dist2 < min_dist*min_dist)
                     min_dist = (float) STBTT_sqrt(dist2);

                  // coarse culling against bbox
                  //if (sx > STBTT_min(x0,x1)-min_dist && sx < STBTT_max(x0,x1)+min_dist &&
                  //    sy > STBTT_min(y0,y1)-min_dist && sy < STBTT_max(y0,y1)+min_dist)
                  dist = (float) STBTT_fabs((x1-x0)*(y0-sy) - (y1-y0)*(x0-sx)) * precompute[i];
                  STBTT_assert(i != 0);
                  if (dist < min_dist) {
                     // check position along line
                     // x' = x0 + t*(x1-x0), y' = y0 + t*(y1-y0)
                     // minimize (x'-sx)*(x'-sx)+(y'-sy)*(y'-sy)
                     float dx = x1-x0, dy = y1-y0;
                     float px = x0-sx, py = y0-sy;
                     // minimize (px+t*dx)^2 + (py+t*dy)^2 = px*px + 2*px*dx*t + t^2*dx*dx + py*py + 2*py*dy*t + t^2*dy*dy
                     // derivative: 2*px*dx + 2*py*dy + (2*dx*dx+2*dy*dy)*t, set to 0 and solve
                     float t = -(px*dx + py*dy) / (dx*dx + dy*dy);
                     if (t >= 0.0f && t <= 1.0f)
                        min_dist = dist;
                  }
               } else if (verts[i].type == STBTT_vcurve) {
                  float x2 = verts[i-1].x *scale_x, y2 = verts[i-1].y *scale_y;
                  float x1 = verts[i  ].cx*scale_x, y1 = verts[i  ].cy*scale_y;
                  float box_x0 = STBTT_min(STBTT_min(x0,x1),x2);
                  float box_y0 = STBTT_min(STBTT_min(y0,y1),y2);
                  float box_x1 = STBTT_max(STBTT_max(x0,x1),x2);
                  float box_y1 = STBTT_max(STBTT_max(y0,y1),y2);
                  // coarse culling against bbox to avoid computing cubic unnecessarily
                  if (sx > box_x0-min_dist && sx < box_x1+min_dist && sy > box_y0-min_dist && sy < box_y1+min_dist) {
                     int num=0;
                     float ax = x1-x0, ay = y1-y0;
                     float bx = x0 - 2*x1 + x2, by = y0 - 2*y1 + y2;
                     float mx = x0 - sx, my = y0 - sy;
                     float res[3] = {0.f,0.f,0.f};
                     float px,py,t,it,dist2;
                     float a_inv = precompute[i];
                     if (a_inv == 0.0) { // if a_inv is 0, it's 2nd degree so use quadratic formula
                        float a = 3*(ax*bx + ay*by);
                        float b = 2*(ax*ax + ay*ay) + (mx*bx+my*by);
                        float c = mx*ax+my*ay;
                        if (STBTT_fabs(a) < eps2) { // if a is 0, it's linear
                           if (STBTT_fabs(b) >= eps2) {
                              res[num++] = -c/b;
                           }
                        } else {
                           float discriminant = b*b - 4*a*c;
                           if (discriminant < 0)
                              num = 0;
                           else {
                              float root = (float) STBTT_sqrt(discriminant);
                              res[0] = (-b - root)/(2*a);
                              res[1] = (-b + root)/(2*a);
                              num = 2; // don't bother distinguishing 1-solution case, as code below will still work
                           }
                        }
                     } else {
                        float b = 3*(ax*bx + ay*by) * a_inv; // could precompute this as it doesn't depend on sample point
                        float c = (2*(ax*ax + ay*ay) + (mx*bx+my*by)) * a_inv;
                        float d = (mx*ax+my*ay) * a_inv;
                        num = stbtt__solve_cubic(b, c, d, res);
                     }
                     dist2 = (x0-sx)*(x0-sx) + (y0-sy)*(y0-sy);
                     if (dist2 < min_dist*min_dist)
                        min_dist = (float) STBTT_sqrt(dist2);

                     if (num >= 1 && res[0] >= 0.0f && res[0] <= 1.0f) {
                        t = res[0], it = 1.0f - t;
                        px = it*it*x0 + 2*t*it*x1 + t*t*x2;
                        py = it*it*y0 + 2*t*it*y1 + t*t*y2;
                        dist2 = (px-sx)*(px-sx) + (py-sy)*(py-sy);
                        if (dist2 < min_dist * min_dist)
                           min_dist = (float) STBTT_sqrt(dist2);
                     }
                     if (num >= 2 && res[1] >= 0.0f && res[1] <= 1.0f) {
                        t = res[1], it = 1.0f - t;
                        px = it*it*x0 + 2*t*it*x1 + t*t*x2;
                        py = it*it*y0 + 2*t*it*y1 + t*t*y2;
                        dist2 = (px-sx)*(px-sx) + (py-sy)*(py-sy);
                        if (dist2 < min_dist * min_dist)
                           min_dist = (float) STBTT_sqrt(dist2);
                     }
                     if (num >= 3 && res[2] >= 0.0f && res[2] <= 1.0f) {
                        t = res[2], it = 1.0f - t;
                        px = it*it*x0 + 2*t*it*x1 + t*t*x2;
                        py = it*it*y0 + 2*t*it*y1 + t*t*y2;
                        dist2 = (px-sx)*(px-sx) + (py-sy)*(py-sy);
                        if (dist2 < min_dist * min_dist)
                           min_dist = (float) STBTT_sqrt(dist2);
                     }
                  }
               }
            }
            if (winding == 0)
               min_dist = -min_dist;  // if outside the shape, value is negative
            val = onedge_value + pixel_dist_scale * min_dist;
            if (val < 0)
               val = 0;
            else if (val > 255)
               val = 255;
            data[(y-iy0)*w+(x-ix0)] = (unsigned char) val;
         }
      }
      STBTT_free(precompute, info->userdata);
      STBTT_free(verts, info->userdata);
   }
   return data;
}

STBTT_DEF unsigned char * stbtt_GetCodepointSDF(const stbtt_fontinfo *info, float scale, int codepoint, int padding, unsigned char onedge_value, float pixel_dist_scale, int *width, int *height, int *xoff, int *yoff)
{
   return stbtt_GetGlyphSDF(info, scale, stbtt_FindGlyphIndex(info, codepoint), padding, onedge_value, pixel_dist_scale, width, height, xoff, yoff);
}

STBTT_DEF void stbtt_FreeSDF(unsigned char *bitmap, void *userdata)
{
   STBTT_free(bitmap, userdata);
}

//////////////////////////////////////////////////////////////////////////////
//
// font name matching -- recommended not to use this
//

// check if a utf8 string contains a prefix which is the utf16 string; if so return length of matching utf8 string
static stbtt_int32 stbtt__CompareUTF8toUTF16_bigendian_prefix(stbtt_uint8 *s1, stbtt_int32 len1, stbtt_uint8 *s2, stbtt_int32 len2)
{
   stbtt_int32 i=0;

   // convert utf16 to utf8 and compare the results while converting
   while (len2) {
      stbtt_uint16 ch = s2[0]*256 + s2[1];
      if (ch < 0x80) {
         if (i >= len1) return -1;
         if (s1[i++] != ch) return -1;
      } else if (ch < 0x800) {
         if (i+1 >= len1) return -1;
         if (s1[i++] != 0xc0 + (ch >> 6)) return -1;
         if (s1[i++] != 0x80 + (ch & 0x3f)) return -1;
      } else if (ch >= 0xd800 && ch < 0xdc00) {
         stbtt_uint32 c;
         stbtt_uint16 ch2 = s2[2]*256 + s2[3];
         if (i+3 >= len1) return -1;
         c = ((ch - 0xd800) << 10) + (ch2 - 0xdc00) + 0x10000;
         if (s1[i++] != 0xf0 + (c >> 18)) return -1;
         if (s1[i++] != 0x80 + ((c >> 12) & 0x3f)) return -1;
         if (s1[i++] != 0x80 + ((c >>  6) & 0x3f)) return -1;
         if (s1[i++] != 0x80 + ((c      ) & 0x3f)) return -1;
         s2 += 2; // plus another 2 below
         len2 -= 2;
      } else if (ch >= 0xdc00 && ch < 0xe000) {
         return -1;
      } else {
         if (i+2 >= len1) return -1;
         if (s1[i++] != 0xe0 + (ch >> 12)) return -1;
         if (s1[i++] != 0x80 + ((ch >> 6) & 0x3f)) return -1;
         if (s1[i++] != 0x80 + ((ch     ) & 0x3f)) return -1;
      }
      s2 += 2;
      len2 -= 2;
   }
   return i;
}

static int stbtt_CompareUTF8toUTF16_bigendian_internal(char *s1, int len1, char *s2, int len2)
{
   return len1 == stbtt__CompareUTF8toUTF16_bigendian_prefix((stbtt_uint8*) s1, len1, (stbtt_uint8*) s2, len2);
}

// returns results in whatever encoding you request... but note that 2-byte encodings
// will be BIG-ENDIAN... use stbtt_CompareUTF8toUTF16_bigendian() to compare
STBTT_DEF const char *stbtt_GetFontNameString(const stbtt_fontinfo *font, int *length, int platformID, int encodingID, int languageID, int nameID)
{
   stbtt_int32 i,count,stringOffset;
   stbtt_uint8 *fc = font->data;
   stbtt_uint32 offset = font->fontstart;
   stbtt_uint32 nm = stbtt__find_table(fc, offset, "name");
   if (!nm) return NULL;

   count = ttUSHORT(fc+nm+2);
   stringOffset = nm + ttUSHORT(fc+nm+4);
   for (i=0; i < count; ++i) {
      stbtt_uint32 loc = nm + 6 + 12 * i;
      if (platformID == ttUSHORT(fc+loc+0) && encodingID == ttUSHORT(fc+loc+2)
          && languageID == ttUSHORT(fc+loc+4) && nameID == ttUSHORT(fc+loc+6)) {
         *length = ttUSHORT(fc+loc+8);
         return (const char *) (fc+stringOffset+ttUSHORT(fc+loc+10));
      }
   }
   return NULL;
}

static int stbtt__matchpair(stbtt_uint8 *fc, stbtt_uint32 nm, stbtt_uint8 *name, stbtt_int32 nlen, stbtt_int32 target_id, stbtt_int32 next_id)
{
   stbtt_int32 i;
   stbtt_int32 count = ttUSHORT(fc+nm+2);
   stbtt_int32 stringOffset = nm + ttUSHORT(fc+nm+4);

   for (i=0; i < count; ++i) {
      stbtt_uint32 loc = nm + 6 + 12 * i;
      stbtt_int32 id = ttUSHORT(fc+loc+6);
      if (id == target_id) {
         // find the encoding
         stbtt_int32 platform = ttUSHORT(fc+loc+0), encoding = ttUSHORT(fc+loc+2), language = ttUSHORT(fc+loc+4);

         // is this a Unicode encoding?
         if (platform == 0 || (platform == 3 && encoding == 1) || (platform == 3 && encoding == 10)) {
            stbtt_int32 slen = ttUSHORT(fc+loc+8);
            stbtt_int32 off = ttUSHORT(fc+loc+10);

            // check if there's a prefix match
            stbtt_int32 matchlen = stbtt__CompareUTF8toUTF16_bigendian_prefix(name, nlen, fc+stringOffset+off,slen);
            if (matchlen >= 0) {
               // check for target_id+1 immediately following, with same encoding & language
               if (i+1 < count && ttUSHORT(fc+loc+12+6) == next_id && ttUSHORT(fc+loc+12) == platform && ttUSHORT(fc+loc+12+2) == encoding && ttUSHORT(fc+loc+12+4) == language) {
                  slen = ttUSHORT(fc+loc+12+8);
                  off = ttUSHORT(fc+loc+12+10);
                  if (slen == 0) {
                     if (matchlen == nlen)
                        return 1;
                  } else if (matchlen < nlen && name[matchlen] == ' ') {
                     ++matchlen;
                     if (stbtt_CompareUTF8toUTF16_bigendian_internal((char*) (name+matchlen), nlen-matchlen, (char*)(fc+stringOffset+off),slen))
                        return 1;
                  }
               } else {
                  // if nothing immediately following
                  if (matchlen == nlen)
                     return 1;
               }
            }
         }

         // @TODO handle other encodings
      }
   }
   return 0;
}

static int stbtt__matches(stbtt_uint8 *fc, stbtt_uint32 offset, stbtt_uint8 *name, stbtt_int32 flags)
{
   stbtt_int32 nlen = (stbtt_int32) STBTT_strlen((char *) name);
   stbtt_uint32 nm,hd;
   if (!stbtt__isfont(fc+offset)) return 0;

   // check italics/bold/underline flags in macStyle...
   if (flags) {
      hd = stbtt__find_table(fc, offset, "head");
      if ((ttUSHORT(fc+hd+44) & 7) != (flags & 7)) return 0;
   }

   nm = stbtt__find_table(fc, offset, "name");
   if (!nm) return 0;

   if (flags) {
      // if we checked the macStyle flags, then just check the family and ignore the subfamily
      if (stbtt__matchpair(fc, nm, name, nlen, 16, -1))  return 1;
      if (stbtt__matchpair(fc, nm, name, nlen,  1, -1))  return 1;
      if (stbtt__matchpair(fc, nm, name, nlen,  3, -1))  return 1;
   } else {
      if (stbtt__matchpair(fc, nm, name, nlen, 16, 17))  return 1;
      if (stbtt__matchpair(fc, nm, name, nlen,  1,  2))  return 1;
      if (stbtt__matchpair(fc, nm, name, nlen,  3, -1))  return 1;
   }

   return 0;
}

static int stbtt_FindMatchingFont_internal(unsigned char *font_collection, char *name_utf8, stbtt_int32 flags)
{
   stbtt_int32 i;
   for (i=0;;++i) {
      stbtt_int32 off = stbtt_GetFontOffsetForIndex(font_collection, i);
      if (off < 0) return off;
      if (stbtt__matches((stbtt_uint8 *) font_collection, off, (stbtt_uint8*) name_utf8, flags))
         return off;
   }
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif

STBTT_DEF int stbtt_BakeFontBitmap(const unsigned char *data, int offset,
                                float pixel_height, unsigned char *pixels, int pw, int ph,
                                int first_char, int num_chars, stbtt_bakedchar *chardata)
{
   return stbtt_BakeFontBitmap_internal((unsigned char *) data, offset, pixel_height, pixels, pw, ph, first_char, num_chars, chardata);
}

STBTT_DEF int stbtt_GetFontOffsetForIndex(const unsigned char *data, int index)
{
   return stbtt_GetFontOffsetForIndex_internal((unsigned char *) data, index);
}

STBTT_DEF int stbtt_GetNumberOfFonts(const unsigned char *data)
{
   return stbtt_GetNumberOfFonts_internal((unsigned char *) data);
}

STBTT_DEF int stbtt_InitFont(stbtt_fontinfo *info, const unsigned char *data, int offset)
{
   return stbtt_InitFont_internal(info, (unsigned char *) data, offset);
}

STBTT_DEF int stbtt_FindMatchingFont(const unsigned char *fontdata, const char *name, int flags)
{
   return stbtt_FindMatchingFont_internal((unsigned char *) fontdata, (char *) name, flags);
}

STBTT_DEF int stbtt_CompareUTF8toUTF16_bigendian(const char *s1, int len1, const char *s2, int len2)
{
   return stbtt_CompareUTF8toUTF16_bigendian_internal((char *) s1, len1, (char *) s2, len2);
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#endif // STB_TRUETYPE_IMPLEMENTATION

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
