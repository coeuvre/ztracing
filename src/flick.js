class Renderer {
  /**@param {WebGLRenderingContext} gl */
  constructor(gl) {
    this.gl = gl;
    this.next_texture_id = 1;
    this.texture_map = {};

    const vs_code = `#version 300 es
precision highp float;
uniform mat4 proj;

layout (location = 0) in vec2 pos;
layout (location = 1) in vec2 uv;
layout (location = 2) in vec4 color;

out vec2 frag_uv;
out vec4 frag_color;

void main() {
  gl_Position = proj * vec4(pos, 0, 1);
  frag_uv = uv;
  frag_color = color;
}
`;

    const vs = gl.createShader(gl.VERTEX_SHADER);
    gl.shaderSource(vs, vs_code);
    gl.compileShader(vs);

    const fs_code = `#version 300 es
precision mediump float;
uniform sampler2D tex;

in vec2 frag_uv;
in vec4 frag_color;

layout (location = 0) out vec4 out_color;

void main(){
  out_color = frag_color * texture(tex, frag_uv);
}`;

    const fs = gl.createShader(gl.FRAGMENT_SHADER);
    gl.shaderSource(fs, fs_code);
    gl.compileShader(fs);

    const program = gl.createProgram();
    gl.attachShader(program, vs);
    gl.attachShader(program, fs);
    gl.linkProgram(program);
    gl.useProgram(program);

    this.uniform_tex = gl.getUniformLocation(program, "tex");
    this.uniform_proj = gl.getUniformLocation(program, "proj");
    const attrib_pos = gl.getAttribLocation(program, "pos");
    const attrib_uv = gl.getAttribLocation(program, "uv");
    const attrib_color = gl.getAttribLocation(program, "color");

    const vertex_buffer = gl.createBuffer();
    const index_buffer = gl.createBuffer();

    gl.bindBuffer(gl.ARRAY_BUFFER, vertex_buffer);
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, index_buffer);

    gl.enableVertexAttribArray(attrib_pos);
    gl.enableVertexAttribArray(attrib_uv);
    gl.enableVertexAttribArray(attrib_color);

    gl.vertexAttribPointer(attrib_pos, 2, gl.FLOAT, false, 20, 0);
    gl.vertexAttribPointer(attrib_uv, 2, gl.FLOAT, false, 20, 8);
    gl.vertexAttribPointer(attrib_color, 4, gl.UNSIGNED_BYTE, true, 20, 16);

    gl.enable(gl.BLEND);
    gl.blendEquation(gl.FUNC_ADD);
    gl.blendFuncSeparate(
      gl.SRC_ALPHA,
      gl.ONE_MINUS_SRC_ALPHA,
      gl.ONE,
      gl.ONE_MINUS_SRC_ALPHA,
    );
    gl.disable(gl.CULL_FACE);
    gl.disable(gl.DEPTH_TEST);
    gl.disable(gl.STENCIL_TEST);
    gl.enable(gl.SCISSOR_TEST);

    gl.activeTexture(gl.TEXTURE0);
  }

  OnResize() {
    const width = this.gl.drawingBufferWidth;
    const height = this.gl.drawingBufferHeight;

    this.gl.viewport(0, 0, width, height);

    const ratio = window.devicePixelRatio;
    const L = 0;
    const R = width / ratio;
    const T = 0;
    const B = height / ratio;
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
    this.gl.uniform1i(this.uniform_tex, 0);
    this.gl.uniformMatrix4fv(
      this.uniform_proj,
      false,
      new Float32Array(ortho_projection),
    );
  }

  /**
   * @param {Uint8Array} pixels
   */
  CreateTexture(width, height, pixels) {
    const gl = this.gl;
    const texture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texImage2D(
      gl.TEXTURE_2D,
      0,
      gl.RGBA,
      width,
      height,
      0,
      gl.RGBA,
      gl.UNSIGNED_BYTE,
      pixels,
    );
    texture.id = this.next_texture_id++;
    this.texture_map[texture.id] = texture;
    return texture.id;
  }

  /**
   * @param {Uint8Array} pixels
   */
  UpdateTexture(texture_id, x, y, width, height, pixels) {
    const gl = this.gl;
    const texture = this.texture_map[texture_id];
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.texSubImage2D(
      gl.TEXTURE_2D,
      0,
      x,
      y,
      width,
      height,
      gl.RGBA,
      gl.UNSIGNED_BYTE,
      pixels,
    );
  }

  /**
   * @param {Uint8Array} vtx_buffer
   * @param {Uint8Array} idx_buffer
   */
  SetBufferData(vtx_buffer, idx_buffer) {
    const gl = this.gl;

    gl.bufferData(gl.ARRAY_BUFFER, vtx_buffer, gl.STREAM_DRAW);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, idx_buffer, gl.STREAM_DRAW);
  }

  Draw(left, top, right, bottom, texture_id, idx_count, idx_offset) {
    const gl = this.gl;
    const ratio = window.devicePixelRatio;

    gl.scissor(
      left * ratio,
      gl.drawingBufferHeight - bottom * ratio,
      (right - left) * ratio,
      (bottom - top) * ratio,
    );

    const texture = this.texture_map[texture_id];
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.drawElements(gl.TRIANGLES, idx_count, gl.UNSIGNED_INT, idx_offset * 4);
  }
}

