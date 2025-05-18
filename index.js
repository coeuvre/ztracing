import Module from "./ztracing.js";

var canvas = document.getElementById("canvas");
await Module({
  print(...args) {
    console.log(...args);
  },
  canvas: canvas,
});
