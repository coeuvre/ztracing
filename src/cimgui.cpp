#define LOG_ERR 0
#define LOG_WARN 1
#define LOG_INFO 2
#define LOG_DEBUG 3
extern "C" void logFromC(int level, const char *msg);

#include <stdio.h>
#include <stdarg.h>

static void my_debug_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[4096];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    logFromC(LOG_DEBUG, buf);
}

#define IM_ASSERT(_EXPR) (void)((!!(_EXPR)) || (logFromC(LOG_ERR, #_EXPR), 0))
#define IMGUI_DISABLE_DEFAULT_ALLOCATORS 
#define IMGUI_DEBUG_PRINTF(_FMT,...) my_debug_printf(_FMT, __VA_ARGS__)

#define ImDrawIdx unsigned int

#include "cimgui/imgui/imgui.cpp"
#include "cimgui/imgui/imgui_demo.cpp"
#include "cimgui/imgui/imgui_draw.cpp"
#include "cimgui/imgui/imgui_tables.cpp"
#include "cimgui/imgui/imgui_widgets.cpp"


#define CIMGUI_API

#include "cimgui/cimgui.cpp"