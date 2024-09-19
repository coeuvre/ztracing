#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "core.h"
#include "memory.cpp"
#include "json.cpp"
#include "json_trace.cpp"

#include "os.h"
#include "channel.cpp"
#include "task.cpp"

#include "ui.h"
#include "document.cpp"
#include "app.cpp"

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "imgui_impl_sdl2.cpp"
#include "imgui_impl_opengl3.cpp"
#include "imgui_impl_opengl3_extra.cpp"
#include "../assets/JetBrainsMono-Regular.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include "os.cpp"
#ifdef __EMSCRIPTEN__
#include "os_emscripten.cpp"
#else
#include "os_native.cpp"
#endif
