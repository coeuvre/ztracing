#include "src/platform.h"

#include <emscripten.h>

double platform_get_now() {
  return emscripten_get_now();
}

EM_JS(bool, platform_is_dark_mode, (), {
  return (window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches) ? 1 : 0;
});
