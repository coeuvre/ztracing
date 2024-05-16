#include "ztracing.h"

static void ui_main_menu(MainMenu *main_menu) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open")) {
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("About")) {
            ImGui::MenuItem("Dear ImGui", "", &main_menu->show_demo_window);
            ImGui::EndMenu();
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
    ImGui::Text(WELCOME_MESSAGE);
}

static void ui_main_window_loading(MainWindowLoading *loading) {

}

static void ui_main_window(MainWindow *main_window) {
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
        switch (main_window->state) {
        case MAIN_WINDOW_WELCOME: {
            ui_main_window_welcome();
        } break;

        case MAIN_WINDOW_LOADING: {
            ui_main_window_loading(&main_window->loading);
        } break;

        default: {
            UNREACHABLE;
        } break;
        }
    }
    ImGui::End();
}

static void render_ui(UIState *ui) {
    ui_main_menu(&ui->main_menu);
    ui_main_window(&ui->main_window);
}

static void ztracing_update(ZTracing *ztracing) {
    render_ui(&ztracing->ui);
}

static int load_file_fn(void *data) {
    LoadFileTask *task = (LoadFileTask *)data;
    INFO("Loading file %s ...", os_file_get_path(task->file));
    return 0;
}

static LoadFileTask *start_load_file(OsFile *file) {
    LoadFileTask *task = (LoadFileTask *)malloc(sizeof(LoadFileTask));
    task->file = file;

    os_thread_create(load_file_fn, task);

    return task;
}

static void ztracing_load_file(ZTracing *ztracing, OsFile *file) {
    switch (ztracing->ui.main_window.state) {
    case MAIN_WINDOW_WELCOME: {
        ztracing->ui.main_window.state = MAIN_WINDOW_LOADING;
        MainWindowLoading *loading = &ztracing->ui.main_window.loading;
        loading->task = start_load_file(file);
    } break;

    default: {
        os_file_close(file);
    } break;
    }
}
