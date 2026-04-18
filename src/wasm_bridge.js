(function() {
  let currentSessionId = 0;

  window.addEventListener('dragover', (e) => {
    e.preventDefault();
    e.dataTransfer.dropEffect = 'copy';
  }, false);

  window.addEventListener('drop', async (e) => {
    e.preventDefault();
    const file = e.dataTransfer.files[0];
    if (!file) return;

    const sessionId = ++currentSessionId;
    console.log(`loading file: ${file.name} (${file.size} bytes), session: ${sessionId}`);

    Module.ccall('ztracing_begin_session', null, ['number', 'string'], [sessionId, file.name]);

    const stream = file.stream();
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
})();
