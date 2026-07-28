#![cfg(target_os = "emscripten")]

use base::allocation::CountingAllocator;
use base::logging::{Level, LogWriter, set_writer};
use base::{debug, error};

#[global_allocator]
static ALLOCATOR: CountingAllocator = CountingAllocator;

use std::cell::{Cell, RefCell};
use std::ffi::{CStr, CString, c_char};
use std::fmt;
use ztracing::runtime::Runtime;

mod ffi_buffer;

fn main() {}

use ztracing::imgui::Context;

struct JsWriter;

static JS_WRITER: JsWriter = JsWriter;

impl LogWriter for JsWriter {
    fn write(&self, level: Level, args: fmt::Arguments<'_>) {
        let message = ffi_string(args.to_string());
        unsafe { ztracing_js_log(level as i32, message.as_ptr()) };
    }
}

fn ffi_string(message: String) -> CString {
    CString::new(message).unwrap_or_else(|error| {
        let mut message = error.into_vec();
        message.retain(|byte| *byte != 0);
        CString::new(message).expect("NUL bytes were removed from FFI string")
    })
}

thread_local! {
    static RUNTIME: RefCell<Option<Runtime>> = const { RefCell::new(None) };
    static ANIMATION_STARTED: Cell<bool> = const { Cell::new(false) };
}

#[unsafe(no_mangle)]
pub extern "C" fn ztracing_init(canvas: *const c_char) -> i32 {
    let _ = set_writer(&JS_WRITER);
    if RUNTIME.with_borrow(Option::is_some) {
        return 0;
    }
    if canvas.is_null() {
        error!("failed to initialize: canvas selector is null");
        return 1;
    }
    let selector = unsafe { CStr::from_ptr(canvas) };
    let Ok(selector) = selector.to_str() else {
        error!("failed to initialize: canvas selector is not UTF-8");
        return 1;
    };
    let webgl_context = unsafe { ztracing_create_webgl_context(canvas) };
    if webgl_context == 0 {
        error!("failed to create WebGL context for selector '{selector}'");
        return 1;
    }
    let imgui = Context::create();
    if unsafe { imgui_impl_webgl_init() } == 0 {
        unsafe { ztracing_destroy_webgl_context(webgl_context) };
        error!("failed to initialize ImGui WebGL backend");
        return 2;
    }
    if unsafe { imgui_impl_wasm_init(canvas) } == 0 {
        unsafe {
            imgui_impl_webgl_shutdown();
            ztracing_destroy_webgl_context(webgl_context);
        }
        error!("failed to initialize ImGui WASM backend");
        return 2;
    }
    let runtime = Runtime::new(imgui, ztracing::platform::is_dark_mode());
    RUNTIME.with_borrow_mut(|current| *current = Some(runtime));
    unsafe { imgui_impl_wasm_request_update() };
    debug!("ztracing initialized successfully.");
    0
}

