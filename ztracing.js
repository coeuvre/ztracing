import pako from "pako";

/** @type {App} */
var app;

class Memory {
  /**
   * @param {WebAssembly.Memory} memory
   */
  constructor(memory) {
    this.memory = memory;
    this.textDecoder = new TextDecoder();

    this.nextObjectRef = 1n;
    this.objects = {};
  }

  storeObject(object) {
    const ref = this.nextObjectRef;
    this.nextObjectRef = this.nextObjectRef + 1n;
    this.objects[ref] = object;
    return ref;
  }

  loadObject(ref) {
    return this.objects[ref];
  }

  freeObject(ref) {
    this.objects[ref] = undefined;
  }

  loadString(ptr, len) {
    return this.textDecoder.decode(
      new Uint8Array(this.memory.buffer, ptr, len)
    );
  }

  setUint32(ptr, val) {
    const dataView = new DataView(this.memory.buffer);
    dataView.setUint32(ptr, val, true);
  }
}

const unreachble = () => {
  throw new Error("unreachable");
};

const imports = {
  wasi_snapshot_preview1: {
    args_get: unreachble,
    args_sizes_get: unreachble,
    fd_close: unreachble,
    fd_fdstat_get: unreachble,
    fd_fdstat_set_flags: unreachble,
    fd_prestat_get: unreachble,
    fd_prestat_dir_name: unreachble,
    fd_read: unreachble,
    fd_seek: unreachble,
    fd_write: unreachble,
    path_open: unreachble,
    proc_exit: unreachble,
    random_get: unreachble,
    clock_time_get: unreachble,
  },

  js: {
    log: (level, ptr, len) => {
      const msg = app.memory.loadString(ptr, len);
      switch (level) {
        case 0: {
          console.error(msg);
          break;
        }
        case 1: {
          console.warn(msg);
          break;
        }
        case 2: {
          console.info(msg);
          break;
        }
        case 3: {
          console.debug(msg);
          break;
        }
        default: {
          console.log("[level " + level + "] " + msg);
        }
      }
    },

    destory: (ref) => {
      app.memory.freeObject(ref);
    },

    glCreateFontTexture: (w, h, pixels) => {
      const view = new Uint8Array(app.memory.memory.buffer, pixels);
      const gl = app.gl;
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
      return app.memory.storeObject(texture);
    },

    glBufferData: (target, size, data, usage) => {
      const buffer = app.memory.memory.buffer.slice(data, data + size);
      app.gl.bufferData(target, buffer, usage);
    },

    glScissor: (x, y, w, h) => {
      app.gl.scissor(x, y, w, h);
    },

    glBindTexture: (target, texture_ref) => {
      const texture = app.memory.loadObject(texture_ref);
      app.gl.bindTexture(target, texture);
    },

    glDrawElements: (mode, count, type, offset) => {
      app.gl.drawElements(mode, count, type, offset);
    },

    showOpenFilePicker: async () => {
      if (app.loadingFile || !app.instance.exports.shouldLoadFile()) {
        return;
      }

      const pickedFiles = await window.showOpenFilePicker();
      /** @type {FileSystemFileHandle} */
      const firstPickedFile = pickedFiles[0];

      const file = await firstPickedFile.getFile();

      app.instance.exports.onLoadFileStart(file.size);

      const stream = file.stream();
      loadFileFromStream(stream);
    },

    copyChunk: (chunkRef, ptr, len) => {
      /** @type {Uint8Array} */
      const chunk = app.memory.loadObject(chunkRef);
      const dst = new Uint8Array(app.memory.memory.buffer, ptr, len);
      dst.set(chunk);
    },
  },
};

