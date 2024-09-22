#include "base/base.h"
#include "base/base.c"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "third_party/stb/stb_truetype.h"

#include "assets/JetBrainsMono-Regular.h"

#define UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#pragma comment(lib, "kernel32")
#pragma comment(lib, "user32")
#pragma comment(lib, "gdi32")

static LRESULT CALLBACK
window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    LRESULT result = 0;

    switch (message) {
    case WM_CLOSE:
    case WM_DESTROY: {
        PostQuitMessage(0);
    } break;

    default: {
        result = DefWindowProcW(window, message, wparam, lparam);
    } break;
    }

    return result;
}

typedef struct OS_Window OS_Window;
struct OS_Window {
    HWND handle;
};

static OS_Window
os_create_window() {
    static WNDCLASSEXW window_class;

    if (window_class.cbSize == 0) {
        window_class = (WNDCLASSEXW){
            .cbSize = sizeof(window_class),
            .lpfnWndProc = &window_proc,
            .hInstance = GetModuleHandleW(0),
            .hIcon = LoadIconW(0, IDI_APPLICATION),
            .hCursor = LoadCursorW(0, IDC_ARROW),
            .hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH),
            .lpszClassName = L"ztracingclass",
        };

        if (!RegisterClassExW(&window_class)) {
            UNREACHABLE;
        }
    }

    OS_Window result = {0};
    DWORD ex_style = WS_EX_APPWINDOW;
    result.handle = CreateWindowExW(
        ex_style,
        window_class.lpszClassName,
        L"ztracing",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        0,
        0,
        window_class.hInstance,
        0
    );
    return result;
}

static void
os_show_window(OS_Window *window) {
    if (IsWindow(window->handle)) {
        ShowWindow(window->handle, SW_SHOW);
    }
}

static Vec2i
os_get_window_size(OS_Window *window) {
    Vec2i result = {0};

    RECT rect;
    GetClientRect(window->handle, &rect);
    result.x = rect.right - rect.left;
    result.y = rect.bottom - rect.top;

    return result;
}

typedef struct Bitmap Bitmap;
struct Bitmap {
    u32 *pixels;
    Vec2i size;
};

static Bitmap *
push_bitmap(Arena *arena, Vec2i size) {
    Bitmap *bitmap = push_array(arena, Bitmap, 1);
    if (size.x > 0 && size.y > 0) {
        bitmap->pixels = push_array(arena, u32, size.x * size.y);
        bitmap->size = size;
    }
    return bitmap;
}

static void
os_copy_bitmap_to_window(OS_Window *window, Bitmap *bitmap) {
    if (bitmap->pixels) {
        BITMAPINFO bi;
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = bitmap->size.x;
        bi.bmiHeader.biHeight = -bitmap->size.y;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        Vec2i size = os_get_window_size(window);
        i32 width = MIN(size.x, bitmap->size.x);
        i32 height = MIN(size.y, bitmap->size.y);

        HDC hdc = GetDC(window->handle);
        StretchDIBits(
            hdc,
            0,
            0,
            width,
            height,
            0,
            0,
            width,
            height,
            bitmap->pixels,
            &bi,
            DIB_RGB_COLORS,
            SRCCOPY
        );
        ReleaseDC(window->handle, hdc);
    }
}

// static inline Vec4
// linear_color_from_srgb(u32 color) {

// }

// static inline u32
// srgb_color_from_linear(Vec4 color) {

// }

// static u32
// blend_color(u32 dst, u32 src) {
//     Vec4 dst_linear = linear_color_from_srgb(dst);
//     Vec4 src_linear = linear_color_from_srgb(src);

//     dst_linear.x =
//         (1.0 - src_linear.w) * dst_linear.x + src_linear.w * src_linear.x;
//     dst_linear.y =
//         (1.0 - src_linear.w) * dst_linear.y + src_linear.w * src_linear.y;
//     dst_linear.z =
//         (1.0 - src_linear.w) * dst_linear.z + src_linear.w * src_linear.z;
//     dst_linear.w = ;
// }

