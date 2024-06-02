#pragma once

#include "document.h"
#include "memory.h"
#include "os.h"

struct App {
    Arena arena;
    bool show_demo_window;
    Document *document;
};

App *AppCreate();
void AppDestroy(App *app);

void AppUpdate(App *app);

bool AppCanLoadFile(App *app);
void AppLoadFile(App *app, OsLoadingFile *file);
