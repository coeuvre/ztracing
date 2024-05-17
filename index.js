export default {
  mount: async function (options) {
    const init_module = options.init_module;
    const canvas = options.canvas;

    const module = await init_module({
      canvas: canvas,
    });

    const app_set_window_size = module.cwrap("app_set_window_size", null, [
      "number",
      "number",
    ]);
    const app_load_file = module.cwrap("app_load_file", null, ["string"]);

    const FS = module.FS;
    FS.mkdir("/uploads");

    canvas.addEventListener("dragover", (event) => {
      event.preventDefault();
    });
    canvas.addEventListener("drop", async (event) => {
      event.preventDefault();
      if (
        event.dataTransfer &&
        event.dataTransfer.files &&
        event.dataTransfer.files.length > 0
      ) {
        const file = event.dataTransfer.files[0];
        console.log(file);
        // const reader = new FileReader();
        // reader.onloadend = (event) => {
        //   console.log(event);
        //   const path = "/uploads/" + file.name;
        //   FS.writeFile(path, new DataView(reader.result));
        //   app_load_file(path);
        // };
        // reader.readAsArrayBuffer(file);
      }
    });

    return {
      module,
      set_window_size: app_set_window_size,
    };
  },
};