static void
copy_bitmap(Bitmap *dst, Vec2 pos, Bitmap *src) {
    Vec2i offset = vec2i_from_vec2(round_vec2(pos));
    Vec2i src_min = max_vec2i(neg_vec2i(offset), vec2i(0, 0));
    Vec2i src_max =
        sub_vec2i(min_vec2i(add_vec2i(offset, src->size), dst->size), offset);

    Vec2i dst_min = max_vec2i(offset, vec2i(0, 0));
    Vec2i dst_max = add_vec2i(dst_min, sub_vec2i(src_max, src_min));

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

static void
draw_rect(Bitmap *bitmap, Vec2 min, Vec2 max, u32 color) {
    Vec2 fb_min = vec2(0.0f, 0.0f);
    Vec2 fb_max = vec2_from_vec2i(bitmap->size);
    min = clamp_vec2(min, fb_min, fb_max);
    max = clamp_vec2(max, fb_min, fb_max);
    i32 min_x = round_f32(min.x);
    i32 min_y = round_f32(min.y);
    i32 max_x = round_f32(max.x);
    i32 max_y = round_f32(max.y);
    for (i32 y = min_y; y < max_y; ++y) {
        u32 *row = bitmap->pixels + bitmap->size.x * y;
        for (i32 x = min_x; x < max_x; ++x) {
            row[x] = color;
        }
    }
}

int WINAPI
wWinMain(
    HINSTANCE instance, HINSTANCE prev_instance, PWSTR cmd_line, int cmd_show
) {
    OS_Window window = os_create_window();
    os_show_window(&window);

    Str8 text = str8_literal("Heljo World! 你好，世界！");
    stbtt_fontinfo font;
    {
        i32 ret = stbtt_InitFont(
            &font,
            JetBrainsMono_Regular_ttf,
            stbtt_GetFontOffsetForIndex(JetBrainsMono_Regular_ttf, 0)
        );
        assert(ret != 0);
    }
    f32 scale = stbtt_ScaleForPixelHeight(&font, 32);
    i32 ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);

    Arena *framebuffer_arena = alloc_arena();
    Bitmap *framebuffer =
        push_bitmap(framebuffer_arena, os_get_window_size(&window));

    b32 quit = 0;
    while (!quit) {
        MSG message;
        while (PeekMessageW(&message, 0, 0, 0, PM_REMOVE)) {
            switch (message.message) {
            case WM_QUIT: {
                quit = 1;
            } break;
            default: {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            } break;
            }
        }
        Vec2i window_size = os_get_window_size(&window);
        if (!equal_vec2i(framebuffer->size, window_size)) {
            free_arena(framebuffer_arena);
            framebuffer_arena = alloc_arena();
            framebuffer =
                push_bitmap(framebuffer_arena, os_get_window_size(&window));
        }

        draw_rect(
            framebuffer,
            vec2(0.0f, 0.0f),
            vec2_from_vec2i(framebuffer->size),
            0x00000000
        );

        {
            TempMemory scratch = begin_scratch(0, 0);

            Str32 text32 = str32_from_str8(scratch.arena, text);
            i32 baseline = (i32)(ascent * scale);
            f32 pos_x = 2.0f;
            for (u32 i = 0; i < text32.len; ++i) {
                Vec2i min, max;
                i32 advance, lsb;
                u32 ch = text32.ptr[i];
                f32 x_shift = pos_x - floor_f32(pos_x);
                i32 glyph = stbtt_FindGlyphIndex(&font, ch);
                stbtt_GetGlyphHMetrics(&font, glyph, &advance, &lsb);
                stbtt_GetGlyphBitmapBox(
                    &font, glyph, scale, scale, &min.x, &min.y, &max.x, &max.y
                );
                Vec2i glyph_size = sub_vec2i(max, min);
                u8 *out =
                    push_array(scratch.arena, u8, glyph_size.x * glyph_size.y);
                assert(out);
                stbtt_MakeGlyphBitmap(
                    &font,
                    out,
                    glyph_size.x,
                    glyph_size.y,
                    glyph_size.x,
                    scale,
                    scale,
                    glyph
                );

                Bitmap *glyph_bitmap = push_bitmap(scratch.arena, glyph_size);
                u32 *dst_row = glyph_bitmap->pixels;
                u8 *src_row = out;
                for (i32 y = 0; y < glyph_size.y; ++y) {
                    u32 *dst = dst_row;
                    u8 *src = src_row;
                    for (i32 x = 0; x < glyph_size.x; ++x) {
                        u8 alpha = *src++;
                        (*dst++) =
                            (((u32)alpha << 24) | ((u32)alpha << 16) |
                             ((u32)alpha << 8) | ((u32)alpha << 0));
                    }
                    dst_row += glyph_size.x;
                    src_row += glyph_size.x;
                }
                copy_bitmap(
                    framebuffer,
                    vec2(pos_x + min.x, baseline + min.y),
                    glyph_bitmap
                );

                pos_x += advance * scale;
                if (i + 1 < text32.len) {
                    i32 kern = stbtt_GetCodepointKernAdvance(
                        &font, ch, text32.ptr[i + 1]
                    );
                    pos_x += scale * kern;
                }
            }

            end_scratch(scratch);
        }

        os_copy_bitmap_to_window(&window, framebuffer);
    }

    free_arena(framebuffer_arena);

    return 0;
}
