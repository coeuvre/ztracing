var LibraryZtracing = {
  $Ztracing__deps: ["$stringToUTF8", "$writeArrayToMemory", "$ccall"],
  $Ztracing: {
    LoadFile: async (/**@type {File}*/ file) => {
      await Ztracing.LoadStream(file.name, file.stream());
    },

    Alloc: (size) => {
      return ccall("JS_Alloc", "number", ["number"], [size]);
    },

    Free: (ptr, size) => {
      return ccall("JS_Free", "void", ["number", "number"], [ptr, size]);
    },

    LoadStream: async (
      /**@type {string}*/ name,
      /**@type {ReadableStream<Uint8Array>}*/ stream,
    ) => {
      const name_ptr = Ztracing.Alloc(name.length + 1);
      stringToUTF8(name, name_ptr, name.length + 1);
      const loading_file = ccall(
        "JS_LoadingFile_Begin",
        "number",
        ["number", "number"],
        [name_ptr, name.length],
      );
      Ztracing.Free(name_ptr, name.length + 1);
      for await (const chunk of stream) {
        const ptr = Ztracing.Alloc(chunk.length);
        writeArrayToMemory(chunk, ptr);
        while (true) {
          const sent = ccall(
            "JS_LoadingFile_OnChunk",
            "boolean",
            ["number", "number", "number"],
            [loading_file, ptr, chunk.length],
          );
          if (sent) {
            break;
          }
          // If the queue is full, give the main thread a chance to update the UI and try again.
          await new Promise((resolve) => {
            setTimeout(() => resolve(), 0);
          });
        }
      }
      ccall("JS_LoadingFile_End", "void", ["number"], [loading_file]);
    },
  },

  JS_Init: () => {
    const canvas = Module.canvas;

    canvas.addEventListener("drop", (event) => {
      event.preventDefault();

      if (
        event.dataTransfer &&
        event.dataTransfer.files &&
        event.dataTransfer.files.length > 0
      ) {
        const file = event.dataTransfer.files[0];
        Ztracing.LoadFile(file);
      }
    });

    canvas.addEventListener("dragover", (event) => {
      event.preventDefault();
    });

    Module.JS_LoadStream = Ztracing.LoadStream;
  },
};

autoAddDeps(LibraryZtracing, "$Ztracing");
addToLibrary(LibraryZtracing);
