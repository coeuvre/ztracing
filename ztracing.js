import { Heap, make_wasm_imports } from "./ztracing_shared.js";

import ZtracingWorker from "./ztracing_worker.js?worker";

const keyCodeMap = {
  ControlLeft: 527,
  ShiftLeft: 528,
  AltLeft: 529,
  MetaLeft: 530,
  ControlRight: 531,
  ShiftRight: 532,
  AltRight: 533,
  MetaRight: 534,
};

/** @type {App} */
var app;

const unreachble = () => {
  throw new Error("unreachable");
};

class Webgl2Renderer {
  constructor(gl, app) {
    /** @type {WebGLRenderingContext} */
    this.gl = gl;
    /** @type {App} */
    this.app = app;
  }

  init() {
    const gl = this.gl;

    const vertex_shader_code =
      "#version 300 es\n" +
      "precision highp float;\n" +
      "layout (location = 0) in vec2 Position;\n" +
      "layout (location = 1) in vec2 UV;\n" +
      "layout (location = 2) in vec4 Color;\n" +
      "uniform mat4 ProjMtx;\n" +
      "out vec2 Frag_UV;\n" +
      "out vec4 Frag_Color;\n" +
      "void main()\n" +
      "{\n" +
      "    Frag_UV = UV;\n" +
      "    Frag_Color = Color;\n" +
      "    gl_Position = ProjMtx * vec4(Position.xy,0,1);\n" +
      "}\n";

    const vertex_shader = gl.createShader(gl.VERTEX_SHADER);
    gl.shaderSource(vertex_shader, vertex_shader_code);
    gl.compileShader(vertex_shader);

    const fragment_shader_code =
      "#version 300 es\n" +
      "precision mediump float;\n" +
      "uniform sampler2D Texture;\n" +
      "in vec2 Frag_UV;\n" +
      "in vec4 Frag_Color;\n" +
      "layout (location = 0) out vec4 Out_Color;\n" +
      "void main()\n" +
      "{\n" +
      "    Out_Color = Frag_Color * texture(Texture, Frag_UV.st);\n" +
      "}\n";

    const fragment_shader = gl.createShader(gl.FRAGMENT_SHADER);
    gl.shaderSource(fragment_shader, fragment_shader_code);
    gl.compileShader(fragment_shader);

    const program = gl.createProgram();
    gl.attachShader(program, vertex_shader);
    gl.attachShader(program, fragment_shader);
    gl.linkProgram(program);
    gl.useProgram(program);

    this.uniform_texture = gl.getUniformLocation(program, "Texture");
    this.uniform_proj_mtx = gl.getUniformLocation(program, "ProjMtx");
    const attrib_position = gl.getAttribLocation(program, "Position");
    const attrib_uv = gl.getAttribLocation(program, "UV");
    const attrib_color = gl.getAttribLocation(program, "Color");

    const vertex_buffer = gl.createBuffer();
    const index_buffer = gl.createBuffer();

    gl.bindBuffer(gl.ARRAY_BUFFER, vertex_buffer);
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, index_buffer);

    gl.enableVertexAttribArray(attrib_position);
    gl.enableVertexAttribArray(attrib_uv);
    gl.enableVertexAttribArray(attrib_color);

    gl.vertexAttribPointer(attrib_position, 2, gl.FLOAT, false, 20, 0);
    gl.vertexAttribPointer(attrib_uv, 2, gl.FLOAT, false, 20, 8);
    gl.vertexAttribPointer(attrib_color, 4, gl.UNSIGNED_BYTE, true, 20, 16);

    gl.enable(gl.BLEND);
    gl.blendEquation(gl.FUNC_ADD);
    gl.blendFuncSeparate(
      gl.SRC_ALPHA,
      gl.ONE_MINUS_SRC_ALPHA,
      gl.ONE,
      gl.ONE_MINUS_SRC_ALPHA
    );
    gl.disable(gl.CULL_FACE);
    gl.disable(gl.DEPTH_TEST);
    gl.disable(gl.STENCIL_TEST);
    gl.enable(gl.SCISSOR_TEST);

