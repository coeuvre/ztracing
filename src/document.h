#pragma once

#include "memory.h"
#include "os.h"
#include "task.h"

enum DocumentState {
    DocumentState_Loading,
    DocumentState_Error,
    DocumentState_View,
};

struct LoadState {
    Arena *document_arena;
    OsLoadingFile *file;
    volatile usize loaded;
    Buffer error;
};

struct Document {
    Arena arena;
    Buffer path;

    DocumentState state;
    union {
        struct {
            Task *task;
            LoadState state;
        } loading;

        struct {
            Buffer message;
        } error;
    };
};

Document *LoadDocument(OsLoadingFile *file);
void UnloadDocument(Document *document);
void RenderDocument(Document *document, Arena *frame_arena);
