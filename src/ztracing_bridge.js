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
  },
  ztracing_create_webgl_context: function(selector) {
    const canvas = document.querySelector(UTF8ToString(selector));
    if (!canvas) return 0;
    const context = GL.createContext(canvas, { majorVersion: 2, minorVersion: 0, alpha: false, antialias: false, premultipliedAlpha: false, depth: false, stencil: false, powerPreference: 'high-performance' });
    if (!context) return 0;
    GL.makeContextCurrent(context); return context;
  },
  ztracing_destroy_webgl_context: function(context) {
    GL.deleteContext(context);
  },
  ztracing_start_animation_loop: function() {
    function frame() { Module._ztracing_update(); requestAnimationFrame(frame); }
    requestAnimationFrame(frame);
  }
});
