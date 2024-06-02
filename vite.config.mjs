import { defineConfig } from "vite";

export default defineConfig(() => {
  return {
    worker: {
      format: "es",
    },
  };
});