var LibraryFlick = {
  $Flick__deps: ["$setValue", "$getValue", "$UTF8ToString", "$ccall"],
  $Flick: {
    Renderer,

    GetColor: (color) => {
      const r = getValue(color, "float") * 255;
      const g = getValue(color + 4, "float") * 255;
      const b = getValue(color + 8, "float") * 255;
      const a = getValue(color + 12, "float") * 255;
      return `rgba(${r}, ${g}, ${b}, ${a})`;
    },

    GetString: (s) => {
      const ptr = getValue(s, "*");
      const len = getValue(s + 4, "i32");
      return UTF8ToString(ptr, len);
    },

    button_map: {
      0: 1,
      1: 4,
      2: 2,
    },
  },

  FLJS_Renderer_CreateTexture: (width, height, pixels) => {
    return Flick.renderer.CreateTexture(width, height, HEAPU8.subarray(pixels));
  },

  FLJS_Renderer_UpdateTexture: (texture_id, x, y, width, height, pixels) => {
    return Flick.renderer.UpdateTexture(
      texture_id,
      x,
      y,
      width,
      height,
      HEAPU8.subarray(pixels),
    );
  },

  FLJS_Renderer_SetBufferData: (
    vtx_buffer_ptr,
    vtx_buffer_len,
    idx_buffer_ptr,
    idx_buffer_len,
  ) => {
    const vtx_buffer = HEAPU8.subarray(
      vtx_buffer_ptr,
      vtx_buffer_ptr + vtx_buffer_len,
    );
    const idx_buffer = HEAPU8.subarray(
      idx_buffer_ptr,
      idx_buffer_ptr + idx_buffer_len,
    );
    Flick.renderer.SetBufferData(vtx_buffer, idx_buffer);
  },

  FLJS_Renderer_Draw: (
    left,
    top,
    right,
    bottom,
    texture_id,
    idx_count,
    idx_offset,
  ) => {
    Flick.renderer.Draw(
      left,
      top,
      right,
      bottom,
      texture_id,
      idx_count,
      idx_offset,
    );
  },

  FLJS_Init: () => {
    const canvas = Module.canvas;

    const gl = canvas.getContext("webgl2");
    Flick.renderer = new Flick.Renderer(gl);

    canvas.addEventListener("mousemove", (event) => {
      ccall(
        "FLJS_OnMouseMove",
        "void",
        ["number", "number"],
        [event.clientX, event.clientY],
      );
      event.preventDefault();
    });

    canvas.addEventListener("mousedown", (event) => {
      ccall(
        "FLJS_OnMouseButtonDown",
        "void",
        ["number", "number", "number"],
        [event.clientX, event.clientY, Flick.button_map[event.button]],
      );
      event.preventDefault();
    });

    window.addEventListener("mouseup", (event) => {
      ccall(
        "FLJS_OnMouseButtonUp",
        "void",
        ["number", "number", "number"],
        [event.clientX, event.clientY, Flick.button_map[event.button]],
      );
    });

    canvas.addEventListener("wheel", (event) => {
      ccall(
        "FLJS_OnMouseScroll",
        "void",
        ["number", "number", "number", "number"],
        [event.clientX, event.clientY, event.deltaX, event.deltaY],
      );
      event.preventDefault();
    });
  },

  FLJS_PerformanceNow: () => {
    return performance.now();
  },

  FLJS_CheckCanvasSize: (size, pixels_per_point) => {
    const canvas = Module.canvas;
    const ratio = window.devicePixelRatio;
    if (
      window.innerWidth * ratio != canvas.width ||
      window.innerHeight * ratio != canvas.height
    ) {
      canvas.width = window.innerWidth * ratio;
      canvas.height = window.innerHeight * ratio;
      canvas.style.width = window.innerWidth + "px";
      canvas.style.height = window.innerHeight + "px";

      Flick.renderer.OnResize();
    }

    const gl = Flick.renderer.gl;
    gl.clearColor(0, 0, 0, 1);
    gl.clear(gl.COLOR_BUFFER_BIT);

    setValue(size, window.innerWidth, "float");
    setValue(size + 4, window.innerHeight, "float");
    setValue(pixels_per_point, ratio, "float");
  },
};

autoAddDeps(LibraryFlick, "$Flick");
addToLibrary(LibraryFlick);
