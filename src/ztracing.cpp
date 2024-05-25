#include "ztracing.h"

static App *AppCreate() {
    Arena *arena = ArenaCreate();
    App *app = ArenaAllocStruct(arena, App);
    app->arena = arena;
    app->documents = DynArrayCreate(arena, sizeof(DocumentTab), 0);
    return app;
}

static void AppDestroy(App *app) {
    for (usize i = 0; i < app->documents->len; ++i) {
        DocumentTab *tab = (DocumentTab *)DynArrayGet(app->documents, i);
        DocumentDestroy(tab->document);
    }
    ArenaDestroy(app->arena);
}

static const char *WELCOME_MESSAGE =
    R"(
 ________  _________  _______          _        ______  _____  ____  _____   ______
|  __   _||  _   _  ||_   __ \        / \     .' ___  ||_   _||_   \|_   _|.' ___  |
|_/  / /  |_/ | | \_|  | |__) |      / _ \   / .'   \_|  | |    |   \ | | / .'   \_|
   .'.' _     | |      |  __ /      / ___ \  | |         | |    | |\ \| | | |   ____
 _/ /__/ |   _| |_    _| |  \ \_  _/ /   \ \_\ `.___.'\ _| |_  _| |_\   |_\ `.___]  |
|________|  |_____|  |____| |___||____| |____|`.____ .'|_____||_____|\____|`._____.'


                        Drag & Drop a trace file to start.
)";

static void MaybeDrawWelcome(App *app) {
    if (!app->documents->len) {
        Vec2 window_size = ImGui::GetWindowSize();
        Vec2 logo_size = ImGui::CalcTextSize(WELCOME_MESSAGE);
        ImGui::SetCursorPos((window_size - logo_size) / 2.0f);
        ImGui::Text("%s", WELCOME_MESSAGE);
    }
}

static void DrawMenuBar(App *app) {
    ImGuiIO *io = &ImGui::GetIO();
    ImGuiStyle *style = &ImGui::GetStyle();
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("About")) {
            ImGui::MenuItem("Dear ImGui", "", &app->show_demo_window);
            ImGui::EndMenu();
        }

        f32 left = ImGui::GetCursorPosX();
        f32 right = left;
        {
            char *text = ArenaFormatString(
                app->arena,
                "%.1f MB  %.0f",
                MemoryGetAlloc() / 1024.0f / 1024.0f,
                io->Framerate
            );
            Vec2 size = ImGui::CalcTextSize(text);
            right = ImGui::GetWindowContentRegionMax().x - size.x -
                    style->ItemSpacing.x;
            ImGui::SetCursorPosX(right);
            ImGui::Text("%s", text);
            ArenaFree(app->arena, text);
        }

        ImGui::EndMainMenuBar();
    }
}

static void AppUpdate(App *app) {
    DrawMenuBar(app);

    ImGuiID dockspace_id = ImGui::GetID("DockSpace");

    ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("Background", 0, window_flags);
    ImGui::PopStyleVar(3);
    {
        ImGui::DockSpace(
            dockspace_id,
            ImVec2(0.0f, 0.0f),
            ImGuiDockNodeFlags_PassthruCentralNode
        );

        MaybeDrawWelcome(app);
    }
    ImGui::End();

    for (u32 tab_index = 0; tab_index < app->documents->len;) {
        DocumentTab *tab =
            (DocumentTab *)DynArrayGet(app->documents, tab_index);
        Document *document = tab->document;
        char *title =
            ArenaFormatString(app->arena, "%s##%u", document->path, tab_index);
        ImGui::SetNextWindowDockID(dockspace_id, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(title, &tab->open)) {
            DocumentUpdate(document);
        }
        ImGui::End();
        ArenaFree(app->arena, title);

        if (!tab->open) {
            DocumentDestroy(document);
            DynArrayRemove(app->documents, tab_index);
        } else {
            ++tab_index;
        }
    }

    if (app->show_demo_window) {
        ImGui::ShowDemoWindow(&app->show_demo_window);
    }
}

static bool AppCanLoadFile(App *app) {
    return true;
}

static void AppLoadFile(App *app, OsLoadingFile *file) {
    DocumentTab *tab = (DocumentTab *)DynArrayAppend(app->documents);
    tab->open = true;
    tab->document = DocumentLoad(file);
}