#[unsafe(no_mangle)]
pub extern "C" fn ztracing_start() {
    if RUNTIME.with_borrow(Option::is_none) {
        return;
    }
    ANIMATION_STARTED.with(|started| {
        if !started.replace(true) {
            unsafe { ztracing_start_animation_loop() }
        }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn ztracing_update() {
    RUNTIME.with_borrow_mut(|runtime| {
        let Some(runtime) = runtime.as_mut() else {
            return;
        };
        let _ = runtime.update(
            |app| {
                if app.loading.request_update {
                    unsafe { imgui_impl_wasm_request_update() }
                }
                !app.power_save_mode || unsafe { imgui_impl_wasm_need_update() } != 0
            },
            || unsafe {
                imgui_impl_webgl_new_frame();
                imgui_impl_wasm_new_frame()
            },
        );
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn ztracing_set_font_data(data: *const u8, size: i32) {
    if data.is_null() || size < 0 {
        return;
    }
    RUNTIME.with_borrow_mut(|runtime| {
        let Some(runtime) = runtime.as_mut() else {
            return;
        };
        let dpi = ztracing::platform::dpi_scale();
        let font = unsafe { std::slice::from_raw_parts(data, size as usize) };
        runtime.set_font(font, dpi);
        unsafe { imgui_impl_wasm_request_update() }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn ztracing_malloc(size: i32) -> *mut u8 {
    if size <= 0 {
        return std::ptr::null_mut();
    }
    let layout = std::alloc::Layout::array::<u8>(size as usize).expect("allocation size overflow");
    // SAFETY: the matching exported free function or `Vec::from_raw_parts`
    // reclaims this allocation with the same size and alignment.
    unsafe { std::alloc::alloc(layout) }
}

#[unsafe(no_mangle)]
pub extern "C" fn ztracing_free(pointer: *mut u8, size: i32) {
    if pointer.is_null() && size == 0 {
        return;
    }
    if pointer.is_null() || size <= 0 {
        error!("invalid buffer passed to ztracing_free");
        return;
    }
    let layout = std::alloc::Layout::array::<u8>(size as usize).expect("allocation size overflow");
    // SAFETY: JavaScript returns pointers created by `ztracing_malloc` exactly once
    // and supplies the original allocation size.
    unsafe { std::alloc::dealloc(pointer, layout) }
}

#[unsafe(no_mangle)]
pub extern "C" fn ztracing_begin_session(id: i32, filename: *const c_char, total: f64) -> i32 {
    if filename.is_null() {
        return 0;
    }
    let name = unsafe { CStr::from_ptr(filename) }
        .to_string_lossy()
        .into_owned();
    RUNTIME.with_borrow_mut(|runtime| {
        let Some(runtime) = runtime.as_mut() else {
            return 0;
        };
        let accepted = runtime.app.begin_session(id, name, total.max(0.0) as usize);
        unsafe { imgui_impl_wasm_request_update() }
        i32::from(accepted)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn ztracing_set_error(id: i32, message: *const c_char) {
    if message.is_null() {
        return;
    }
    let message = unsafe { CStr::from_ptr(message) }
        .to_string_lossy()
        .into_owned();
    RUNTIME.with_borrow_mut(|runtime| {
        if let Some(runtime) = runtime.as_mut() {
            if id == 0 {
                runtime.app.report_error(message);
            } else {
                runtime.app.fail_session(id, message);
            }
            unsafe { imgui_impl_wasm_request_update() }
        }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn ztracing_handle_file_chunk(
    id: i32,
    data: *mut u8,
    size: i32,
    consumed: f64,
    eof: bool,
) -> i32 {
    let bytes = match unsafe { ffi_buffer::take_owned(data, size, eof) } {
        Ok(bytes) => bytes,
        Err(error) => {
            error!(
                "invalid trace chunk: {error:?}, pointer_null={}, size={size}, eof={eof}",
                data.is_null(),
            );
            RUNTIME.with_borrow_mut(|runtime| {
                if let Some(runtime) = runtime.as_mut() {
                    runtime
                        .app
                        .fail_session(id, format!("Invalid trace chunk: {error:?}"));
                    unsafe { imgui_impl_wasm_request_update() }
                }
            });
            return 0;
        }
    };
    RUNTIME.with_borrow_mut(|runtime| {
        runtime.as_mut().map_or(0, |runtime| {
            runtime
                .app
                .handle_file_chunk(id, bytes, consumed.max(0.0) as usize, eof)
                .min(i32::MAX as usize) as i32
        })
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn ztracing_get_buffered_bytes() -> i32 {
    RUNTIME.with_borrow(|runtime| {
        runtime.as_ref().map_or(0, |runtime| {
            runtime.app.buffered_bytes().min(i32::MAX as usize) as i32
        })
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn ztracing_is_loading_active() -> bool {
    RUNTIME.with_borrow(|runtime| {
        runtime
            .as_ref()
            .is_some_and(|runtime| runtime.app.loading.active)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn ztracing_on_theme_changed(dark: bool) {
    RUNTIME.with_borrow_mut(|runtime| {
        if let Some(runtime) = runtime.as_mut() {
            runtime.on_theme_changed(dark);
            unsafe { imgui_impl_wasm_request_update() }
        }
    })
}

unsafe extern "C" {
    fn ztracing_js_log(level: i32, message: *const c_char);
    fn ztracing_create_webgl_context(selector: *const c_char) -> i32;
    fn ztracing_destroy_webgl_context(context: i32);
    fn ztracing_start_animation_loop();
    fn imgui_impl_wasm_init(selector: *const c_char) -> i32;
    fn imgui_impl_wasm_new_frame();
    fn imgui_impl_wasm_request_update();
    fn imgui_impl_wasm_need_update() -> i32;
    fn imgui_impl_webgl_init() -> i32;
    fn imgui_impl_webgl_shutdown();
    fn imgui_impl_webgl_new_frame();
}
