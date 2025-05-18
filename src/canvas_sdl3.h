#ifndef ZTRACING_SRC_CANVS_SDL3_H_
#define ZTRACING_SRC_CANVS_SDL3_H_

#include <SDL3/SDL.h>

#include "src/flick.h"
#include "src/memory.h"

FL_Canvas Canvas_Init(SDL_Window *window, SDL_Renderer *renderer,
                      Allocator allocator);

#endif  // ZTRACING_SRC_CANVS_SDL3_H_
