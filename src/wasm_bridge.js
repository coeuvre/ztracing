(function() {
  window.addEventListener('dragover', (e) => {
    e.preventDefault();
    e.dataTransfer.dropEffect = 'copy';
  }, false);

  window.addEventListener('drop', async (e) => {
    e.preventDefault();
    const file = e.dataTransfer.files[0];
    if (!file) return;

    console.log(`loading file: ${file.name} (${file.size} bytes)`);

    // Reset/Initialize parser in WASM
    Module._ztracing_handle_file_chunk(0, 0, false);

    const stream = file.stream();
    const reader = stream.getReader();

    let lastYieldTime = performance.now();
    try {
      while (true) {
        const { done, value } = await reader.read();
        if (done) break;

        const size = value.length;
        const ptr = Module._ztracing_malloc(size);
        Module.HEAPU8.set(value, ptr);
        Module._ztracing_handle_file_chunk(ptr, size, false);
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

      // Signal EOF
      Module._ztracing_handle_file_chunk(0, 0, true);
    } catch (err) {
      console.error(`error reading file: ${err}`);
    }
  }, false);
})();
