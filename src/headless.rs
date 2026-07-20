use crate::headless_gl::Context as GlContext;
use crate::imgui::{Context as ImguiContext, MOD_CTRL};
use crate::runtime::Runtime;
use base::logging::{STDERR_WRITER, set_writer};
use base::{error, info};
use std::ffi::c_void;

const GL_RGBA: u32 = 0x1908;
const GL_UNSIGNED_BYTE: u32 = 0x1401;

pub struct HeadlessApp {
    pub runtime: Runtime,
    gl: GlContext,
}
impl HeadlessApp {
    pub fn create(width: i32, height: i32) -> Result<Self, String> {
        let _ = set_writer(&STDERR_WRITER);
        let gl = GlContext::create(width, height)?;
        let mut imgui = ImguiContext::create();
        imgui.set_display_size(width as f32, height as f32);
        imgui.set_delta_time(1.0 / 60.0);
        // SAFETY: the EGL/GLES context is current and ImGui has a live context.
        if unsafe { imgui_impl_webgl_init() } == 0 {
            let message = "failed to initialize ImGui GLES renderer".to_owned();
            error!("{message}");
            return Err(message);
        }
        let runtime = Runtime::new(imgui, true);
        info!("Headless ztracing initialized successfully.");
        Ok(Self { runtime, gl })
    }
    pub fn set_font(&mut self, font: &[u8]) {
        self.runtime.set_font(font, 1.0);
    }
    pub fn update(&mut self) {
        let width = self.gl.width() as f32;
        let height = self.gl.height() as f32;
        self.runtime.imgui.set_display_size(width, height);
        self.runtime.imgui.set_delta_time(1.0 / 60.0);
        self.runtime.update(|_| true, || {});
    }
    pub fn click(&mut self, x: f32, y: f32) {
        self.runtime.imgui.add_mouse_position(x, y);
        self.update();
        self.runtime.imgui.add_mouse_button(0, true);
        self.update();
        self.runtime.imgui.add_mouse_button(0, false);
        self.update()
    }
    pub fn double_click(&mut self, x: f32, y: f32) {
        // Settle hover once, then two click pulses.
        self.runtime.imgui.add_mouse_position(x, y);
        self.update();
        self.runtime.imgui.add_mouse_button(0, true);
        self.update();
        self.runtime.imgui.add_mouse_button(0, false);
        self.update();
        self.runtime.imgui.add_mouse_button(0, true);
        self.update();
        self.runtime.imgui.add_mouse_button(0, false);
        self.update();
    }
    pub fn zoom(&mut self, x: f32, y: f32, wheel: f32) {
        self.runtime.imgui.add_mouse_position(x, y);
        self.runtime.imgui.add_key(MOD_CTRL, true);
        self.runtime.imgui.add_mouse_wheel(0.0, wheel);
        self.update();
        self.runtime.imgui.add_mouse_wheel(0.0, 0.0);
        self.update();
        self.runtime.imgui.add_key(MOD_CTRL, false);
        self.update()
    }
    pub fn drag(&mut self, start: (f32, f32), end: (f32, f32)) {
        self.runtime.imgui.add_mouse_position(start.0, start.1);
        self.update();
        self.runtime.imgui.add_mouse_button(0, true);
        self.update();
        self.runtime.imgui.add_mouse_position(end.0, end.1);
        self.update();
        self.update();
        self.runtime.imgui.add_mouse_button(0, false);
        self.update()
    }
    pub fn key_shortcut(&mut self, key: i32, modifier: i32) {
        self.runtime.imgui.add_key(modifier, true);
        self.runtime.imgui.add_key(key, true);
        self.update();
        self.runtime.imgui.add_key(key, false);
        self.runtime.imgui.add_key(modifier, false);
        self.update();
    }
    pub fn drag_with_modifier(&mut self, start: (f32, f32), end: (f32, f32), modifier: i32) {
        if modifier != 0 {
            self.runtime.imgui.add_key(modifier, true);
        }
        self.runtime.imgui.add_mouse_position(start.0, start.1);
        self.update();
        self.runtime.imgui.add_mouse_button(0, true);
        self.update();
        self.runtime.imgui.add_mouse_position(end.0, end.1);
        self.update();
        self.update();
        self.runtime.imgui.add_mouse_button(0, false);
        if modifier != 0 {
            self.runtime.imgui.add_key(modifier, false);
        }
        self.update();
    }
    pub fn text_input(&mut self, text: &str) {
        self.runtime.imgui.add_text(text);
        self.update()
    }
    pub fn dimensions(&self) -> (usize, usize) {
        (self.gl.width() as usize, self.gl.height() as usize)
    }
    pub fn read_rgba(&self) -> Vec<u8> {
        let width = self.gl.width();
        let height = self.gl.height();
        let mut pixels = vec![0; width as usize * height as usize * 4]; // SAFETY: the output allocation covers the requested RGBA8 framebuffer dimensions.
        unsafe {
            glReadPixels(
                0,
                0,
                width,
                height,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                pixels.as_mut_ptr().cast(),
            )
        };
        pixels
    }
}

impl Drop for HeadlessApp {
    fn drop(&mut self) {
        // SAFETY: this object uniquely owns the initialized renderer.
        unsafe { imgui_impl_webgl_shutdown() }
        info!("Headless ztracing deinitialized.");
    }
}

unsafe extern "C" {
    fn imgui_impl_webgl_init() -> i32;
    fn imgui_impl_webgl_shutdown();
    fn glReadPixels(
        x: i32,
        y: i32,
        width: i32,
        height: i32,
        format: u32,
        kind: u32,
        pixels: *mut c_void,
    );
}
