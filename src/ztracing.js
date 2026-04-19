(function() {
let currentSessionId = 0;

async function loadFont(fontUrl) {
  try {
    const response = await fetch(fontUrl);
    if (response.ok) {
      const buffer = await response.arrayBuffer();
      const fontData = new Uint8Array(buffer);
      Module.ccall(
          'ztracing_set_font_data', null, ['array', 'number'],
          [fontData, fontData.length]);
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
  const {canvasSelector, fontUrl, trace} = options;
  Module.ccall('ztracing_init', 'number', ['string'], [canvasSelector]);
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
