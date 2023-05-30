/** @type App */
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
  },

  js: {
    log: (level, ptr, len) => {
      const msg = app.memory.loadString(ptr, len);
      switch (level) {
        case 0: {
          throw new Error(msg);
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
  },
};

class App {
  /**
   * @param {WebAssembly.Instance} instance
   * @param {Memory} memory
   */
  constructor(instance, memory) {
    this.instance = instance;
    this.memory = memory;

    /** @type HTMLCanvasElement */
    this.canvas = document.getElementById("canvas");
    /** @type WebGLRenderingContext */
    this.gl = this.canvas.getContext("webgl");
  }

  init() {
    this.canvas.width = window.innerWidth;
    this.canvas.height = window.innerHeight;
    this.instance.exports.init(this.canvas.width, this.canvas.height);

    const gl = this.gl;

    const vertex_buffer = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, vertex_buffer);

    const index_buffer = gl.createBuffer();
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, index_buffer);

    const vertex_shader_code =
      "uniform mat4 ProjMtx;\n" +
      "attribute vec2 Position;\n" +
      "attribute vec2 UV;\n" +
      "attribute vec4 Color;\n" +
      "varying vec2 Frag_UV;\n" +
      "varying vec4 Frag_Color;\n" +
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
      "precision mediump float;\n" +
      "uniform sampler2D Texture;\n" +
      "varying vec2 Frag_UV;\n" +
      "varying vec4 Frag_Color;\n" +
      "void main()\n" +
      "{\n" +
      "    gl_FragColor = Frag_Color * texture2D(Texture, Frag_UV.st);\n" +
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

  onresize() {
    if (
      this.canvas.width != window.innerWidth ||
      this.canvas.height != window.innerHeight
    ) {
      this.canvas.width = window.innerWidth;
      this.canvas.height = window.innerHeight;

      this.setViewport(this.canvas.width, this.canvas.height);

      this.instance.exports.onResize(this.canvas.width, this.canvas.height);
    }
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

  update() {
    const gl = this.gl;

    gl.scissor(0, 0, this.canvas.width, this.canvas.height);
    gl.clearColor(0.0, 0.0, 0.0, 1.0);
    gl.clear(gl.COLOR_BUFFER_BIT);

    this.instance.exports.update();

    requestAnimationFrame(() => this.update());
  }
}

WebAssembly.instantiateStreaming(
  fetch("zig-out/lib/ztracing.wasm"),
  imports
).then((wasm) => {
  const exports = wasm.instance.exports;

  app = new App(wasm.instance, new Memory(exports.memory));
  app.init();

  window.onresize = () => app.onresize();
  addEventListener("mousemove", (event) =>
    app.onMouseMove(event.clientX, event.clientY)
  );
  addEventListener("mousedown", (event) => {
    app.onMouseDown(event.button);
    event.preventDefault();
    return false;
  });
  addEventListener("mouseup", (event) => {
    app.onMouseUp(event.button);
    event.preventDefault();
    return false;
  });
  addEventListener("wheel", (event) => {
    app.onWheel(event.deltaX, event.deltaY);
    return false;
  });
  addEventListener("contextmenu", (event) => event.preventDefault());

  requestAnimationFrame(() => app.update());
});
