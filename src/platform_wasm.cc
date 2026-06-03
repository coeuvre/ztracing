#include <emscripten.h>

#include "src/platform.h"

double platform_get_now() { return emscripten_get_now(); }

EM_JS(bool, platform_is_dark_mode, (), {
  return (window.matchMedia &&
          window.matchMedia('(prefers-color-scheme: dark)').matches)
             ? 1
             : 0;
});

EM_JS(bool, platform_is_mac, (), {
  return /Mac|iPhone|iPod|iPad/i.test(navigator.userAgent || navigator.platform || "");
});

EM_JS(void, platform_open_file_dialog, (), {
  const input = document.createElement('input');
  input.type = 'file';
  input.accept = '.json,.gz,.zip';
  input.onchange = async (e) => {
    const file = e.target.files[0];
    if (!file) return;
    if (typeof Module !== 'undefined' && typeof Module['ztracing_load_from_stream'] === 'function') {
      try {
        await Module['ztracing_load_from_stream'](file.stream(), file.name, file.size, file.type);
      } catch (err) {
        console.error('Failed to load selected file:', err);
      }
    } else {
      console.error('ztracing: Module.ztracing_load_from_stream not found');
    }
  };
  input.click();
});

