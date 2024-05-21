#pragma once

#include "memory.h"
#include "os.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

typedef ImVec2 Vec2;

struct LoadFileData {
    OsLoadingFile *file;
};

enum AppState {
    AppState_Welcome,
    AppState_Loading,
};

struct AppLoading {
    LoadFileData *data;
    Task *task;
};

struct App {
    Arena *arena;
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
