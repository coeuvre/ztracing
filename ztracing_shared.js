const WorkerEvent = {
    load: 0,
    init: 1,
    run_task: 2,
};

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
    const dataView = new DataView(this.memory.buffer);
    dataView.setUint32(ptr, val, true);
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
      clock_time_get: unreachble,
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

      showOpenFilePicker: async () => {
        if (app.loadingFile || !app.instance.exports.shouldLoadFile()) {
          return;
        }

        const pickedFiles = await window.showOpenFilePicker();
        /** @type {FileSystemFileHandle} */
        const firstPickedFile = pickedFiles[0];

        const file = await firstPickedFile.getFile();

        app.instance.exports.onLoadFileStart(
          file.size,
          app.store_string(file.name)
        );

        const stream = file.stream();
        loadFileFromStream(stream);
      },

      copy_uint8_array: (buf_ref, ptr, len) => {
        /** @type {Uint8Array} */
        const chunk = app.load_object(buf_ref);
        const dst = new Uint8Array(heap.memory.buffer, ptr, len);
        dst.set(chunk);
      },

      get_uint8_array_len: (buf_ref) => {
        /** @type {Uint8Array} */
        const buf = app.load_object(buf_ref);
        return buf.length;
      },

      get_current_timestamp: () => {
        return performance.now();
      },
    },

    env: {
      memory: memory,
    },
  };
}

const unreachble = () => {
  throw new Error("unreachable");
};

export { make_wasm_imports, Heap, WorkerEvent };
