'use strict';

const assert = require('node:assert/strict');
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const {gzipSync} = require('node:zlib');
const {after, before, test} = require('node:test');

// The Bazel lifecycle action installs Chromium beside playwright-core.
process.env.PLAYWRIGHT_BROWSERS_PATH = '0';
const {chromium} = require('playwright');

const runfiles = process.env.JS_BINARY__RUNFILES;
const workspace = process.env.JS_BINARY__WORKSPACE;
const bundle = path.join(runfiles, workspace, 'ztracing');
const contentTypes = {
  '.html': 'text/html',
  '.js': 'text/javascript',
  '.wasm': 'application/wasm',
};

let browser;
let origin;
let server;

before(async () => {
  server = http.createServer((request, response) => {
    response.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
    response.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
    response.setHeader('Cross-Origin-Resource-Policy', 'same-origin');
    const url = new URL(request.url, 'http://localhost');
    const basename = url.pathname === '/' ? 'index.html' :
                                           path.basename(url.pathname);
    const filename = path.join(bundle, basename);
    const extension = path.extname(filename);
    if (!contentTypes[extension] || !fs.existsSync(filename)) {
      response.statusCode = 404;
      response.end('not found');
      return;
    }
    response.setHeader('Content-Type', contentTypes[extension]);
    fs.createReadStream(filename).pipe(response);
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  origin = `http://127.0.0.1:${server.address().port}`;
  browser = await chromium.launch({
    headless: true,
    args: [
      '--enable-unsafe-swiftshader',
      '--no-sandbox',
      '--use-angle=swiftshader',
    ],
  });
});

after(async () => {
  if (browser) await browser.close();
  if (server) await new Promise(resolve => server.close(resolve));
});

async function openApplication(themeMode, colorScheme) {
  const page = await browser.newPage({viewport: {width: 800, height: 600}});
  const failures = [];

  if (colorScheme) {
    await page.emulateMedia({colorScheme});
  }

  await page.addInitScript(mode => {
    if (mode === undefined) {
      localStorage.removeItem('ztracing_theme_mode');
    } else {
      localStorage.setItem('ztracing_theme_mode', mode);
    }
  }, themeMode);

  // Keep this test hermetic while exercising the real shell. Font rendering is
  // outside this suite's scope, so satisfy shell.html's font request without
  // making an external network request.
  await page.addInitScript(() => {
    const fetch = window.fetch.bind(window);
    window.fetch = (input, init) => {
      if (String(input).startsWith('https://fonts.gstatic.com/')) {
        return Promise.resolve({
          ok: true,
          arrayBuffer: async () => null,
        });
      }
      return fetch(input, init);
    };
  });

  page.on('console', message => {
    const text = message.text();
    if (text === 'software renderer detected, disabling hidpi') {
      return;
    }
    if (message.type() === 'error' ||
        text.includes('thread pool is exhausted') ||
        text.includes('deadlock')) {
      failures.push(`console ${message.type()}: ${text}`);
    }
  });
  page.on('pageerror', error => failures.push(`page error: ${error.stack}`));
  await page.goto(origin, {waitUntil: 'load'});
  await page.waitForFunction(
      () => typeof Module !== 'undefined' &&
          Module.calledRun === true &&
          typeof Module._ztracing_is_loading_active === 'function',
      null,
      {timeout: 30_000});
  assert.equal(await page.evaluate(() => crossOriginIsolated), true);
  assert.equal(await page.title(), 'ztracing');
  assert.equal(
      await page.locator('#error-container').evaluate(
          element => getComputedStyle(element).display),
      'none');
  return {failures, page};
}

async function captureViewport(page) {
  await page.evaluate(() => new Promise(
      resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
  return page.locator('#canvas').screenshot();
}

async function waitUntilIdle(page) {
  await page.waitForFunction(
      () => !Module._ztracing_is_loading_active() &&
          Module._ztracing_get_buffered_bytes() === 0,
      null,
      {timeout: 30_000});
}

async function selectWelcomeFile(page, file) {
  const chooserPromise = page.waitForEvent('filechooser');
  await page.mouse.click(400, 330);
  const chooser = await chooserPromise;
  await chooser.setFiles(file);
}

test('production threaded WASM handles growth, cancellation, and replacement', async () => {
  const {failures, page} = await openApplication();

  assert.equal(
      await page.evaluate(
          () => Module.ccall(
              'ztracing_init', 'number', ['string'], ['#canvas'])),
      0);
  await page.evaluate(() => {
    Module._ztracing_start();
    Module._ztracing_start();
  });

  await page.evaluate(async () => {
    const encoder = new TextEncoder();
    const json =
        '[{"name":"first","ph":"X","pid":1,"tid":1,"ts":1,"dur":2}]';
    const bytes = encoder.encode(json);
    const split = Math.floor(bytes.length / 2);
    const stream = new ReadableStream({
      start(controller) {
        controller.enqueue(bytes.slice(0, split));
        controller.enqueue(bytes.slice(split));
        controller.close();
      },
    });
    await Module.ztracing_load_from_stream(
        stream, 'first.json', bytes.length, 'application/json');
  });
  await waitUntilIdle(page);

  const growth = await page.evaluate(() => {
    const before = Module.wasmMemory.buffer.byteLength;
    Module.wasmMemory.grow(1);
    return {
      after: Module.wasmMemory.buffer.byteLength,
      before,
    };
  });
  assert(growth.after > growth.before);

  await page.evaluate(async () => {
    const encoder = new TextEncoder();
    const oldPrefix =
        encoder.encode('[{"name":"old","ph":"X","pid":1,"tid":1,');
    const oldStream = new ReadableStream({
      start(controller) {
        controller.enqueue(oldPrefix);
      },
    });
    const oldLoad = Module.ztracing_load_from_stream(
        oldStream, 'old.json', 100, 'application/json');
    await new Promise(resolve => setTimeout(resolve, 0));

    const currentJson =
        '[{"name":"current","ph":"X","pid":2,"tid":2,"ts":3,"dur":4}]';
    const currentBytes = encoder.encode(currentJson);
    const currentStream = new ReadableStream({
      start(controller) {
        controller.enqueue(currentBytes);
        controller.close();
      },
    });
    await Module.ztracing_load_from_stream(
        currentStream, 'current.json', currentBytes.length, 'application/json');

    await oldLoad;
  });
  await waitUntilIdle(page);

  const loadedViewport = await captureViewport(page);
  await page.evaluate(async () => {
    const broken = new ReadableStream({
      start(controller) {
        controller.error(new Error('injected stream failure'));
      },
    });
    await Module.ztracing_load_from_stream(
        broken, 'broken.json', 1, 'application/json');
  });
  await waitUntilIdle(page);

  const errorViewport = await captureViewport(page);
  assert.notDeepEqual(errorViewport, loadedViewport);
  assert.deepEqual(failures, []);
  await page.close();
});

test('production WASM detects gzip magic and reports parser errors', async () => {
  const {failures, page} = await openApplication();
  const welcome = await captureViewport(page);
  const compressed = gzipSync(Buffer.from(
      '[{"name":"gzip","ph":"X","pid":1,"tid":1,"ts":1,"dur":2}]'));

  await page.evaluate(async bytes => {
    const stream = new ReadableStream({
      start(controller) {
        controller.enqueue(Uint8Array.from(bytes));
        controller.close();
      },
    });
    await Module.ztracing_load_from_stream(
        stream, 'trace.bin', bytes.length, 'application/octet-stream');
  }, Array.from(compressed));
  await waitUntilIdle(page);

  const loaded = await captureViewport(page);
  assert.notDeepEqual(loaded, welcome);

  await page.evaluate(async () => {
    const bytes = new TextEncoder().encode('[{"name"');
    const stream = new ReadableStream({
      start(controller) {
        controller.enqueue(bytes);
        controller.close();
      },
    });
    await Module.ztracing_load_from_stream(
        stream, 'broken.json', bytes.length, 'application/json');
    await new Promise(resolve => {
      function poll() {
        if (!Module._ztracing_is_loading_active()) {
          resolve();
        } else {
          requestAnimationFrame(poll);
        }
      }
      poll();
    });
  });

  const errorUi = await captureViewport(page);
  assert.notDeepEqual(errorUi, loaded);
  assert.deepEqual(failures, []);
  await page.close();
});

test('production WASM restores the persisted theme mode', async () => {
  const dark = await openApplication('dark');
  const darkViewport = await captureViewport(dark.page);
  assert.deepEqual(dark.failures, []);
  await dark.page.close();

  const light = await openApplication('light');
  const lightViewport = await captureViewport(light.page);
  assert.deepEqual(light.failures, []);
  await light.page.close();

  assert.notDeepEqual(lightViewport, darkViewport);
});

test('production file picker loads a selected trace', async () => {
  const {failures, page} = await openApplication();
  const welcome = await captureViewport(page);
  await selectWelcomeFile(page, {
    buffer: Buffer.from(
        '[{"name":"picked","ph":"X","pid":1,"tid":1,"ts":1,"dur":2}]'),
    mimeType: 'application/json',
    name: 'picked.json',
  });
  await waitUntilIdle(page);
  const loaded = await captureViewport(page);

  assert.notDeepEqual(loaded, welcome);
  assert.deepEqual(failures, []);
  await page.close();
});

test('production auto theme follows browser color-scheme changes', async () => {
  const application = await openApplication(undefined, 'dark');
  const dark = await captureViewport(application.page);
  await application.page.emulateMedia({colorScheme: 'light'});
  const light = await captureViewport(application.page);

  assert.notDeepEqual(light, dark);
  assert.deepEqual(application.failures, []);
  await application.page.close();
});
