import init_module from "./build_web/Debug/ztracing.js";

export default {
  mount: async function (options) {
    const canvas = options.canvas;

    const module = await init_module({
      canvas: canvas,
    });

    return {
      set_window_size: module.cwrap(
        "app_set_window_size",
        "number",
        "number",
        [],
      ),
    };
  },
};
