import ztracing from "./ztracing.js";
import ztracingWasmUrl from "/zig-out/lib/ztracing.wasm?url";

const canvas = document.getElementById("canvas");
canvas.width = window.innerWidth;
canvas.height = window.innerHeight;

window.onresize = () => {
  canvas.width = window.innerWidth;
  canvas.height = window.innerHeight;
};

ztracing.mount({
  ztracingWasmUrl: ztracingWasmUrl,
  canvas: canvas,
  onMount: (app) => {
    // Making debug easier
    window.app = app;
  },
});
