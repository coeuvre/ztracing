#pragma once

#include "memory.h"
#include "os.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

typedef ImVec2 Vec2;

struct LoadFileData {
    OsLoadingFile *file;
    volatile bool cancelled;
};

enum TracingState {
    TracingState_Loading,
    TracingState_View,
};

struct TracingLoading {
    LoadFileData *data;
    Task *task;
};

struct Tracing {
    Tracing *prev;
    Tracing *next;
    Arena *arena;
    char *title;
    u32 id;
    TracingState state;
    bool open;
    union {
        TracingLoading loading;
    };
};

struct App {
    Arena *arena;
    bool show_demo_window;
    u32 next_tracing_id;
    Tracing *tracing;
};

static App *AppCreate();
static void AppDestroy(App *);

static void AppUpdate(App *app);

static bool AppCanLoadFile(App *app);
static void AppLoadFile(App *app, OsLoadingFile *file);
