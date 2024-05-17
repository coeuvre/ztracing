import { fileURLToPath, URL } from "url";
import { defineConfig } from "vite";

export default defineConfig(({ mode }) => {
  const prod = mode == "production";
  return {
    resolve: {
      alias: {
        "@": prod
          ? fileURLToPath(new URL("./build_web/Release", import.meta.url))
          : fileURLToPath(new URL("./build_web/Debug", import.meta.url)),
      },
    },
  };
});
