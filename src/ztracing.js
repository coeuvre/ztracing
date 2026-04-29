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

function setFontData(buffer) {
  try {
    const fontData = new Uint8Array(buffer);
    const size = fontData.length;
    const ptr = Module._ztracing_malloc(size);
    setWasmMemory(ptr, fontData);
    Module.ccall(
        'ztracing_set_font_data', null, ['number', 'number'],
        [ptr, size]);
    Module._ztracing_free(ptr, size);
  } catch (e) {
    console.error('Font upload error:', e);
  }
}

async function loadFromStream(stream, name, sizeHint, contentType) {
  const sessionId = ++currentSessionId;
  let statusMsg = `loading: ${name}`;
  if (sizeHint) statusMsg += ` (${sizeHint} bytes)`;

  const isGzip = contentType &&
      (contentType === 'application/gzip' ||
       contentType === 'application/x-gzip');

  console.log(`${statusMsg}, session: ${sessionId}`);

  let inputTotalBytes = sizeHint || 0;
  Module.ccall(
      'ztracing_begin_session', null, ['number', 'string', 'number'],
      [sessionId, name, inputTotalBytes]);

  let inputProcessedBytes = 0;
  const progressTracker = new TransformStream({
    transform(chunk, controller) {
      inputProcessedBytes += chunk.length;
      controller.enqueue(chunk);
    }
  });

  stream = stream.pipeThrough(progressTracker);

  if (isGzip) {
    if (typeof DecompressionStream !== 'undefined') {
      stream = stream.pipeThrough(new DecompressionStream('gzip'));
    } else {
      console.warn(
          'ztracing: DecompressionStream not supported in this browser, skipping decompression.');
    }
  }

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

      // We pass the ABSOLUTE amount of RAW (compressed) bytes that were consumed so far.
      let queueSize = Module._ztracing_handle_file_chunk(sessionId, ptr, size, inputProcessedBytes, false);

      const MAX_QUEUE_SIZE = 32 * 1024 * 1024; // 32MB
      if (queueSize > MAX_QUEUE_SIZE) {
        while (queueSize > MAX_QUEUE_SIZE) {
          // Wait for the worker to process some chunks.
          await new Promise(resolve => setTimeout(resolve, 10));
          queueSize = Module._ztracing_get_queue_size();
        }
        lastYieldTime = performance.now();
      }

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
      Module._ztracing_handle_file_chunk(sessionId, 0, 0, inputProcessedBytes, true);
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

/**
 * Starts the ztracing application.
 *
 * @param {Object} options - Configuration options.
 * @param {string} options.canvasSelector - CSS selector for the target canvas.
 * @param {Function} [options.getFont] - Async function returning an ArrayBuffer with font data.
 * @param {Function} [options.getTrace] - Async function returning a trace object (stream, name, size, contentType).
 * @param {Function} [options.onError] - Callback for handling initialization or loading errors.
 */
Module['ztracing_start'] = async function(options) {
  const {canvasSelector, getFont, getTrace, onError} = options;
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

  // 1. Start font fetch and trace fetch in parallel
  const fontPromise = typeof getFont === 'function' ? getFont() : Promise.resolve(null);
  
  (async () => {
    try {
      if (typeof getTrace === 'function') {
        const trace = await getTrace();
        if (trace) {
          await loadFromStream(trace.stream, trace.name, trace.size, trace.contentType);
        } else {
          setupDragDrop(canvasSelector);
        }
      } else {
        setupDragDrop(canvasSelector);
      }
    } catch (e) {
      if (typeof onError === 'function') {
        onError(0, e.message);
      } else {
        console.error('Trace loading error:', e);
      }
    }
  })();

  // 2. WAIT for font as required before main loop
  try {
    const fontBuffer = await fontPromise;
    if (fontBuffer) {
      setFontData(fontBuffer);
    }
  } catch (e) {
    console.error('Font loading error:', e);
  }

  if (window.matchMedia) {
    const media = window.matchMedia('(prefers-color-scheme: dark)');
    media.addEventListener('change', () => {
      Module.ccall('ztracing_on_theme_changed', null, [], []);
    });
  }

  // 3. Enter main loop
  Module.ccall('ztracing_start', null, [], []);
};
})();
