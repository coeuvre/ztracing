import { make_wasm_imports, WorkerEvent } from "./ztracing_shared.js";

/** @type {WebAssembly.WebAssemblyInstantiatedSource} */
var wasm;
var worker_ptr;

onmessage = function (e) {
  handle(e.data.message).then(
    (value) => {
      postMessage({ id: e.data.id, resolve: { value } });
    },
    (reason) => {
      postMessage({ id: e.data.id, reject: { reason } });
    }
  );
};

async function handle(message) {
  switch (message.event) {
    case WorkerEvent.load:
      await load(message.args);
      break;

    case WorkerEvent.init:
      await init(message.args);
      break;
  }
}

async function load(args) {
  const { memory, ztracing_wasm_url } = args;
  const imports = make_wasm_imports(memory, undefined);
  wasm = await WebAssembly.instantiateStreaming(
    fetch(ztracing_wasm_url),
    imports
  );
}

async function init(args) {
  const { shared_state } = args;
  worker_ptr = wasm.instance.exports.init(shared_state);
  console.log(worker_ptr);
}
