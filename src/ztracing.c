#include "src/ztracing.h"

#include "src/assert.h"
#include "src/draw_software.h"
#include "src/math.h"
#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"
#include "src/ui.h"
#include "src/ui_widgets.h"

#define UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#pragma comment(lib, "kernel32")
#pragma comment(lib, "user32")
#pragma comment(lib, "gdi32")

typedef struct Window Window;
struct Window {
  HWND handle;
};

Window window;

static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam,
                                   LPARAM lparam) {
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

static Window OpenWindow(void) {
  static WNDCLASSEXW window_class = {0};

  if (window_class.cbSize == 0) {
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = &WindowProc;
    window_class.hInstance = GetModuleHandleW(0);
    window_class.hIcon = LoadIconW(0, IDI_APPLICATION);
    window_class.hCursor = LoadCursorW(0, IDC_ARROW);
    window_class.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    window_class.lpszClassName = L"ztracingclass";
  };

  if (!RegisterClassExW(&window_class)) {
    UNREACHABLE;
  }

  Window result = {0};
  DWORD ex_style = WS_EX_APPWINDOW;
  result.handle = CreateWindowExW(
      ex_style, window_class.lpszClassName, L"ztracing", WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0,
      window_class.hInstance, 0);

  ShowWindow(result.handle, SW_SHOW);

  return result;
}

static Vec2I GetWindowSize(Window *window) {
  Vec2I result;

  RECT rect;
  GetClientRect(window->handle, &rect);
  result.x = rect.right - rect.left;
  result.y = rect.bottom - rect.top;

  return result;
}

Vec2I GetCanvasSize(void) {
  Vec2I result = GetWindowSize(&window);
  return result;
}

static void CopyBitmapToWindow(Window *window, Bitmap *bitmap) {
  if (bitmap->pixels) {
    BITMAPINFO bi;
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = bitmap->size.x;
    bi.bmiHeader.biHeight = -bitmap->size.y;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    Vec2I size = GetWindowSize(window);
    i32 width = MIN(size.x, bitmap->size.x);
    i32 height = MIN(size.y, bitmap->size.y);

    HDC hdc = GetDC(window->handle);
    StretchDIBits(hdc, 0, 0, width, height, 0, 0, width, height, bitmap->pixels,
                  &bi, DIB_RGB_COLORS, SRCCOPY);
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

static void DoFrame(void) {
  BeginCenter(Str8Literal("Center"));
  {
    BeginContainer(Str8Literal("Container"));
    SetWidgetColor(0xFFE6573F);
    SetWidgetSize(V2(100, 100));
    { Text(Str8Literal("Heljo")); }
    EndContainer();
  }

  EndCenter();

  // BeginStack();
  // TextLine(Str8Literal("Heljo"));
  // SpaceBar();
  // TextLine(Str8Literal(" World!"));
  // EndStack();
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE prev_instance, PWSTR cmd_line,
                    int cmd_show) {
  window = OpenWindow();
  Bitmap *framebuffer = InitSoftwareRenderer();

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
    Vec2I window_size = GetWindowSize(&window);
    if (!EqualVec2I(framebuffer->size, window_size)) {
      ResizeSoftwareRenderer(window_size);
    }

    ZeroMemory(framebuffer->pixels,
               framebuffer->size.x * framebuffer->size.y * 4);

    BeginUI();
    DoFrame();
    EndUI();

    CopyBitmapToWindow(&window, framebuffer);
  }

  return 0;
}
