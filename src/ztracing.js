(function() {
  let currentSessionId = 0;

  async function loadFont(fontUrl) {
    try {
      const response = await fetch(fontUrl);
      if (response.ok) {
        const buffer = await response.arrayBuffer();
        const fontData = new Uint8Array(buffer);
        Module.ccall('ztracing_set_font_data', null, ['array', 'number'], [fontData, fontData.length]);
      } else {
        console.warn(`Font fetch failed: ${response.status} ${response.statusText}`);
      }
    } catch (e) {
      console.error('Font loading error:', e);
    }
  }

  function setupDragDrop(canvasSelector) {
    const canvas = document.querySelector(canvasSelector);
    if (!canvas) {
      console.warn('ztracing: drag and drop target not found:', canvasSelector);
      return;
    }

    canvas.addEventListener('dragover', (e) => {
      e.preventDefault();
      e.dataTransfer.dropEffect = 'copy';
    }, false);

    canvas.addEventListener('drop', async (e) => {
      e.preventDefault();
      const file = e.dataTransfer.files[0];
      if (!file) return;

      const sessionId = ++currentSessionId;
      let stream = file.stream();
      let statusMsg = `loading file: ${file.name} (${file.size} bytes)`;

      if (file.name.toLowerCase().endsWith('.gz') || file.name.toLowerCase().endsWith('.gzip')) {
        if (typeof DecompressionStream !== 'undefined') {
          stream = stream.pipeThrough(new DecompressionStream('gzip'));
          statusMsg += ' [decompressing gzip]';
        } else {
          console.warn('ztracing: DecompressionStream not supported in this browser, skipping decompression.');
        }
      }
      console.log(`${statusMsg}, session: ${sessionId}`);

      Module.ccall('ztracing_begin_session', null, ['number', 'string'], [sessionId, file.name]);

      const reader = stream.getReader();

      let lastYieldTime = performance.now();
      try {
        while (true) {
          if (sessionId !== currentSessionId) {
            console.log(`session ${sessionId} aborted`);
            break;
          }

          const { done, value } = await reader.read();
          if (done) break;

          const size = value.length;
          const ptr = Module._ztracing_malloc(size);
          Module.HEAPU8.set(value, ptr);
          Module._ztracing_handle_file_chunk(sessionId, ptr, size, false);
          Module._ztracing_free(ptr, size);

          const now = performance.now();
          if (now - lastYieldTime > 100) {
            // Yield to the browser's event loop every 100ms. This prevents the
            // async loop from starving the main thread, ensuring that
            // requestAnimationFrame and UI rendering can still fire.
            await new Promise(resolve => setTimeout(resolve, 0));
            lastYieldTime = performance.now();
          }
        }

        if (sessionId === currentSessionId) {
          // Signal EOF
          Module._ztracing_handle_file_chunk(sessionId, 0, 0, true);
        }
      } catch (err) {
        console.error(`error reading file: ${err}`);
      }
    }, false);
  }

  Module['ztracing_start'] = async function(options) {
    const { canvasSelector, fontUrl } = options;
    Module.ccall('ztracing_init', 'number', ['string'], [canvasSelector]);
    await loadFont(fontUrl);
    setupDragDrop(canvasSelector);
    Module.ccall('ztracing_start', null, [], []);
  };
})();
