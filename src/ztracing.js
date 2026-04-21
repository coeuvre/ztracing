(function() {
let currentSessionId = 0;

function setWasmMemory(ptr, value) {
  const size = value.length;
  ptr = ptr >>> 0;
  // Use wasmMemory.buffer directly as it's the source of truth.
  // We create a fresh view to ensure we have the current length.
  const buffer = Module.wasmMemory ? Module.wasmMemory.buffer : Module.HEAPU8.buffer;
  const heap = new Uint8Array(buffer);

  if (ptr + size > heap.length) {
    console.error(`ztracing: Memory growth sync issue. ptr=${ptr} size=${size} heap.length=${heap.length} buffer.byteLength=${buffer.byteLength}`);
    // Fallback: try to refresh Module properties if they exist
    if (typeof lib_updateGlobalBufferViews !== 'undefined') lib_updateGlobalBufferViews();
  }

  heap.set(value, ptr);
}

async function loadFont(fontUrl) {
  try {
    const response = await fetch(fontUrl);
    if (response.ok) {
      const buffer = await response.arrayBuffer();
      const fontData = new Uint8Array(buffer);
      const size = fontData.length;
      const ptr = Module._ztracing_malloc(size);
      setWasmMemory(ptr, fontData);
      Module.ccall(
          'ztracing_set_font_data', null, ['number', 'number'],
          [ptr, size]);
      Module._ztracing_free(ptr, size);
    } else {
      console.warn(
          `Font fetch failed: ${response.status} ${response.statusText}`);
    }
  } catch (e) {
    console.error('Font loading error:', e);
  }
}

async function loadFromStream(stream, name, sizeHint, contentType) {
  const sessionId = ++currentSessionId;
  let statusMsg = `loading: ${name}`;
  if (sizeHint) statusMsg += ` (${sizeHint} bytes)`;

  const isGzip = contentType &&
      (contentType === 'application/gzip' ||
       contentType === 'application/x-gzip');

  if (isGzip) {
    if (typeof DecompressionStream !== 'undefined') {
      stream = stream.pipeThrough(new DecompressionStream('gzip'));
      statusMsg += ' [decompressing gzip]';
    } else {
      console.warn(
          'ztracing: DecompressionStream not supported in this browser, skipping decompression.');
    }
  }
  console.log(`${statusMsg}, session: ${sessionId}`);

  Module.ccall(
      'ztracing_begin_session', null, ['number', 'string'], [sessionId, name]);

  const reader = stream.getReader();

  let lastYieldTime = performance.now();
  try {
    while (true) {
      if (sessionId !== currentSessionId) {
        console.log(`session ${sessionId} aborted`);
        break;
      }

      const {done, value} = await reader.read();
      if (done) break;

      const size = value.length;
      const ptr = Module._ztracing_malloc(size);
      setWasmMemory(ptr, value);
      Module._ztracing_handle_file_chunk(sessionId, ptr, size, false);

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
    console.error(`error reading: ${err}`);
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
    await loadFromStream(file.stream(), file.name, file.size, file.type);
  }, false);
}

Module['ztracing_start'] = async function(options) {
  const {canvasSelector, fontUrl, trace, onError} = options;
  const result = Module.ccall('ztracing_init', 'number', ['string'], [canvasSelector]);
  if (result !== 0) {
    if (typeof onError === 'function') {
      let message = 'Unknown initialization error';
      if (result === 1) {
        message = 'Failed to create WebGL 2.0 context. Please ensure your browser supports WebGL 2 and it is not disabled.';
      } else if (result === 2) {
        message = 'Failed to initialize WebGL renderer (shader compilation or linking failed).';
      }
      onError(result, message);
    }
    return;
  }
  await loadFont(fontUrl);

  if (window.matchMedia) {
    const media = window.matchMedia('(prefers-color-scheme: dark)');
    media.addEventListener('change', () => {
      Module.ccall('ztracing_on_theme_changed', null, [], []);
    });
  }

  if (trace) {
    loadFromStream(trace.stream, trace.name, trace.size, trace.contentType);
  } else {
    setupDragDrop(canvasSelector);
  }

  Module.ccall('ztracing_start', null, [], []);
};
})();
