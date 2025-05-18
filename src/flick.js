var LibraryFlick = {
  $Flick__deps: ["$setValue", "$getValue", "$UTF8ToString", "$ccall"],
  $Flick: {
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

  FLJS_Init: () => {
    const canvas = Module.canvas;

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

  FLJS_ResizeCanvas: (w, h, size) => {
    const canvas = Module.canvas;
    const ratio = window.devicePixelRatio;
    if (window.innerWidth * ratio != w || window.innerHeight * ratio != h) {
      canvas.width = window.innerWidth * ratio;
      canvas.height = window.innerHeight * ratio;
      canvas.style.width = window.innerWidth + "px";
      canvas.style.height = window.innerHeight + "px";

      const ctx = canvas.getContext("2d");
      // Scale by the ratio so that the input to the canvas APIs remains in point coordinate, no pixel.
      ctx.scale(ratio, ratio);
    }

    setValue(size, window.innerWidth, "float");
    setValue(size + 4, window.innerHeight, "float");
  },

  FLJS_Canvas_Save: () => {
    const canvas = Module.canvas;
    const ctx = canvas.getContext("2d");
    ctx.save();
  },

  FLJS_Canvas_Restore: () => {
    const canvas = Module.canvas;
    const ctx = canvas.getContext("2d");
    ctx.restore();
  },

  FLJS_Canvas_ClipRect: (x, y, width, height) => {
    const canvas = Module.canvas;
    const ctx = canvas.getContext("2d");
    ctx.beginPath();
    ctx.rect(x, y, width, height);
    ctx.clip();
  },

  FLJS_Canvas_FillRect: (x, y, width, height, color) => {
    const canvas = Module.canvas;
    const ctx = canvas.getContext("2d");
    ctx.fillStyle = Flick.GetColor(color);
    ctx.fillRect(x, y, width, height);
  },

  FLJS_Canvas_StrokeRect: (x, y, width, height, color, line_width) => {
    const canvas = Module.canvas;
    const ctx = canvas.getContext("2d");
    ctx.strokeStyle = Flick.GetColor(color);
    ctx.lineWidth = line_width;
    ctx.strokeRect(x, y, width, height);
  },

  FLJS_Canvas_MeasureText: (text, font_size, text_metrics) => {
    const canvas = Module.canvas;
    const ctx = canvas.getContext("2d");

    ctx.font = font_size + "px monospace";
    const metrics = ctx.measureText(Flick.GetString(text));

    setValue(text_metrics, metrics.width, "float");
    setValue(text_metrics + 4, metrics.fontBoundingBoxAscent, "float");
    setValue(text_metrics + 8, metrics.fontBoundingBoxDescent, "float");
  },

  FLJS_Canvas_FillText: (text, x, y, font_size, color) => {
    const canvas = Module.canvas;
    const ctx = canvas.getContext("2d");

    ctx.font = font_size + "px monospace";
    ctx.fillStyle = Flick.GetColor(color);
    ctx.fillText(Flick.GetString(text), x, y);
  },
};

autoAddDeps(LibraryFlick, "$Flick");
addToLibrary(LibraryFlick);
