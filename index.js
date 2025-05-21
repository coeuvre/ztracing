import Module from "./ztracing.js";

var canvas = document.getElementById("canvas");
const ztracing = await Module({
  print(...args) {
    console.log(...args);
  },
  canvas: canvas,
});

const params = new URLSearchParams(location.search);
const profile = params.get("profile");
if (profile) {
  const response = await fetch(profile);
  const stream = response.body;
  ztracing.JS_LoadStream(profile, stream);
}
