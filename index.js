import queryString from "query-string";

import ztracing from "./ztracing.js";
import ztraing_wasm_url from "./zig-out/bin/ztracing.wasm?url";
import font_url from "./assets/JetBrainsMono-Regular.ttf?url";

const canvas = document.getElementById("canvas");

window.onresize = () => {
  window.app.resize(window.innerWidth, window.innerHeight);
};

const query = queryString.parse(location.search);

ztracing.mount({
  ztraing_wasm_url,
  font: {
    url: font_url,
    size: 15.0,
  },
  canvas: canvas,
  width: window.innerWidth,
  height: window.innerHeight,
  profileUrl: query.profile,
  onMount: (app) => {
    // Making debug easier
    window.app = app;
  },
});