    gl.activeTexture(gl.TEXTURE0);

    this.on_resize();
  }

  on_resize() {
    const width = this.gl.drawingBufferWidth;
    const height = this.gl.drawingBufferHeight;

    this.gl.viewport(0, 0, width, height);

    const L = 0;
    const R = width;
    const T = 0;
    const B = height;
    const ortho_projection = [
      2 / (R - L),
      0,
      0.0,
      0.0,
      0.0,
      2.0 / (T - B),
      0.0,
      0.0,
      0.0,
      0.0,
      -1.0,
      0.0,
      (R + L) / (L - R),
      (T + B) / (B - T),
      0.0,
      1.0,
    ];
    this.gl.uniform1i(this.uniform_texture, 0);
    this.gl.uniformMatrix4fv(
      this.uniform_proj_mtx,
      false,
      new Float32Array(ortho_projection)
    );
  }

  createFontTexture(w, h, pixels) {
    const view = new Uint8Array(this.app.heap.memory.buffer, pixels);
    const gl = this.gl;
    const texture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texImage2D(
      gl.TEXTURE_2D,
      0,
      gl.RGBA,
      w,
      h,
      0,
      gl.RGBA,
      gl.UNSIGNED_BYTE,
      view
    );
    return app.store_object(texture);
  }

  bufferData(vtx_buffer_ptr, vtx_buffer_size, idx_buffer_ptr, idx_buffer_size) {
    const gl = this.gl;

    const vtx_buffer = this.app.heap.memory.buffer.slice(
      vtx_buffer_ptr,
      vtx_buffer_ptr + vtx_buffer_size
    );
    gl.bufferData(gl.ARRAY_BUFFER, vtx_buffer, gl.STREAM_DRAW);

    const idx_buffer = this.app.heap.memory.buffer.slice(
      idx_buffer_ptr,
      idx_buffer_ptr + idx_buffer_size
    );
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, idx_buffer, gl.STREAM_DRAW);
  }

  draw(
    clip_rect_min_x,
    clip_rect_min_y,
    clip_rect_max_x,
    clip_rect_max_y,
    texture_ref,
    idx_count,
    idx_offset
  ) {
    const gl = this.gl;
    gl.scissor(
      clip_rect_min_x,
      this.gl.drawingBufferHeight - clip_rect_max_y,
      clip_rect_max_x - clip_rect_min_x,
      clip_rect_max_y - clip_rect_min_y
    );

    const texture = this.app.load_object(texture_ref);
    gl.bindTexture(gl.TEXTURE_2D, texture);

    gl.drawElements(gl.TRIANGLES, idx_count, gl.UNSIGNED_INT, idx_offset * 4);
  }
}

/** @type {URL} url */
function loadFileFromUrl(url) {
  if (app.loadingFile || !app.instance.exports.shouldLoadFile(app.app_ptr)) {
    return;
  }

  app.instance.exports.onLoadFileStart(app.app_ptr, 0, app.store_string(url));

  fetch(url)
    .then((response) => response.body)
    .then((stream) => {
      loadFileFromStream(stream);
    });
}

/** @param {ReadableStream} stream */
function loadFileFromStream(stream) {
  if (app.loadingFile) {
    return;
  }

  app.loadingFile = new LoadingFile(stream);
}

class LoadingFile {
  /**
   * @param {ReadableStream} stream
   */
  constructor(stream) {
    const self = this;
    this.stream = stream.pipeThrough(
      new TransformStream({
        transform(chunk, controller) {
          self.underlying_offset += chunk.length;
          controller.enqueue(chunk);
        },
      })
    );
    this.reader = this.stream.getReader();
    this.reading = false;
    this.offset = 0;
    this.underlying_offset = 0;
    this.isDone = false;
  }

  load() {
    if (this.reading || this.isDone) {
      return;
    }

    this.reading = true;
    this.reader.read().then(({ done, value }) => this.onChunk(done, value));
  }

