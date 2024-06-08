struct App {
    Arena arena;
    bool show_demo_window;
    Document *document;
};

static App *
AppCreate() {
    App *app = BootstrapPushStruct(App, arena);
    return app;
}

static void
AppDestroy(App *app) {
    if (app->document) {
        UnloadDocument(app->document);
    }
    ClearArena(&app->arena);
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

static void
RenderWelcome(App *app) {
    ASSERT(!app->document);
    Vec2 window_size = ImGui::GetWindowSize();
    Vec2 logo_size = ImGui::CalcTextSize(WELCOME_MESSAGE);
    ImGui::SetCursorPos((window_size - logo_size) / 2.0f);
    ImGui::Text("%s", WELCOME_MESSAGE);
}

static void
DrawMenuBar(Arena *frame_arena, App *app) {
    ImGuiIO *io = &ImGui::GetIO();
    ImGuiStyle *style = &ImGui::GetStyle();
    if (ImGui::BeginMainMenuBar()) {
        Vec2 menu_bar_size = ImGui::GetWindowSize();
        if (ImGui::BeginMenu("About")) {
            ImGui::MenuItem("Dear ImGui", "", &app->show_demo_window);
            ImGui::EndMenu();
        }

        f32 left = ImGui::GetCursorPosX();
        f32 right = left;
        {
            char *text = PushFormatZ(
                frame_arena,
                "%.1f MB %.0f",
                GetAllocatedBytes() / 1024.0f / 1024.0f,
                io->Framerate
            );
            Vec2 size = ImGui::CalcTextSize(text);
            right = ImGui::GetWindowContentRegionMax().x - size.x -
                    style->ItemSpacing.x;
            ImGui::SetCursorPosX(right);
            ImGui::Text("%s", text);
        }

        if (app->document) {
            ImDrawList *draw_list = ImGui::GetWindowDrawList();
            Buffer path = app->document->path;
            Vec2 size = ImGui::CalcTextSize(
                (char *)path.data,
                (char *)(path.data + path.size)
            );
            f32 from = left + (right - left - size.x) / 2.0f;
            from = MAX(from, left);
            f32 to = right - style->ItemSpacing.x;
            to = MAX(from, to);

            f32 top = ImGui::GetCursorPosY();
            ImVec4 clip_rect = {from, top, to, top + menu_bar_size.y};
            draw_list->AddText(
                ImGui::GetFont(),
                ImGui::GetFontSize(),
                {from, ImGui::GetItemRectMin().y},
                IM_COL32_BLACK,
                (char *)path.data,
                (char *)(path.data + path.size),
                0.0f,
                &clip_rect
            );
        }

        ImGui::EndMainMenuBar();
    }
}

static void
AppUpdate(App *app) {
    Arena frame_arena = app->arena;

    DrawMenuBar(&frame_arena, app);

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
    ImGui::Begin("MainWindow", 0, window_flags);
    ImGui::PopStyleVar(3);
    ImGui::DockSpace(
        dockspace_id,
        ImVec2(0.0f, 0.0f),
        ImGuiDockNodeFlags_PassthruCentralNode
    );
    ImGui::End();

    ImGuiWindowClass window_class;
    window_class.DockNodeFlagsOverrideSet =
        ImGuiDockNodeFlags_NoTabBar | ImGuiDockNodeFlags_NoDockingOverMe;
    ImGui::SetNextWindowDockID(dockspace_id);
    ImGui::SetNextWindowClass(&window_class);
    ImGui::Begin(
        "Document",
        0,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
    );
    if (app->document) {
        char *title = PushFormatZ(&frame_arena, "%s", app->document->path);
        UpdateDocument(app->document, &frame_arena);
    } else {
        RenderWelcome(app);
    }
    ImGui::End();

    if (app->show_demo_window) {
        ImGui::ShowDemoWindow(&app->show_demo_window);
    }
}

static bool
AppCanLoadFile(App *app) {
    return true;
}

static void
AppLoadFile(App *app, OsLoadingFile *file) {
    if (app->document) {
        UnloadDocument(app->document);
    }
    app->document = LoadDocument(file);
}
