async function setup(module, canvas) {
  const app_set_window_size = module.cwrap("app_set_window_size", null, [
    "number",
    "number",
  ]);
  const app_memory_allocate = module.cwrap("app_memory_allocate", "number", [
    "number",
  ]);
  const app_on_load_begin = module.cwrap("app_on_load_begin", null, [
    "string",
    "number",
  ]);
  const app_on_load_chunk = module.cwrap("app_on_load_chunk", null, [
    "nubmer",
    "number",
  ]);
  const app_on_load_end = module.cwrap("app_on_load_end", null, []);

  /**
   * @param {String} path
   * @param {number} total
   * @param {ReadableStream} stream
   */
  async function load_stream(path, total, stream) {
    app_on_load_begin(path, total);
    for await (const chunk of stream) {
      const len = chunk.length * chunk.BYTES_PER_ELEMENT;
      const buf = app_memory_allocate(len);
      module.HEAPU8.set(chunk, buf);
      app_on_load_chunk(buf, len);
    }
    app_on_load_end();
  }

  canvas.addEventListener("dragover", (event) => {
    event.preventDefault();
  });
  canvas.addEventListener("drop", (event) => {
    event.preventDefault();
    if (
      event.dataTransfer &&
      event.dataTransfer.files &&
      event.dataTransfer.files.length > 0
    ) {
      const file = event.dataTransfer.files[0];
      load_stream(file.name, file.size, file.stream());
    }
  });

  return {
    module,
    set_canvas_size: app_set_window_size,
  };
}

export default {
  mount: async function (options) {
    const init_module = options.init_module;
    const canvas = options.canvas;
    const module = await init_module({
      canvas: canvas,
    });
    return setup(module, canvas);
  },
};
