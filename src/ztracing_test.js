'use strict';

const assert = require('node:assert/strict');
const {afterEach, beforeEach, test} = require('node:test');

const MAX_BUFFERED_BYTES = 32 * 1024 * 1024;

function loadBridge(overrides = {}) {
  const memory = overrides.wasmMemory || new WebAssembly.Memory({
    initial: 1,
    maximum: 4,
  });
  let nextPointer = 1024;
  const calls = {
    begin: [],
    clipboard: [],
    chunks: [],
    errors: [],
    frees: [],
    pasteEvents: 0,
  };
  global.Module = {
    wasmMemory: memory,
    ztracing_test_hooks_enabled: true,
    ccall(name, _result, _types, values) {
      if (name === 'ztracing_begin_session') {
        calls.begin.push(values);
        return 1;
      }
      if (name === 'ztracing_set_error') calls.errors.push(values);
      if (name === 'ztracing_init') return 0;
    },
    _ztracing_malloc(size) {
      const pointer = nextPointer;
      nextPointer += size;
      return pointer;
    },
    _ztracing_free(pointer, size) {
      calls.frees.push([pointer, size]);
    },
    _ztracing_handle_file_chunk(id, pointer, size, consumed, eof) {
      const bytes = size === 0 ?
          [] :
          Array.from(new Uint8Array(memory.buffer, pointer, size));
      calls.chunks.push({id, pointer, size, consumed, eof, bytes});
      return 0;
    },
    _ztracing_get_buffered_bytes() {
      return 0;
    },
    _imgui_impl_wasm_set_clipboard_text_from_js(pointer) {
      const heap = new Uint8Array(memory.buffer);
      let end = pointer;
      while (heap[end] !== 0) end += 1;
      calls.clipboard.push(
          new TextDecoder().decode(heap.subarray(pointer, end)));
    },
    _imgui_impl_wasm_trigger_paste_event() {
      calls.pasteEvents += 1;
    },
    ...overrides,
  };
  const path = require.resolve('./ztracing.js');
  delete require.cache[path];
  require(path);
  return {
    calls,
    memory,
    module: global.Module,
    hooks: global.Module.ztracing_test_hooks,
  };
}

function loadPlatformBridge() {
  global.LibraryManager = {library: {}};
  global.mergeInto = (target, functions) => Object.assign(target, functions);
  const path = require.resolve('./ztracing_bridge.js');
  delete require.cache[path];
  require(path);
  return global.LibraryManager.library;
}

function streamOf(...chunks) {
  return new ReadableStream({
    start(controller) {
      for (const chunk of chunks) controller.enqueue(Uint8Array.from(chunk));
      controller.close();
    },
  });
}

let originalLog;
let originalError;

beforeEach(() => {
  originalLog = console.log;
  originalError = console.error;
  console.log = () => {};
  console.error = () => {};
});

afterEach(() => {
  console.log = originalLog;
  console.error = originalError;
  delete global.Module;
  delete global.LibraryManager;
  delete global.mergeInto;
  delete global.ENVIRONMENT_IS_PTHREAD;
  delete global.UTF8ToString;
  delete global.stringToUTF8;
  delete global.localStorage;
  delete global.document;
  delete global.window;
});

test('successful chunks transfer ownership and end with an empty EOF', async () => {
  const bridge = loadBridge();
  await bridge.hooks.loadFromStream(
      streamOf([1, 2], [], [3]), 'trace.json', 3, 'application/json');
  assert.deepEqual(
      bridge.calls.chunks.map(chunk => ({
        size: chunk.size,
        eof: chunk.eof,
        bytes: chunk.bytes,
      })),
      [
        {size: 2, eof: false, bytes: [1, 2]},
        {size: 1, eof: false, bytes: [3]},
        {size: 0, eof: true, bytes: []},
      ]);
  assert.deepEqual(bridge.calls.frees, []);
});

test('copies use the current WebAssembly buffer after memory growth', () => {
  const memory = new WebAssembly.Memory({initial: 1, maximum: 4});
  const bridge = loadBridge({wasmMemory: memory});
  const oldBuffer = memory.buffer;
  memory.grow(1);
  assert.notEqual(memory.buffer, oldBuffer);
  bridge.hooks.setWasmMemory(70_000, Uint8Array.from([4, 5, 6]));
  assert.deepEqual(
      Array.from(new Uint8Array(memory.buffer, 70_000, 3)),
      [4, 5, 6]);
});

test('paste copies UTF-8 text, triggers ImGui, and frees its buffer', () => {
  const bridge = loadBridge();

  assert.equal(bridge.hooks.handlePaste('pasted ✓'), true);

  assert.deepEqual(bridge.calls.clipboard, ['pasted ✓']);
  assert.equal(bridge.calls.pasteEvents, 1);
  assert.deepEqual(bridge.calls.frees, [[1024, 11]]);
  assert.deepEqual(bridge.calls.errors, []);
});

