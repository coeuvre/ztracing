#pragma once

#include "app.h"
#include "channel.h"
#include "core.h"
#include "os.h"
#include "ui.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>

enum MainLoopState {
    MainLoopState_Init,
    MainLoopState_Update,
    MainLoopState_Shutdown,
};

struct MainLoop {
    int argc;
    char **argv;
    SDL_Thread *worker_thread;
    MainLoopState state;
    SDL_Window *window;
    SDL_Renderer *renderer;
    App *app;
    OsLoadingFile *loading_file;
};

extern MainLoop MAIN_LOOP;

Vec2 GetInitialWindowSize();
void NotifyAppInitDone();
