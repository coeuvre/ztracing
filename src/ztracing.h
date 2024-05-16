#pragma once

#include "core.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

typedef ImVec2 Vec2;

struct MainMenu {
    bool show_demo_window;
};

enum MainWindowState {
    MAIN_WINDOW_WELCOME,
};

struct MainWindow {
    MainWindowState state;
};

struct UI {
    MainMenu main_menu;
    MainWindow main_window;
};

struct ZTracing {
    UI ui;
};

static void ztracing_update(ZTracing *ztracing);
