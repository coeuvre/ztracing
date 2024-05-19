#include "ztracing.h"

#include <stdio.h>

char TMP_BUF[256];
u32 TMP_BUF_SIZE = ARRAY_SIZE(TMP_BUF);

static void transit_to_welcome(App *app) {
    app->state = APP_WELCOME;
}

static void transit_to_loading(App *app, AppLoading loading) {
    switch (app->state) {
    case APP_WELCOME: {
        app->state = APP_LOADING;
        app->loading = loading;
    } break;

    default: {
        UNREACHABLE;
    } break;
    }
}

static void ui_main_menu(MainMenu *main_menu) {
    ImGuiIO *io = &ImGui::GetIO();
    ImGuiStyle *style = &ImGui::GetStyle();
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("About")) {
            ImGui::MenuItem("Dear ImGui", "", &main_menu->show_demo_window);
            ImGui::EndMenu();
        }

        {
            snprintf(
                TMP_BUF,
                TMP_BUF_SIZE,
                "Memory: %.1f MB",
                memory_get_allocated_bytes() / 1024.0f / 1024.0f
            );
            Vec2 size = ImGui::CalcTextSize(TMP_BUF);
            ImGui::Text("%s", TMP_BUF);
        }

        {
            snprintf(TMP_BUF, TMP_BUF_SIZE, "%.0f", io->Framerate);
            Vec2 size = ImGui::CalcTextSize(TMP_BUF);
            ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - size.x);
            ImGui::Text("%s", TMP_BUF);
        }

        ImGui::EndMainMenuBar();
    }

    if (main_menu->show_demo_window) {
        ImGui::ShowDemoWindow(&main_menu->show_demo_window);
    }
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

static void ui_main_window_welcome() {
    Vec2 window_size = ImGui::GetWindowSize();
    Vec2 logo_size = ImGui::CalcTextSize(WELCOME_MESSAGE);
    ImGui::SetCursorPos((window_size - logo_size) / 2.0f);
    ImGui::Text("%s", WELCOME_MESSAGE);
}

static void ui_main_window_loading(App *app) {
    AppLoading *loading = &app->loading;

    {
        Vec2 window_size = ImGui::GetWindowSize();

        snprintf(TMP_BUF, TMP_BUF_SIZE, "Loading ...");
        Vec2 text_size = ImGui::CalcTextSize(TMP_BUF);
        ImGui::SetCursorPos((window_size - text_size) / 2.0f);
        ImGui::Text("%s", TMP_BUF);
    }

    if (loading->task->done) {
        os_thread_join(loading->thread);
        memory_free(loading->task);
        transit_to_welcome(app);
    }
}

static void ui_main_window(App *app) {
    ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    if (ImGui::Begin(
            "MainWindow",
            0,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoDocking
        )) {
        switch (app->state) {
        case APP_WELCOME: {
            ui_main_window_welcome();
        } break;

        case APP_LOADING: {
            ui_main_window_loading(app);
        } break;

        default: {
            UNREACHABLE;
        } break;
        }
    }
    ImGui::End();
}

static void ztracing_update(App *app) {
    ui_main_menu(&app->main_menu);
    ui_main_window(app);
}

static int load_file_fn(void *data) {
    LoadFileTask *task = (LoadFileTask *)data;
    INFO("Loading file ...");

    isize total = 0;
    u8 buf[4096];
    for (bool need_more_read = true; need_more_read;) {
        u32 nread = os_loading_file_next(task->file, buf, ARRAY_SIZE(buf) - 1);
        buf[nread] = 0;
        total += nread;
        // INFO("(%d): %s", nread, buf);
        // TODO: process buf[0..nread]
        need_more_read = nread > 0;
    }

    INFO("Processed %zd bytes.", total);

    os_loading_file_close(task->file);

    task->done = true;

    return 0;
}

static bool ztracing_accept_load(App *app) {
    bool result = app->state != APP_LOADING;
    return result;
}

static void ztracing_load_file(App *app, OsLoadingFile *file) {
    ASSERT(ztracing_accept_load(app), "");

    LoadFileTask *task = (LoadFileTask *)memory_alloc(sizeof(LoadFileTask));
    task->file = file;

    OsThread *thread = os_thread_create(load_file_fn, task);

    AppLoading loading = {};
    loading.task = task;
    loading.thread = thread;
    transit_to_loading(app, loading);
}

static OsLoadingFile *ztracing_get_loading_file(App *app) {
    OsLoadingFile *file = 0;
    if (app->state == APP_LOADING) {
        file = app->loading.task->file;
    }
    return file;
}
