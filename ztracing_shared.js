class Heap {
  /**
   * @param {WebAssembly.Memory} memory
   */
  constructor(memory) {
    this.memory = memory;
  }

  load_string(ptr, len) {
    return new TextDecoder().decode(
      new Uint8Array(this.memory.buffer.slice(ptr, ptr + len)).slice()
    );
  }

  set_uint32(ptr, val) {
    const view = new DataView(this.memory.buffer);
    view.setUint32(ptr, val, true);
  }

  set_uint64(ptr, val) {
    const view = new DataView(this.memory.buffer);
    view.setBigUint64(ptr, val);
  }

  /**
   *
   * @param {number} dst
   * @param {Uint8Array} src
   */
  memcpy(dst, src) {
    new Uint8Array(this.memory.buffer).set(src, dst);
  }
}

/**
 * @param {WebAssembly.Memory} memory
 */
function make_wasm_imports(memory, app) {
  const heap = new Heap(memory);
  return {
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
      clock_time_get: (ctx, clock_id, precision, time) => {
        if (clock_id != 1) {
          unreachble();
        }
        heap.set_uint64(time, BigInt(Math.round(new Date().getTime() * 1e6)));
        return 0;
      },
    },

    js: {
      log: (level, ptr, len) => {
        const msg = heap.load_string(ptr, len);
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
        app.free_object(ref);
      },

      rendererCreateFontTexture: (width, height, pixels) => {
        return app.renderer.createFontTexture(width, height, pixels);
      },

      rendererBufferData: (
        vtx_buffer_ptr,
        vtx_buffer_size,
        idx_buffer_ptr,
        idx_buffer_size
      ) => {
        app.renderer.bufferData(
          vtx_buffer_ptr,
          vtx_buffer_size,
          idx_buffer_ptr,
          idx_buffer_size
        );
      },

      rendererDraw: (
        clip_rect_min_x,
        clip_rect_min_y,
        clip_rect_max_x,
        clip_rect_max_y,
        texture_ref,
        idx_count,
        idx_offset
      ) => {
        app.renderer.draw(
          clip_rect_min_x,
          clip_rect_min_y,
          clip_rect_max_x,
          clip_rect_max_y,
          texture_ref,
          idx_count,
          idx_offset
        );
      },

      showOpenFilePicker: () => {
        if (!app) {
          unreachble();
        }

        app.show_open_file_picker();
      },

      get_current_timestamp: () => {
        return performance.now();
      },
    },

    env: {
      memory: memory,
    },

    wasi: {
      "thread-spawn": (arg) => {
        return app.spawn_thread(arg);
      },
    },
  };
}

const unreachble = () => {
  throw new Error("unreachable");
};

export { make_wasm_imports, Heap };