/** @type {URL} url */
function loadFileFromUrl(url) {
  if (app.loadingFile || !app.instance.exports.shouldLoadFile()) {
    return;
  }

  app.instance.exports.onLoadFileStart(0);

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
    this.reader = stream.getReader();
    this.offset = 0;
    this.isDone = false;
  }

  load() {
    this.reader.read().then(({ done, value }) => {
      if (done) {
        if (!this.inflator) {
          this.onDone();
        }
        return;
      }

      if (this.offset == 0) {
        // gzip magic number
        if (value[0] == 0x1f && value[1] == 0x8b) {
          this.inflator = new pako.Inflate();
          this.inflator.onData = this.onChunk;
          this.inflator.onEnd = this.onDone;
        }
      }

      if (this.inflator) {
        this.inflator.push(value);
      } else {
        this.onChunk(value);
      }

      this.offset += value.length;
    });
  }

  onChunk(chunk) {
    const chunkRef = app.memory.storeObject(chunk);
    app.instance.exports.onLoadFileChunk(this.offset, chunkRef, chunk.length);
  }

  onDone() {
    this.isDone = true;
    app.instance.exports.onLoadFileDone();
  }
}

class App {
  /**
   * @param {HTMLCanvasElement} canvas
   * @param {WebAssembly.Instance} instance
   * @param {Memory} memory
   */
  constructor(canvas, instance, memory) {
    this.instance = instance;
    this.memory = memory;
    this.canvas = canvas;
    /** @type {WebGLRenderingContext} */
    this.gl = this.canvas.getContext("webgl2");
  }

  init() {
    this.instance.exports.init(this.canvas.width, this.canvas.height);

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

    this.setViewport(this.canvas.width, this.canvas.height);
  }

  setViewport(width, height) {
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

  onResize() {
    this.setViewport(this.canvas.width, this.canvas.height);
    this.instance.exports.onResize(this.canvas.width, this.canvas.height);
  }

  onMouseMove(x, y) {
    this.instance.exports.onMouseMove(x, y);
  }

  onMouseDown(button) {
    this.instance.exports.onMouseDown(button);
  }

  onMouseUp(button) {
    this.instance.exports.onMouseUp(button);
  }

  onWheel(dx, dy) {
    this.instance.exports.onWheel(dx, dy);
  }

  onFocusChange(focused) {
    this.instance.exports.onFocusChange(focused);
  }

  update(now) {
    if (this.loadingFile) {
      if (!this.loadingFile.isDone && app.instance.exports.shouldLoadFile()) {
        this.loadingFile.load();
      } else {
        this.loadingFile = undefined;
      }
    }

    const gl = this.gl;

    gl.scissor(0, 0, this.canvas.width, this.canvas.height);
    gl.clearColor(0.0, 0.0, 0.0, 1.0);
    gl.clear(gl.COLOR_BUFFER_BIT);

    if (!this.last) {
      this.last = now;
    }
    const dt = (now - this.last) / 1000;
    this.last = now;
    this.instance.exports.update(dt);

    requestAnimationFrame((now) => this.update(now));
  }
}

/**
 * @param {{
 *  ztracingWasmUrl: URL,
 *  canvas: HTMLCanvasElement,
 *  profileUrl: URL | undefined,
 *  onMount: (app: App) => void,
 * }} options
 */
function mount(options) {
  const canvas = options.canvas;
  WebAssembly.instantiateStreaming(
    fetch(options.ztracingWasmUrl),
    imports
  ).then((wasm) => {
    const exports = wasm.instance.exports;

    app = new App(canvas, wasm.instance, new Memory(exports.memory));
    app.init();

    new ResizeObserver(() => app.onResize()).observe(canvas);

    canvas.addEventListener("mousemove", (event) =>
      app.onMouseMove(event.clientX, event.clientY)
    );
    canvas.addEventListener("mousedown", (event) => {
      app.onMouseDown(event.button);
      event.preventDefault();
      return false;
    });
    window.addEventListener("mouseup", (event) => {
      app.onMouseUp(event.button);
      event.preventDefault();
      return false;
    });
    canvas.addEventListener("wheel", (event) => {
      app.onWheel(event.deltaX, event.deltaY);
      event.preventDefault();
      return false;
    });
    canvas.addEventListener("contextmenu", (event) => event.preventDefault());

    window.addEventListener("blur", () => app.onFocusChange(false));
    window.addEventListener("focus", () => app.onFocusChange(true));

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