test('paste allocation failure reports to the initialized Rust UI', () => {
  const bridge = loadBridge({
    _ztracing_malloc() {
      return 0;
    },
  });

  assert.equal(bridge.hooks.handlePaste('text'), false);

  assert.deepEqual(bridge.calls.clipboard, []);
  assert.equal(bridge.calls.pasteEvents, 0);
  assert.deepEqual(
      bridge.calls.errors,
      [[0, 'failed to allocate 5 bytes in WASM memory']]);
});

test('allocation failure cancels without writing through address zero', async () => {
  const bridge = loadBridge({
    _ztracing_malloc() {
      return 0;
    },
  });
  const before = new Uint8Array(bridge.memory.buffer, 0, 4).slice();
  await bridge.hooks.loadFromStream(
      streamOf([9, 9]), 'oom.json', 2, 'application/json');
  assert.deepEqual(
      new Uint8Array(bridge.memory.buffer, 0, 4),
      before);
  assert.deepEqual(
      bridge.calls.errors,
      [[1, 'failed to allocate 2 bytes in WASM memory']]);
  assert.deepEqual(bridge.calls.chunks, []);
});

test('a failed JavaScript copy frees its allocation exactly once', async () => {
  const memory = new WebAssembly.Memory({initial: 1, maximum: 1});
  const bridge = loadBridge({
    wasmMemory: memory,
    _ztracing_malloc() {
      return memory.buffer.byteLength - 1;
    },
  });
  await bridge.hooks.loadFromStream(
      streamOf([1, 2]), 'bounds.json', 2, 'application/json');
  assert.deepEqual(
      bridge.calls.frees,
      [[memory.buffer.byteLength - 1, 2]]);
  assert.deepEqual(bridge.calls.errors.length, 1);
  assert.equal(bridge.calls.errors[0][0], 1);
  assert.match(bridge.calls.errors[0][1], /out of bounds/);
  assert.deepEqual(bridge.calls.chunks, []);
});

test('a stream error is reported to its matching Rust session', async () => {
  const bridge = loadBridge();
  const stream = new ReadableStream({
    start(controller) {
      controller.error(new Error('read failed'));
    },
  });
  await bridge.hooks.loadFromStream(
      stream, 'broken.json', 1, 'application/json');
  assert.deepEqual(bridge.calls.errors, [[1, 'read failed']]);
  assert.deepEqual(bridge.calls.chunks, []);
});

test('backpressure yields asynchronously until buffered bytes drain', async () => {
  let polls = 0;
  const bridge = loadBridge({
    _ztracing_handle_file_chunk(id, pointer, size, consumed, eof) {
      bridge.calls.chunks.push({id, pointer, size, consumed, eof});
      return eof ? 0 : MAX_BUFFERED_BYTES + 1;
    },
    _ztracing_get_buffered_bytes() {
      polls += 1;
      return polls === 1 ? MAX_BUFFERED_BYTES + 1 : 0;
    },
  });
  const started = performance.now();
  await bridge.hooks.loadFromStream(
      streamOf([1]), 'large.json', 1, 'application/json');
  assert(polls >= 2);
  assert(performance.now() - started >= 10);
  assert.equal(bridge.calls.chunks.at(-1).eof, true);
});

test('a rejected Rust loader stops before reading the stream', async () => {
  let pulls = 0;
  const bridge = loadBridge({
    ccall(name, _result, _types, values) {
      if (name === 'ztracing_begin_session') return 0;
      return 0;
    },
  });
  const stream = {
    pipeThrough() {
      pulls += 1;
    },
  };

  await bridge.hooks.loadFromStream(
      stream, 'busy.json', 1, 'application/json');
  assert.equal(pulls, 0);
  assert.deepEqual(bridge.calls.chunks, []);
});

test('replacing a session immediately cancels a pending reader', async () => {
  let oldCancelled = false;
  const bridge = loadBridge();
  const oldStream = new ReadableStream({
    pull() {
      return new Promise(() => {});
    },
    cancel(reason) {
      oldCancelled = reason === 'session replaced';
    },
  });
  const oldLoad = bridge.hooks.loadFromStream(
      oldStream, 'old.json', 1, 'application/json');
  await new Promise(resolve => setTimeout(resolve, 0));

  await bridge.hooks.loadFromStream(
      streamOf([91, 93]), 'new.json', 2, 'application/json');
  await oldLoad;

  assert.equal(oldCancelled, true);
  assert.equal(bridge.calls.chunks.at(-1).id, 2);
});

test('gzip is detected from magic bytes regardless of MIME type', async () => {
  const {gzipSync} = require('node:zlib');
  const compressed = gzipSync(Buffer.from('[]'));
  const bridge = loadBridge();

  await bridge.hooks.loadFromStream(
      streamOf(...Array.from(compressed, byte => [byte])),
      'trace.bin', compressed.length,
      'application/octet-stream');

  const data = bridge.calls.chunks
      .filter(chunk => !chunk.eof)
      .flatMap(chunk => chunk.bytes);
  assert.deepEqual(data, [91, 93]);
});

