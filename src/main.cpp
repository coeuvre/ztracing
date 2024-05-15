#include "ztracing.h"

#include <SDL.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>

int main(int argc, char **argv) {
    if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
        ABORT("Failed to init SDL: %s", SDL_GetError());
    }

    SDL_Window *window = SDL_CreateWindow(
        "ztracing",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1280,
        720,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED
    );
    ASSERT(window, "Failed to create SDL_Window: %s", SDL_GetError());

    SDL_Renderer *renderer = SDL_CreateRenderer(
        window,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    ASSERT(renderer, "Failed to create SDL_Renderer: %s", SDL_GetError());

    ImGuiContext *imgui_context = ImGui::CreateContext();
    ASSERT(imgui_context, "Failed to create ImGui context");

    {
        ImGuiIO *io = &ImGui::GetIO();
        io->IniFilename = 0;
        io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        ImGuiStyle *style = &ImGui::GetStyle();
        ImGui::StyleColorsLight(style);
    }

    if (!ImGui_ImplSDL2_InitForSDLRenderer(window, renderer)) {
        ABORT("Failed to init ImGui with SDL");
    }

    if (!ImGui_ImplSDLRenderer2_Init(renderer)) {
        ABORT("Failed to init ImGui with SDL_Renderer");
    }

    ZTracing ztracing = {};

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (!ImGui_ImplSDL2_ProcessEvent(&event)) {
                switch (event.type) {
                case SDL_QUIT: {
                    running = false;
                } break;
                }
            }
        }

        ImGui_ImplSDL2_NewFrame();
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui::NewFrame();

        ztracing_update(&ztracing);

        ImGui::Render();
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
        SDL_RenderPresent(renderer);
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext(imgui_context);

    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}

#include "ztracing.cpp"

#include "os_sdl.cpp"
