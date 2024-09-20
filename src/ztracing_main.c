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

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE prev_instance, PWSTR cmd_line,
                    int cmd_show) {
  OS_Window window = OS_CreateWindow();
  OS_ShowWindow(&window);

  MSG message;
  while (GetMessageW(&message, 0, 0, 0) != 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  return 0;
}