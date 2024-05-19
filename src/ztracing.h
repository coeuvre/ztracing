#pragma once

#include "core.h"
#include "os.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

typedef ImVec2 Vec2;

struct LoadFileTask {
    OsLoadingFile *file;
    volatile bool done;
};

struct MainMenu {
    bool show_demo_window;
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
    MainMenu main_menu;
    AppState state;
    union {
        AppLoading loading;
    };
};

static void AppUpdate(App *app);

static bool AppCanLoadFile(App *app);
static void AppLoadFile(App *app, OsLoadingFile *file);
static OsLoadingFile *AppGetLoadingFile(App *app);
