#include <emscripten.h>
#include <emscripten/threading.h>

#include "src/platform.h"

bool platform_is_main_thread(void) {
  return emscripten_is_main_browser_thread();
}

double platform_get_now() {
  double now = emscripten_get_now();
  return now;
}

/* clang-format off */
EM_JS(bool, platform_is_dark_mode, (), {
  return (window.matchMedia &&
          window.matchMedia('(prefers-color-scheme: dark)').matches)
             ? 1
             : 0;
})

EM_JS(bool, platform_is_mac, (), {
  return /Mac|iPhone|iPod|iPad/i.test(navigator.userAgent ||
                                       navigator.platform || "");
})

EM_JS(void, platform_open_file_dialog, (), {
  const input = document.createElement('input');
  input.type = 'file';
  input.accept = '.json,.gz,.zip';
  input.onchange = async (e) => {
    const file = e.target.files[0];
    if (!file) return;
    if (typeof Module !== 'undefined' &&
        typeof Module['ztracing_load_from_stream'] === 'function') {
      try {
        await Module['ztracing_load_from_stream'](file.stream(), file.name,
                                                  file.size, file.type);
      } catch (err) {
        console.error('Failed to load selected file:', err);
      }
    } else {
      console.error('ztracing: Module.ztracing_load_from_stream not found');
    }
  };
  input.click();
})

EM_JS(void, platform_set_setting, (const char* key, const char* value), {
  var keyStr = UTF8ToString(key);
  var valStr = UTF8ToString(value);
  localStorage.setItem("ztracing_" + keyStr, valStr);
})

EM_JS(bool, platform_get_setting, (const char* key, char* out_val, int max_len), {
  var keyStr = UTF8ToString(key);
  var val = localStorage.getItem("ztracing_" + keyStr);
  if (val === null) {
    return false;
  }
  stringToUTF8(val, out_val, max_len);
  return true;
})
/* clang-format on */
