#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <zlib.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "core.h"
#include "memory.cpp"
#include "json.cpp"

#include "os.h"
#include "channel.cpp"
#include "task.cpp"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>
typedef ImVec2 Vec2;

#include "document.cpp"
#include "app.cpp"

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>
#include "os.cpp"
#ifdef __EMSCRIPTEN__
#include "os_emscripten.cpp"
#else
#include "os_native.cpp"
#endif
