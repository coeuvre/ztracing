#pragma once

#include "core.h"

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
    APP_WELCOME,
    APP_LOADING,
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

static void ztracing_update(App *app);

static bool ztracing_accept_load(App *app);
static void ztracing_load_file(App *app, OsLoadingFile *file);
static OsLoadingFile *ztracing_get_loading_file(App *app);
