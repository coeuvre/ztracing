#include <SDL.h>

#include "core.h"

int main(int argc, char **argv) {
    int result = SDL_Init(SDL_INIT_EVERYTHING);
    ASSERT(result == 0, "Failed to init SDL: %s", SDL_GetError());

    SDL_Window *window = SDL_CreateWindow(
        "ztracing",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1280,
        720,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED
    );
    ASSERT(window, "Failed to create window: %s", SDL_GetError());

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT: {
                running = false;
            } break;
            }
        }
    }

    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}

#include "os_sdl.cpp"
