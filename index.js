import ztracing from "./ztracing.js";

const canvas = document.getElementById("canvas");
canvas.width = window.innerWidth;
canvas.height = window.innerHeight;

window.onresize = () => {
  canvas.width = window.innerWidth;
  canvas.height = window.innerHeight;
};

ztracing.mount(canvas, (app) => {
  // Making debug easier
  window.app = app;
});
