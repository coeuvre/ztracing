#pragma once

#include "document.h"
#include "memory.h"
#include "os.h"

struct App {
    Arena arena;
    bool show_demo_window;
    Document *document;
};

static App *AppCreate();
static void AppDestroy(App *app);

static void AppUpdate(App *app);

static bool AppCanLoadFile(App *app);
static void AppLoadFile(App *app, OsLoadingFile *file);
