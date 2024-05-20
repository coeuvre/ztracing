#pragma once

#include "memory.h"
#include "os.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

typedef ImVec2 Vec2;

struct LoadFileTask {
    OsLoadingFile *file;
    volatile bool done;
};

enum AppState {
    AppState_Welcome,
    AppState_Loading,
};

struct AppLoading {
    LoadFileTask *task;
    OsThread *thread;
};

struct App {
    Arena *arena;
    Arena *frame_arena;
    bool show_demo_window;
    AppState state;
    union {
        AppLoading loading;
    };
};

static App *AppCreate();
static void AppDestroy(App *);

static void AppUpdate(App *app);

static bool AppCanLoadFile(App *app);
static void AppLoadFile(App *app, OsLoadingFile *file);
