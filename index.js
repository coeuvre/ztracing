async function setup(module, canvas) {
  const AppSetWindowSize = module.cwrap("AppSetWindowSize", null, [
    "number",
    "number",
  ]);
  const AppMemAlloc = module.cwrap("AppMemAlloc", "number", ["number"]);
  const AppCanLoadFile = module.cwrap("AppCanLoadFile", null, []);
  const AppOnLoadBegin = module.cwrap("AppOnLoadBegin", null, [
    "string",
    "number",
  ]);
  const AppCanLoadChunk = module.cwrap("AppCanLoadChunk", null, []);
  const AppOnLoadChunk = module.cwrap("AppOnLoadChunk", null, [
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
    if (AppCanLoadFile()) {
      AppOnLoadBegin(path, total);
      for await (const chunk of stream) {
        if (AppCanLoadChunk()) {
          const len = chunk.length * chunk.BYTES_PER_ELEMENT;
          const buf = AppMemAlloc(len);
          module.HEAPU8.set(chunk, buf);
          AppOnLoadChunk(buf, len);
        } else {
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
