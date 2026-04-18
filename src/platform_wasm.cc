#include "src/platform.h"

#include <emscripten.h>

double platform_get_now() {
  return emscripten_get_now();
}