  /**
   *
   * @param {ReadableStreamReadResult} result
   */
  onChunk(done, chunk) {
    if (done) {
      this.isDone = true;
      app.instance.exports.onLoadFileDone(app.app_ptr);
      return;
    }

    if (this.offset == 0) {
      // gzip magic number
      if (chunk[0] == 0x1f && chunk[1] == 0x8b) {
        const ds = new DecompressionStream("gzip");
        {
          const writable = ds.writable.getWriter();
          writable.write(chunk);
          writable.releaseLock();
        }

        this.reader.releaseLock();
        this.reader = this.stream.pipeThrough(ds).getReader();
        this.reader.read().then(({ done, value }) => this.onChunk(done, value));
        return;
      }
    }

    const chunkRef = app.store_object(chunk);
    app.instance.exports.onLoadFileChunk(
      app.app_ptr,
      this.offset,
      chunkRef,
      chunk.length
    );
    this.offset = this.underlying_offset;
    this.reader.read().then(({ done, value }) => this.onChunk(done, value));
  }
}

class App {
  /**
   * @param {HTMLCanvasElement} canvas
   * @param {Heap} heap
   */
  constructor(canvas, heap) {
    /** @type {WebAssembly.Instance} */
    this.instance = undefined;
    this.heap = heap;
    this.canvas = canvas;
    this.renderer = new Webgl2Renderer(
      this.canvas.getContext("webgl2", {
        powerPreference: "high-performance",
        alpha: false,
        antialias: false,
        depth: false,
        stencil: false,
      }),
      this
    );
    /** @type {LoadingFile | undefined} */
    this.loadingFile = undefined;

    this.next_object_ref = 1n;
    this.objects = {};
  }

  set_canvas_size(width, height) {
    this.canvas_display_width = width;
    this.canvas_display_height = height;
    this.canvas.style.width = width + "px";
    this.canvas.style.height = height + "px";
    this.canvas.width = width * devicePixelRatio;
    this.canvas.height = height * devicePixelRatio;
  }

  init(instance, width, height, font_data, font_size) {
    this.instance = instance;
    this.set_canvas_size(width, height);

    this.app_ptr = this.instance.exports.init(
      this.canvas.width,
      this.canvas.height,
      devicePixelRatio,
      font_data ? this.store_object(new Uint8Array(font_data)) : 0n,
      font_data ? font_data.byteLength : 0,
      font_size
    );
    this.renderer.init();
  }

  resize(width, height) {
    this.set_canvas_size(width, height);

    this.renderer.on_resize();
    this.instance.exports.on_resize(
      this.app_ptr,
      this.canvas.width,
      this.canvas.height
    );
  }

  onMousePos(x, y) {
    this.instance.exports.onMousePos(
      this.app_ptr,
      (x / this.canvas_display_width) * this.canvas.width,
      (y / this.canvas_display_height) * this.canvas.height
    );
  }

  onMouseButton(button, down) {
    return this.instance.exports.onMouseButton(this.app_ptr, button, down);
  }

  onMouseWheel(dx, dy) {
    this.instance.exports.onMouseWheel(
      this.app_ptr,
      (dx / this.canvas_display_width) * this.canvas.width,
      (dy / this.canvas_display_height) * this.canvas.height
    );
  }

  onKey(key, down) {
    return this.instance.exports.onKey(this.app_ptr, key, down);
  }

  onFocus(focused) {
    this.instance.exports.onFocus(this.app_ptr, focused);
  }

  update(now) {
    if (this.loadingFile) {
      if (
        !this.loadingFile.isDone &&
        app.instance.exports.shouldLoadFile(this.app_ptr)
      ) {
        this.loadingFile.load();
      } else {
        this.loadingFile = undefined;
      }
    }

    if (!this.last) {
      this.last = now;
    }
    const dt = (now - this.last) / 1000;
    this.last = now;
    if (dt > 0) {
      this.instance.exports.update(this.app_ptr, dt);
    }

    requestAnimationFrame((now) => this.update(now));
  }

