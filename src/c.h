#pragma once

#define ImDrawIdx unsigned int
#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS 1
#define IMGUI_DISABLE_OBSOLETE_KEYIO 1

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS 1
#include "third_party/cimgui/cimgui.h"

CIMGUI_API ImS16 igTableColumnGetSortColumnIndex(ImGuiTableSortSpecs *specs);
CIMGUI_API ImGuiSortDirection igTableColumnGetSortDirection(ImGuiTableSortSpecs *specs);
CIMGUI_API void igMakeTabVisible(const char *window_name);

#ifndef ZTRACING_WASM

#include <SDL2/SDL.h>

CIMGUI_API bool ig_ImplSDL2_InitForSDLRenderer(SDL_Window *window, SDL_Renderer *renderer);
CIMGUI_API void ig_ImplSDL2_Shutdown();
CIMGUI_API void ig_ImplSDL2_NewFrame();
CIMGUI_API bool ig_ImplSDL2_ProcessEvent(const SDL_Event* event);

CIMGUI_API bool ig_ImplSDLRenderer2_Init(SDL_Renderer* renderer);
CIMGUI_API void ig_ImplSDLRenderer2_Shutdown();
CIMGUI_API void ig_ImplSDLRenderer2_NewFrame();
CIMGUI_API void ig_ImplSDLRenderer2_RenderDrawData(ImDrawData* draw_data);

#include <zlib.h>

#endif
