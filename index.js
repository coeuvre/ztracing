async function setup(module, canvas) {
  const AppSetWindowSize = module.cwrap("AppSetWindowSize", null, [
    "number",
    "number",
  ]);
  const AppMemoryAlloc = module.cwrap("AppMemoryAlloc", "number", ["number"]);
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
      for await (const chunk of stream) {
        const len = chunk.length * chunk.BYTES_PER_ELEMENT;
        const buf = AppMemoryAlloc(len);
        module.HEAPU8.set(chunk, buf);
        if (!AppOnLoadChunk(buf, len)) {
          break;
        }
      }
      AppOnLoadEnd();
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

  return {
    module,
    setCanvasSize: AppSetWindowSize,
    loadProfile: loadProfile,
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
    return setup(module, canvas);
  },
};