  store_object(object) {
    const ref = this.next_object_ref;
    this.next_object_ref = this.next_object_ref + 1n;
    this.objects[ref] = object;
    return ref;
  }

  load_object(ref) {
    return this.objects[ref];
  }

  free_object(ref) {
    this.objects[ref] = undefined;
  }

  store_string(str) {
    return this.store_object(new TextEncoder().encode(str));
  }
}

var next_thread_id = 1;

/**
 * @param {{
 *  ztracing_wasm_url: URL,
 *  font: { url: URL, size: number } | undefined,
 *  canvas: HTMLCanvasElement,
 *  width: number,
 *  height: number,
 *  profileUrl: URL | undefined,
 *  onMount: (app: App) => void,
 * }} options
 */
function mount(options) {
  const canvas = options.canvas;

  const memory = new WebAssembly.Memory({
    initial: 260,
    maximum: 65536,
    shared: true,
  });
  const heap = new Heap(memory);

  app = new App(canvas, heap);
  const imports = make_wasm_imports(memory, app);

  app.spawn_thread = function (arg) {
    const tid = next_thread_id++;
    const worker = new ZtracingWorker();
    worker.postMessage({
      memory,
      ztracing_wasm_url: options.ztracing_wasm_url,
      tid,
      arg,
    });
    worker.onmessage = (e) => {
      worker.terminate();
    };
    return tid;
  };

  const fetch_font_promise = options.font
    ? fetch(options.font.url).then((res) => res.arrayBuffer())
    : Promise.resolve();

  const init_wasm_promise = WebAssembly.instantiateStreaming(
    fetch(options.ztracing_wasm_url),
    imports
  );

  Promise.all([fetch_font_promise, init_wasm_promise]).then(async (result) => {
    const font_data = result[0];
    const wasm = result[1];

    app.init(
      wasm.instance,
      options.width,
      options.height,
      font_data,
      options.font ? options.font.size : 0
    );

    canvas.addEventListener("mousemove", (event) =>
      app.onMousePos(event.clientX, event.clientY)
    );
    canvas.addEventListener("mousedown", (event) => {
      if (app.onMouseButton(event.button, true)) {
        event.preventDefault();
      }
    });
    window.addEventListener("mouseup", (event) => {
      app.onMouseButton(event.button, false);
    });
    canvas.addEventListener("wheel", (event) => {
      app.onMouseWheel(event.deltaX, event.deltaY);
      event.preventDefault();
    });
    canvas.addEventListener("contextmenu", (event) => event.preventDefault());
    window.addEventListener("keydown", (event) => {
      const key = keyCodeMap[event.code];
      if (key) {
        app.onKey(key, true);
        event.preventDefault();
      }
    });
    window.addEventListener("keyup", (event) => {
      const key = keyCodeMap[event.code];
      if (key) {
        app.onKey(key, false);
      }
    });

    window.addEventListener("blur", () => app.onFocus(false));
    window.addEventListener("focus", () => app.onFocus(true));

    canvas.addEventListener("drop", (event) => {
      event.preventDefault();

      if (
        app.loadingFile ||
        !app.instance.exports.shouldLoadFile(app.app_ptr)
      ) {
        return;
      }

      if (
        event.dataTransfer &&
        event.dataTransfer.files &&
        event.dataTransfer.files.length > 0
      ) {
        const file = event.dataTransfer.files[0];
        app.instance.exports.onLoadFileStart(
          app.app_ptr,
          file.size,
          app.store_string(file.name)
        );

        const stream = file.stream();
        loadFileFromStream(stream);
      }
    });

    canvas.addEventListener("dragover", (event) => {
      event.preventDefault();
    });

    requestAnimationFrame((now) => app.update(now));

    options.onMount(app);

    if (options.profileUrl) {
      loadFileFromUrl(options.profileUrl);
    }
  });
}

export default {
  mount: mount,
};
