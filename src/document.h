#pragma once

#include "memory.h"
#include "os.h"

enum DocumentState {
    DocumentState_Loading,
    DocumentState_View,
};

struct DocumentLoading {
    Task *task;
    OsLoadingFile *file;
    volatile usize loaded;
    volatile bool cancelled;
};

struct Document {
    Arena arena;
    char *path;

    DocumentState state;
    union {
        DocumentLoading loading;
    };
};

static Document *DocumentLoad(OsLoadingFile *file);
static void DocumentDestroy(Document *document);
static void DocumentUpdate(Document *document, Arena *frame_arena);
