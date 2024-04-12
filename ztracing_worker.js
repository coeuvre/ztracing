import { make_wasm_imports } from "./ztracing_shared.js";

onmessage = async function (e) {
  const { memory, ztracing_wasm_url, tid, arg } = e.data;
  const imports = make_wasm_imports(memory, undefined);
  const wasm = await WebAssembly.instantiateStreaming(
    fetch(ztracing_wasm_url),
    imports
  );
  wasm.instance.exports.wasi_thread_start(tid, arg);
  postMessage({});
};
