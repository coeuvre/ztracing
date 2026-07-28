mergeInto(LibraryManager.library, {
  ztracing_js_log: function(level, message) {
    const text = UTF8ToString(message);
    switch (level) {
      case 0: console.debug(text); break;
      case 1: console.log(text); break;
      case 2: console.warn(text); break;
      case 3: console.error(text); break;
      default: console.log(text); break;
    }
  },
  ztracing_platform_now: function() { return performance.now(); },
  ztracing_platform_is_main_thread: function() { return ENVIRONMENT_IS_PTHREAD ? 0 : 1; },
  ztracing_platform_is_dark_mode: function() {
    return window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches ? 1 : 0;
  },
  ztracing_platform_is_mac: function() {
    return /Mac|iPhone|iPod|iPad/i.test(navigator.userAgent || navigator.platform || '') ? 1 : 0;
  },
  ztracing_platform_is_software_renderer: function() {
    try {
      const gl = (typeof GL !== 'undefined' && GL.currentContext && GL.currentContext.GLctx)
          ? GL.currentContext.GLctx
          : null;
      const targetGl = gl || (() => {
        const canvas = document.querySelector('#canvas') || document.createElement('canvas');
        return canvas.getContext('webgl2') || canvas.getContext('webgl');
      })();
      if (!targetGl) return 0;
      const ext = targetGl.getExtension('WEBGL_debug_renderer_info');
      const renderer = ext ? (targetGl.getParameter(ext.UNMASKED_RENDERER_WEBGL) || '') : '';
      const vendor = ext ? (targetGl.getParameter(ext.UNMASKED_VENDOR_WEBGL) || '') : '';
      const stdRenderer = targetGl.getParameter(targetGl.RENDERER) || '';
      const combined = (renderer + ' ' + vendor + ' ' + stdRenderer);
      return /software|swiftshader|llvmpipe|softpipe|swrast|warp|subzero/i.test(combined) ? 1 : 0;
    } catch (e) {
      return 0;
    }
  },
  ztracing_platform_get_device_pixel_ratio: function() {
    return window.devicePixelRatio || 1.0;
  },
  ztracing_platform_set_setting: function(key, value) {
    localStorage.setItem(
        'ztracing_' + UTF8ToString(key), UTF8ToString(value));
  },
  ztracing_platform_get_setting: function(key, value, length) {
    const setting = localStorage.getItem('ztracing_' + UTF8ToString(key));
    if (setting === null) return 0;
    stringToUTF8(setting, value, length);
    return 1;
  },
  ztracing_platform_open_file_dialog: function() {
    const input = document.createElement('input');
    input.type = 'file'; input.accept = '.json,.gz';
    input.onchange = async function(event) {
      const file = event.target.files[0];
      if (!file) return;
      if (typeof Module !== 'undefined' &&
          typeof Module.ztracing_load_from_stream === 'function') {
        await Module.ztracing_load_from_stream(
            file.stream(), file.name, file.size, file.type);
      } else {
        Module.ccall(
            'ztracing_set_error', null, ['number', 'string'],
            [0, 'ztracing stream loader is unavailable']);
      }
    };
    input.click();
  }
});
