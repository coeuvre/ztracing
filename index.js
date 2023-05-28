/** @type App */
var app;

class Memory {
  constructor(memory) {
    this.memory = memory;
    this.textDecoder = new TextDecoder();

    this.nextObjectRef = 1;
    this.objects = {};
  }

  storeObject(object) {
    const ref = this.nextObjectRef++;
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
}

const imports = {
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

    clearRect: (x, y, w, h) => {
      app.ctx.clearRect(x, y, w, h);
    },

    setFillStyleColor: (color_ptr, color_len) => {
      const color = app.memory.loadString(color_ptr, color_len);
      app.ctx.fillStyle = color;
    },

    createLinearGradient: (x0, y0, x1, y1) => {
      const linearGradient = app.ctx.createLinearGradient(x0, y0, x1, y1);
      return app.memory.storeObject(linearGradient);
    },

    addGradientColorStop: (ref, offset, color_ptr, color_len) => {
      const gradient = app.memory.loadObject(ref);
      const color = app.memory.loadString(color_ptr, color_len);
      gradient.addColorStop(offset, color);
    },

    setFillStyleGradient: (ref) => {
      app.ctx.fillStyle = app.memory.loadObject(ref);
    },

    fillRect: (x, y, w, h) => {
      app.ctx.fillRect(x, y, w, h);
    },

    setLineWidth: (width) => {
      app.ctx.lineWidth = width;
    },

    setStrokeStyleColor: (color_ptr, color_len) => {
      const color = app.memory.loadString(color_ptr, color_len);
      app.ctx.strokeStyle = color;
    },

    strokeRect: (x, y, w, h) => {
      app.ctx.strokeRect(x, y, w, h);
    },

    beginPath: () => {
      app.ctx.beginPath();
    },

    roundedRect: (x, y, w, h, r) => {
      app.ctx.roundRect(x, y, w, h, r);
    },

    stroke: () => {
      app.ctx.stroke()
    }
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
    /** @type CanvasRenderingContext2D */
    this.ctx = this.canvas.getContext("2d");
  }

  init() {
    this.canvas.width = window.innerWidth;
    this.canvas.height = window.innerHeight;
    this.instance.exports.init(this.canvas.width, this.canvas.height);
  }

  onresize() {
    if (
      this.canvas.width != window.innerWidth ||
      this.canvas.height != window.innerHeight
    ) {
      this.canvas.width = window.innerWidth;
      this.canvas.height = window.innerHeight;
      this.instance.exports.onResize(this.canvas.width, this.canvas.height);
    }
  }

  update() {
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
  requestAnimationFrame(() => app.update());
});
