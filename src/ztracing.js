(function() {
let currentSessionId = 0;
let activeReader = null;
let startPromise = null;
const MAX_BUFFERED_BYTES = 32 * 1024 * 1024; // 32MB backpressure threshold

function setUiError(sessionId, error) {
  const message = error instanceof Error ? error.message : String(error);
  Module.ccall(
      'ztracing_set_error', null, ['number', 'string'],
      [sessionId, message]);
}

function setWasmMemory(ptr, value) {
  const size = value.length;
  ptr = ptr >>> 0;
  // Use wasmMemory.buffer directly as the source of truth. Memory growth can
  // replace the previous buffer, so create a fresh view for every copy.
  const buffer = Module.wasmMemory ? Module.wasmMemory.buffer : Module.HEAPU8.buffer;
  const heap = new Uint8Array(buffer);
  if (ptr + size > heap.length) {
    // Do not continue with a stale or invalid pointer: heap.set() would either
    // throw less contextually or write to an unintended location.
    throw new RangeError(
        `WASM buffer is out of bounds: ptr=${ptr}, size=${size}, memory=${heap.length}`);
  }
  heap.set(value, ptr);
}

function allocateWasmBuffer(value) {
  const size = value.length;
  const ptr = Module._ztracing_malloc(size);
  // A positive-size allocation must never be copied through address zero.
  if (!ptr && size !== 0) {
    throw new Error(`failed to allocate ${size} bytes in WASM memory`);
  }
  try {
    setWasmMemory(ptr, value);
    return ptr;
  } catch (error) {
    // Ownership has not crossed into Rust if the JavaScript copy failed.
    Module._ztracing_free(ptr, size);
    throw error;
  }
}

function handlePaste(text) {
  let ptr = 0;
  let size = 0;
  try {
    const encoded = new TextEncoder().encode(text);
    const terminated = new Uint8Array(encoded.length + 1);
    terminated.set(encoded);
    size = terminated.length;
    ptr = allocateWasmBuffer(terminated);
    Module._imgui_impl_wasm_set_clipboard_text_from_js(ptr);
    Module._imgui_impl_wasm_trigger_paste_event();
    return true;
  } catch (error) {
    setUiError(0, error);
    return false;
  } finally {
    if (ptr) Module._ztracing_free(ptr, size);
  }
}

function asUint8Array(chunk) {
  if (chunk instanceof Uint8Array) return chunk;
  if (chunk instanceof ArrayBuffer) return new Uint8Array(chunk);
  if (ArrayBuffer.isView(chunk)) {
    return new Uint8Array(chunk.buffer, chunk.byteOffset, chunk.byteLength);
  }
  throw new TypeError('trace streams must produce ArrayBuffer or typed-array chunks');
}

function beginGzipSniff(stream) {
  const sourceReader = stream.getReader();
  const result = (async () => {
    const leadingChunks = [];
    const magic = [];
    let sourceDone = false;

    while (magic.length < 2) {
      const {done, value} = await sourceReader.read();
      if (done) {
        sourceDone = true;
        break;
      }
      const chunk = asUint8Array(value);
      if (chunk.length === 0) continue;
      leadingChunks.push(chunk);
      for (let i = 0; i < chunk.length && magic.length < 2; ++i) {
        magic.push(chunk[i]);
      }
    }

    let leadingIndex = 0;
    let released = false;
    function releaseSource() {
      if (!released) {
        released = true;
        sourceReader.releaseLock();
      }
    }

    const replayedStream = new ReadableStream({
      async pull(controller) {
        if (leadingIndex < leadingChunks.length) {
          controller.enqueue(leadingChunks[leadingIndex++]);
          return;
        }
        if (sourceDone) {
          controller.close();
          releaseSource();
          return;
        }
        try {
          const {done, value} = await sourceReader.read();
          if (done) {
            sourceDone = true;
            controller.close();
            releaseSource();
          } else {
            controller.enqueue(asUint8Array(value));
          }
        } catch (error) {
          releaseSource();
          controller.error(error);
        }
      },
      async cancel(reason) {
        try {
          await sourceReader.cancel(reason);
        } finally {
          releaseSource();
        }
      },
    });
    return {
      isGzip: magic.length === 2 && magic[0] === 0x1f && magic[1] === 0x8b,
      stream: replayedStream,
    };
  })();
  return {reader: sourceReader, result};
}

function setFontData(buffer) {
  let ptr = 0;
  let size = 0;
  try {
    const fontData = new Uint8Array(buffer);
    size = fontData.length;
    ptr = allocateWasmBuffer(fontData);
    Module.ccall(
        'ztracing_set_font_data', null, ['number', 'number'],
        [ptr, size]);
  } catch (e) {
    setUiError(0, e);
  } finally {
    if (ptr) Module._ztracing_free(ptr, size);
  }
}

async function loadFromStream(stream, name, sizeHint, contentType) {
  const sessionId = ++currentSessionId;
  if (activeReader) {
    const previousReader = activeReader;
    activeReader = null;
    try {
      Promise.resolve(previousReader.cancel('session replaced')).catch(() => {});
    } catch (_) {
      // The previous stream may already have released its reader.
    }
  }

  let statusMsg = `loading: ${name}`;
  if (sizeHint) statusMsg += ` (${sizeHint} bytes)`;

  console.log(`${statusMsg}, session: ${sessionId}`);

  let inputTotalBytes = sizeHint || 0;
  const accepted = Module.ccall(
      'ztracing_begin_session', 'number', ['number', 'string', 'number'],
      [sessionId, name, inputTotalBytes]);
  if (!accepted) {
    return;
  }

  let inputProcessedBytes = 0;
  let reader = null;
  let sniff;
  try {
    const progressTracker = new TransformStream({
      transform(chunk, controller) {
        const bytes = asUint8Array(chunk);
        inputProcessedBytes += bytes.length;
        controller.enqueue(bytes);
      }
    });
    stream = stream.pipeThrough(progressTracker);
    sniff = beginGzipSniff(stream);
  } catch (error) {
    setUiError(sessionId, error);
    return;
  }
  activeReader = sniff.reader;

  let sniffed;
  try {
    sniffed = await sniff.result;
  } catch (error) {
    if (activeReader === sniff.reader) activeReader = null;
    try {
      await sniff.reader.cancel(error);
    } catch (_) {
      // The source stream may already be errored or cancelled.
    }
    sniff.reader.releaseLock();
    if (sessionId === currentSessionId) {
      setUiError(sessionId, error);
      return;
    }
    return;
  }
  if (sessionId !== currentSessionId) {
    await sniffed.stream.cancel('session replaced');
    return;
  }

  stream = sniffed.stream;
  const isGzip = sniffed.isGzip;
  try {
    if (isGzip) {
      if (typeof DecompressionStream === 'undefined') {
        throw new Error('this browser does not support gzip decompression');
      }
      stream = stream.pipeThrough(new DecompressionStream('gzip'));
    }
    reader = stream.getReader();
  } catch (error) {
    if (activeReader === sniff.reader) activeReader = null;
    if (sessionId === currentSessionId) {
      setUiError(sessionId, error);
    }
    try {
      await stream.cancel(error);
    } catch (_) {
      // Stream setup can fail after the source has already errored.
    }
    return;
  }

  activeReader = reader;

  let lastYieldTime = performance.now();
  try {
    while (true) {
      if (sessionId !== currentSessionId) {
        console.log(`session ${sessionId} aborted`);
        await reader.cancel('session replaced');
        break;
      }

      const {done, value} = await reader.read();
      if (done) break;

      const size = value.length;
      if (size === 0) continue;
      let ptr = allocateWasmBuffer(value);
      let bufferedBytes;
      try {
        bufferedBytes = Module._ztracing_handle_file_chunk(
            sessionId, ptr, size, inputProcessedBytes, false);
        ptr = 0;  // Rust took ownership.
      } finally {
        // Reclaim the buffer if the ownership-transferring call did not return.
        if (ptr) Module._ztracing_free(ptr, size);
      }

      // Apply backpressure if the queue exceeds 32MB
      if (bufferedBytes > MAX_BUFFERED_BYTES) {
        let currentBuffered = bufferedBytes;
        while (sessionId === currentSessionId &&
               currentBuffered > MAX_BUFFERED_BYTES) {
          await new Promise(resolve => setTimeout(resolve, 10));
          currentBuffered = Module._ztracing_get_buffered_bytes();
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
    if (sessionId === currentSessionId) {
      setUiError(sessionId, err);
    } else {
      return;
    }
    try {
      await reader.cancel(err);
    } catch (_) {
      // The stream may already be errored or closed.
    }
    return;
  } finally {
    if (activeReader === reader) activeReader = null;
    reader.releaseLock();
  }
}

Module['ztracing_load_from_stream'] = loadFromStream;
Module['ztracing_handle_paste'] = handlePaste;

// Tests opt in to these internal hooks before loading this pre-JS file. They
// are absent from the production Module surface.
if (Module['ztracing_test_hooks_enabled']) {
  Module['ztracing_test_hooks'] = {
    allocateWasmBuffer,
    beginGzipSniff,
    handlePaste,
    loadFromStream,
    setWasmMemory,
  };
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

async function startApplication(options) {
  const {canvasSelector, getFont, getTrace, onError} = options;
  let result;
  try {
    result = Module.ccall(
        'ztracing_init', 'number', ['string'], [canvasSelector]);
  } catch (error) {
    if (typeof onError === 'function') onError(-1, error.message);
    return;
  }
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
    if (typeof getTrace !== 'function') {
      setupDragDrop(canvasSelector);
      return;
    }
    let trace;
    try {
      trace = await getTrace();
    } catch (error) {
      setUiError(0, error);
      return;
    }
    if (trace) {
      await loadFromStream(
          trace.stream, trace.name, trace.size, trace.contentType);
    } else {
      setupDragDrop(canvasSelector);
    }
  })();

  // 2. WAIT for font as required before main loop
  try {
    const fontBuffer = await fontPromise;
    if (fontBuffer) {
      setFontData(fontBuffer);
    }
  } catch (e) {
    setUiError(0, e);
  }

  if (window.matchMedia) {
    const media = window.matchMedia('(prefers-color-scheme: dark)');
    media.addEventListener('change', (e) => {
      Module.ccall('ztracing_on_theme_changed', null, ['number'], [e.matches ? 1 : 0]);
    });
  }

  // 3. Enter main loop
  Module.ccall('ztracing_start', null, [], []);
}

/**
 * Starts the ztracing application.
 *
 * @param {Object} options - Configuration options.
 * @param {string} options.canvasSelector - CSS selector for the target canvas.
 * @param {Function} [options.getFont] - Async function returning an ArrayBuffer with font data.
 * @param {Function} [options.getTrace] - Async function returning a trace object (stream, name, size, contentType).
 * @param {Function} [options.onError] - Called only when the Rust UI cannot be
 *     initialized. Once initialized, errors are rendered by the Rust UI so
 *     users can dismiss them and continue using the application.
 */
Module['ztracing_start'] = function(options) {
  if (!startPromise) startPromise = startApplication(options);
  return startPromise;
}

})();
