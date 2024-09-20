#include "base/base.h"

#define UNICODE
#include <windows.h>

#pragma comment(lib, "kernel32")
#pragma comment(lib, "user32")
#pragma comment(lib, "gdi32")

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

typedef struct OS_Window OS_Window;
struct OS_Window {
  HWND handle;
};

static OS_Window OS_CreateWindow() {
  static WNDCLASSEXW window_class;

  if (window_class.cbSize == 0) {
    window_class = (WNDCLASSEXW){
        .cbSize = sizeof(window_class),
        .lpfnWndProc = &WindowProc,
        .hInstance = GetModuleHandleW(0),
        .hIcon = LoadIconW(0, IDI_APPLICATION),
        .hCursor = LoadCursorW(0, IDC_ARROW),
        .hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH),
        .lpszClassName = L"ztracingclass",
    };

    if (!RegisterClassExW(&window_class)) {
      Unreachable;
    }
  }

  OS_Window result = {0};
  DWORD ex_style = WS_EX_APPWINDOW;
  result.handle = CreateWindowExW(
      ex_style, window_class.lpszClassName, L"ztracing", WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0,
      window_class.hInstance, 0);
  return result;
}

static void OS_ShowWindow(OS_Window *window) {
  if (IsWindow(window->handle)) {
    ShowWindow(window->handle, SW_SHOW);
  }
}

static Vec2I OS_GetWindowSize(OS_Window *window) {
  Vec2I result = {0};

  RECT rect;
  GetClientRect(window->handle, &rect);
  result.x = rect.right - rect.left;
  result.y = rect.bottom - rect.top;

  return result;
}

typedef struct OS_FrameBuffer OS_FrameBuffer;
struct OS_FrameBuffer {
  BITMAPINFO bi;
  Vec2I size;
  u32 *pixels;
};

static OS_FrameBuffer OS_CreateFrameBuffer(Vec2I size) {
  OS_FrameBuffer fb = {0};
  if (size.x > 0 && size.y > 0) {
    fb.pixels = VirtualAlloc(0, size.x * size.y * 4, MEM_COMMIT | MEM_RESERVE,
                             PAGE_READWRITE);
  }
  if (fb.pixels) {
    fb.size = size;
    fb.bi.bmiHeader.biSize = sizeof(fb.bi.bmiHeader);
    fb.bi.bmiHeader.biWidth = size.x;
    fb.bi.bmiHeader.biHeight = -size.y;
    fb.bi.bmiHeader.biPlanes = 1;
    fb.bi.bmiHeader.biBitCount = 32;
    fb.bi.bmiHeader.biCompression = BI_RGB;
  }
  return fb;
}

static void OS_DestroyFrameBuffer(OS_FrameBuffer *fb) {
  if (fb->pixels) {
    VirtualFree(fb->pixels, 0, MEM_RELEASE);
    ZeroMemory(fb, sizeof(*fb));
  }
}

static void OS_ResizeFrameBuffer(OS_FrameBuffer *fb, Vec2I size) {
  OS_DestroyFrameBuffer(fb);
  *fb = OS_CreateFrameBuffer(size);
}

static void OS_CopyFrameBufferToWindow(OS_Window *window, OS_FrameBuffer *fb) {
  if (fb->pixels) {
    Vec2I size = OS_GetWindowSize(window);
    HDC hdc = GetDC(window->handle);
    i32 width = Min(size.x, fb->size.x);
    i32 height = Min(size.y, fb->size.y);
    StretchDIBits(hdc, 0, 0, width, height, 0, 0, width, height, fb->pixels,
                  &fb->bi, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(window->handle, hdc);
  }
}

static void OS_DrawRect(OS_FrameBuffer *fb, Rect2 rect, u32 color) {
  Vec2 fb_min = V2(0.0f, 0.0f);
  Vec2 fb_max = Vec2FromVec2I(fb->size);
  Vec2 min = ClampVec2(rect.min, fb_min, fb_max);
  Vec2 max = ClampVec2(rect.max, fb_min, fb_max);
  i32 min_x = RoundF32(min.x);
  i32 min_y = RoundF32(min.y);
  i32 max_x = RoundF32(max.x);
  i32 max_y = RoundF32(max.y);
  for (i32 y = min_y; y < max_y; ++y) {
    u32 *row = fb->pixels + fb->size.x * y;
    for (i32 x = min_x; x < max_x; ++x) {
      row[x] = color;
    }
  }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE prev_instance, PWSTR cmd_line,
                    int cmd_show) {
  OS_Window window = OS_CreateWindow();
  OS_ShowWindow(&window);

  OS_FrameBuffer fb = OS_CreateFrameBuffer(OS_GetWindowSize(&window));

  b32 quit = 0;
  for (; !quit;) {
    MSG message;
    while (PeekMessageW(&message, 0, 0, 0, PM_REMOVE)) {
      switch (message.message) {
        case WM_QUIT: {
          quit = 1;
        } break;
        default: {
          TranslateMessage(&message);
          DispatchMessageW(&message);
        }
      }
    }
    Vec2I window_size = OS_GetWindowSize(&window);
    if (!EqualVec2I(fb.size, window_size)) {
      OS_ResizeFrameBuffer(&fb, window_size);
    }
    OS_DrawRect(&fb, R2(V2(0.0f, 0.0f), Vec2FromVec2I(fb.size)), 0x00FF00FF);
    OS_CopyFrameBufferToWindow(&window, &fb);
  }

  return 0;
}