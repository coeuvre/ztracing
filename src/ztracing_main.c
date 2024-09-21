#include "base/base.h"

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
        CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT,
        0, 0, window_class.hInstance, 0
    );
    return result;
}

static void
os_show_window(OS_Window *window) {
    if (IsWindow(window->handle)) {
        ShowWindow(window->handle, SW_SHOW);
    }
}

static Vec2I
os_get_window_size(OS_Window *window) {
    Vec2I result = {0};

    RECT rect;
    GetClientRect(window->handle, &rect);
    result.x = rect.right - rect.left;
    result.y = rect.bottom - rect.top;

    return result;
}

typedef struct Bitmap Bitmap;
struct Bitmap {
    Vec2I size;
    u32 *pixels;
};

static Bitmap
create_bitmap(Vec2I size) {
    Bitmap bitmap = {0};
    if (size.x > 0 && size.y > 0) {
        bitmap.pixels = VirtualAlloc(
            0, size.x * size.y * 4, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    }
    if (bitmap.pixels) {
        bitmap.size = size;
    }
    return bitmap;
}

static void
destroy_bitmap(Bitmap *bitmap) {
    if (bitmap->pixels) {
        VirtualFree(bitmap->pixels, 0, MEM_RELEASE);
        ZeroMemory(bitmap, sizeof(*bitmap));
    }
}

static void
resize_bitmap(Bitmap *bitmap, Vec2I size) {
    destroy_bitmap(bitmap);
    *bitmap = create_bitmap(size);
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

        Vec2I size = os_get_window_size(window);
        i32 width = MIN(size.x, bitmap->size.x);
        i32 height = MIN(size.y, bitmap->size.y);

        HDC hdc = GetDC(window->handle);
        StretchDIBits(hdc, 0, 0, width, height, 0, 0, width, height,
            bitmap->pixels, &bi, DIB_RGB_COLORS, SRCCOPY);
        ReleaseDC(window->handle, hdc);
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
    HINSTANCE instance, HINSTANCE prev_instance,
    PWSTR cmd_line, int cmd_show
) {
    OS_Window window = os_create_window();
    os_show_window(&window);

    Bitmap bitmap = create_bitmap(os_get_window_size(&window));

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
        Vec2I window_size = os_get_window_size(&window);
        if (!equal_vec2i(bitmap.size, window_size)) {
            resize_bitmap(&bitmap, window_size);
        }
        draw_rect(
            &bitmap, vec2(0.0f, 0.0f), vec2_from_vec2i(bitmap.size), 0x00FF00FF
        );
        os_copy_bitmap_to_window(&window, &bitmap);
    }

    return 0;
}