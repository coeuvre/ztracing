async function setup(module, canvas, width, height) {
  const AppSetWindowSize = module.cwrap("AppSetWindowSize", null, [
    "number",
    "number",
  ]);
  const AppAllocateMemory = module.cwrap("AppAllocateMemory", "number", [
    "number",
  ]);
  const AppOnLoadBegin = module.cwrap("AppOnLoadBegin", "boolean", [
    "string",
    "number",
  ]);
  const AppOnLoadChunk = module.cwrap("AppOnLoadChunk", "boolean", [
    "nubmer",
    "number",
  ]);
  const AppOnLoadEnd = module.cwrap("AppOnLoadEnd", null, []);

  /**
   * @param {String} path
   * @param {number} total
   * @param {ReadableStream} stream
   */
  async function loadProfile(path, total, stream) {
    if (AppOnLoadBegin(path, total)) {
      try {
        for await (const chunk of stream) {
          const len = chunk.length * chunk.BYTES_PER_ELEMENT;
          const buf = AppAllocateMemory(len);
          module.HEAPU8.set(chunk, buf);
          if (!AppOnLoadChunk(buf, len)) {
            break;
          }
        }
      } finally {
        AppOnLoadEnd();
      }
    }
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
      loadProfile(file.name, file.size, file.stream());
    }
  });

  if (!module.AppSetupResolve) {
    await new Promise((resolve) => {
      module.AppSetupResolve = resolve;
    });
  }

  function setWindowSize(width, height) {
    AppSetWindowSize(
      width * window.devicePixelRatio,
      height * window.devicePixelRatio,
    );
    canvas.style.width = `${width}px`;
    canvas.style.height = `${height}px`;
  }

  setWindowSize(width, height);

  return {
    module,
    setWindowSize,
    loadProfile,
  };
}

function print(text) {
  if (text.startsWith("ERROR: ")) {
    console.error(text);
  } else if (text.startsWith("WARN: ")) {
    console.warn(text);
  } else if (text.startsWith("INFO: ")) {
    console.info(text);
  } else {
    console.log(text);
  }
}

export default {
  mount: async function (options) {
    const initModule = options.initModule;
    const canvas = options.canvas;
    const module = await initModule({
      canvas: canvas,
      print: print,
      printErr: print,
    });
    return setup(module, canvas, options.width, options.height);
  },
};
