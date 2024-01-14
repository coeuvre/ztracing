import queryString from "query-string";

import ztracing from "./ztracing.js";
import ztracingWasmUrl from "./zig-out/bin/ztracing.wasm?url";

const canvas = document.getElementById("canvas");

window.onresize = () => {
  window.app.resize(window.innerWidth, window.innerHeight);
};

const query = queryString.parse(location.search);

ztracing.mount({
  ztracingWasmUrl: ztracingWasmUrl,
  canvas: canvas,
  width: window.innerWidth,
  height: window.innerHeight,
  profileUrl: query.profile,
  onMount: (app) => {
    // Making debug easier
    window.app = app;
  },
});
