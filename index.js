import queryString from "query-string";

import ztracing from "./ztracing.js";
import ztracingWasmUrl from "./zig-out/bin/ztracing.wasm?url";

const canvas = document.getElementById("canvas");
canvas.width = window.innerWidth;
canvas.height = window.innerHeight;

window.onresize = () => {
  canvas.width = window.innerWidth;
  canvas.height = window.innerHeight;
};

const query = queryString.parse(location.search);

ztracing.mount({
  ztracingWasmUrl: ztracingWasmUrl,
  canvas: canvas,
  profileUrl: query.profile,
  onMount: (app) => {
    // Making debug easier
    window.app = app;
  },
});
