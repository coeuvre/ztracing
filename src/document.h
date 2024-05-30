#pragma once

#include "memory.h"
#include "os.h"

enum DocumentState {
    DocumentState_Loading,
    DocumentState_View,
};

struct LoadState {
    OsLoadingFile *file;
    volatile usize loaded;
};

struct Document {
    Arena arena;
    char *path;

    DocumentState state;
    union {
        struct {
            Task *task;
            LoadState state;
        } loading;
    };
};

static Document *LoadDocument(OsLoadingFile *file);
static void UnloadDocument(Document *document);
static void RenderDocument(Document *document, Arena *frame_arena);