test('gzip reports an error when browser decompression is unavailable', async () => {
  const {gzipSync} = require('node:zlib');
  const compressed = gzipSync(Buffer.from('[]'));
  const original = global.DecompressionStream;
  global.DecompressionStream = undefined;
  const bridge = loadBridge();
  try {
    await bridge.hooks.loadFromStream(
        streamOf(...Array.from(compressed, byte => [byte])),
        'trace.gz', compressed.length, '');
  } finally {
    global.DecompressionStream = original;
  }
  assert.deepEqual(
      bridge.calls.errors,
      [[1, 'this browser does not support gzip decompression']]);
});

test('application startup is idempotent and reserves onError for initialization', async () => {
  let initCalls = 0;
  let startCalls = 0;
  const initializationErrors = [];
  const uiErrors = [];
  global.document = {
    querySelector() {
      return {addEventListener() {}};
    },
  };
  global.window = {matchMedia: null};
  const bridge = loadBridge({
    ccall(name, _result, _types, values) {
      if (name === 'ztracing_init') {
        initCalls += 1;
        return 0;
      }
      if (name === 'ztracing_start') startCalls += 1;
      if (name === 'ztracing_set_error') uiErrors.push(values);
      return 1;
    },
  });
  const options = {
    canvasSelector: '#canvas',
    getFont: async () => {
      throw new Error('font failed');
    },
    onError(code, message) {
      initializationErrors.push({code, message});
    },
  };

  // Invoke ztracing_start twice concurrently to verify startup idempotency:
  // ztracing_init must be called only once and return the shared promise.
  await Promise.all([
    bridge.module.ztracing_start(options),
    bridge.module.ztracing_start(options),
  ]);
  assert.equal(initCalls, 1);
  assert.deepEqual(initializationErrors, []);
  assert.deepEqual(uiErrors, [[0, 'font failed']]);
});

test('an initialization exception is reported through onError', async () => {
  const errors = [];
  const bridge = loadBridge({
    ccall(name) {
      if (name === 'ztracing_init') throw new Error('WASM initialization failed');
    },
  });

  await bridge.module.ztracing_start({
    canvasSelector: '#canvas',
    onError(code, message) {
      errors.push({code, message});
    },
  });

  assert.deepEqual(
      errors,
      [{code: -1, message: 'WASM initialization failed'}]);
  assert.deepEqual(bridge.calls.errors, []);
});

test('platform bridge distinguishes browser main and pthread workers', () => {
  const platform = loadPlatformBridge();
  global.ENVIRONMENT_IS_PTHREAD = false;
  assert.equal(platform.ztracing_platform_is_main_thread(), 1);
  global.ENVIRONMENT_IS_PTHREAD = true;
  assert.equal(platform.ztracing_platform_is_main_thread(), 0);
});

test('platform settings round trip through the ztracing local-storage namespace', () => {
  const strings = new Map([
    [1, 'theme_mode'],
    [2, 'light'],
  ]);
  const storage = new Map();
  let copied;
  global.UTF8ToString = pointer => strings.get(pointer);
  global.stringToUTF8 = (value, pointer, length) => {
    copied = {length, pointer, value};
  };
  global.localStorage = {
    getItem: key => storage.get(key) ?? null,
    setItem: (key, value) => storage.set(key, value),
  };
  const platform = loadPlatformBridge();

  platform.ztracing_platform_set_setting(1, 2);
  assert.equal(storage.get('ztracing_theme_mode'), 'light');
  assert.equal(platform.ztracing_platform_get_setting(1, 3, 64), 1);
  assert.deepEqual(copied, {length: 64, pointer: 3, value: 'light'});
  strings.set(1, 'missing');
  assert.equal(platform.ztracing_platform_get_setting(1, 3, 64), 0);
});

test('platform file picker delegates to the non-throwing stream loader', async () => {
  let input;
  let clicked = false;
  const errors = [];
  global.document = {
    createElement() {
      input = {
        click() {
          clicked = true;
        },
      };
      return input;
    },
  };
  global.Module = {
    async ztracing_load_from_stream() {
      return;
    },
  };
  console.error = (...args) => errors.push(args.map(String).join(' '));
  const platform = loadPlatformBridge();

  platform.ztracing_platform_open_file_dialog();
  assert(clicked);
  assert.equal(input.type, 'file');
  assert.equal(input.accept, '.json,.gz');
  await input.onchange({
    target: {
      files: [{
        name: 'trace.json',
        size: 1,
        stream: () => new ReadableStream(),
        type: 'application/json',
      }],
    },
  });
  assert.deepEqual(errors, []);
});

test('platform file picker reports a missing stream loader', async () => {
  let input;
  const errors = [];
  global.document = {
    createElement() {
      input = {click() {}};
      return input;
    },
  };
  global.Module = {
    ccall(name, _result, _types, values) {
      errors.push({name, values});
    },
  };
  console.error = (...args) => errors.push(args.map(String).join(' '));
  const platform = loadPlatformBridge();

  platform.ztracing_platform_open_file_dialog();
  await input.onchange({
    target: {
      files: [{
        name: 'trace.json',
        size: 0,
        stream: () => new ReadableStream(),
        type: 'application/json',
      }],
    },
  });
  assert.deepEqual(errors, [{
    name: 'ztracing_set_error',
    values: [0, 'ztracing stream loader is unavailable'],
  }]);
});
