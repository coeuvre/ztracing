#pragma once

#include "core.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

typedef ImVec2 Vec2;

struct LoadFileTask {
    OsFile *file;
};

struct MainMenu {
    bool show_demo_window;
};

enum MainWindowState {
    MAIN_WINDOW_WELCOME,
    MAIN_WINDOW_LOADING,
};

struct MainWindowLoading {
    LoadFileTask *task;
};

struct MainWindow {
    MainWindowState state;
    union {
        MainWindowLoading loading;
    };
};

struct UIState {
    MainMenu main_menu;
    MainWindow main_window;
};

struct ZTracing {
    UIState ui;
};

static void ztracing_update(ZTracing *ztracing);
static void ztracing_load_file(ZTracing *ztracing, OsFile *file);
