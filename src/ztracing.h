#pragma once

#include "document.h"
#include "memory.h"
#include "os.h"

struct DocumentNode {
    DocumentNode *prev;
    DocumentNode *next;
    u32 id;
    bool open;
    Document *document;
};

struct App {
    Arena *arena;
    bool show_demo_window;
    u32 next_document_id;
    DocumentNode *node;
};

static App *AppCreate();
static void AppDestroy(App *app);

static void AppUpdate(App *app);

static bool AppCanLoadFile(App *app);
static void AppLoadFile(App *app, OsLoadingFile *file);
