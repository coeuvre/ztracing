#define LOG_ERR 0
#define LOG_WARN 1
#define LOG_INFO 2
#define LOG_DEBUG 3

extern "C" void log_impl(int level, const char *msg);

#include <stdio.h>
#include <stdarg.h>

static void my_debug_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[4096];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    log_impl(LOG_DEBUG, buf);
}

#ifdef ZTRACING_WASM
#define IM_ASSERT(_EXPR) (void)((!!(_EXPR)) || (log_impl(LOG_ERR, #_EXPR), 0))
#endif
#define IMGUI_DISABLE_DEFAULT_ALLOCATORS 
#define IMGUI_DEBUG_PRINTF(_FMT,...) my_debug_printf(_FMT, __VA_ARGS__)

#define ImDrawIdx unsigned int
#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS 1
#define IMGUI_DISABLE_OBSOLETE_KEYIO 1

#include "third_party/cimgui/imgui/imgui.cpp"
#include "third_party/cimgui/imgui/imgui_demo.cpp"
#include "third_party/cimgui/imgui/imgui_draw.cpp"
#include "third_party/cimgui/imgui/imgui_tables.cpp"
#include "third_party/cimgui/imgui/imgui_widgets.cpp"
#include "third_party/cimgui/cimgui.cpp"

CIMGUI_API ImS16 igTableColumnGetSortColumnIndex(ImGuiTableSortSpecs *specs) {
    IM_ASSERT(specs->SpecsCount == 1);
    return specs->Specs[0].ColumnIndex;
}

CIMGUI_API ImGuiSortDirection igTableColumnGetSortDirection(ImGuiTableSortSpecs *specs) {
    IM_ASSERT(specs->SpecsCount == 1);
    return specs->Specs[0].SortDirection;
}

#ifndef ZTRACING_WASM

#include "third_party/cimgui/imgui/backends/imgui_impl_sdl2.cpp"
#include "third_party/cimgui/imgui/backends/imgui_impl_sdlrenderer2.cpp"

CIMGUI_API bool ig_ImplSDL2_InitForSDLRenderer(SDL_Window *window, SDL_Renderer *renderer) {
    return ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
}

CIMGUI_API void ig_ImplSDL2_Shutdown() {
    ImGui_ImplSDL2_Shutdown();
}

CIMGUI_API void ig_ImplSDL2_NewFrame() {
    ImGui_ImplSDL2_NewFrame();
}

CIMGUI_API bool ig_ImplSDL2_ProcessEvent(const SDL_Event* event) {
    return ImGui_ImplSDL2_ProcessEvent(event);
}

CIMGUI_API bool ig_ImplSDLRenderer2_Init(SDL_Renderer* renderer) {
    return ImGui_ImplSDLRenderer2_Init(renderer);
}

CIMGUI_API void ig_ImplSDLRenderer2_Shutdown() {
    return ImGui_ImplSDLRenderer2_Shutdown();
}

CIMGUI_API void ig_ImplSDLRenderer2_NewFrame() {
    return ImGui_ImplSDLRenderer2_NewFrame();
}

CIMGUI_API void ig_ImplSDLRenderer2_RenderDrawData(ImDrawData* draw_data) {
    return ImGui_ImplSDLRenderer2_RenderDrawData(draw_data);
}

#endif
