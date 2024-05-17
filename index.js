export default {
  mount: async function (options) {
    const init_module = options.init_module;
    const canvas = options.canvas;

    const module = await init_module({
      canvas: canvas,
    });

    return {
      set_window_size: module.cwrap("app_set_window_size", null, [
        "number",
        "number",
      ]),
    };
  },
};
