#pragma once

#include "document.h"
#include "memory.h"
#include "os.h"

struct DocumentTab {
    bool open;
    Document *document;
};

struct App {
    Arena *arena;
    bool show_demo_window;
    DynArray *documents;
};

static App *AppCreate();
static void AppDestroy(App *app);

static void AppUpdate(App *app);

static bool AppCanLoadFile(App *app);
static void AppLoadFile(App *app, OsLoadingFile *file);
