//! Frame operations shared by interactive and headless frontends.

use crate::app::App;
use crate::imgui::{Context, DrawData};

pub struct Runtime {
    pub app: App,
    pub(crate) imgui: Context,
}

impl Runtime {
    pub fn new(imgui: Context, dark: bool) -> Self {
        let mut runtime = Self {
            app: App::new(),
            imgui,
        };
        runtime.app.on_theme_changed(dark);
        if runtime.app.take_theme_changed() {
            runtime.imgui.apply_theme(&runtime.app.theme);
        }
        runtime
    }

    pub fn update<S, B>(&mut self, should_render: S, begin_frame: B) -> Option<(f64, f64)>
    where
        S: FnOnce(&App) -> bool,
        B: FnOnce(),
    {
        self.app.poll_completions();
        if !should_render(&self.app) {
            return None;
        }
        begin_frame();
        let cpu_start = std::time::Instant::now();
        let frame = self.imgui.frame();
        self.app.draw(&frame);
        let draw = frame.render();
        if self.app.take_theme_changed() {
            self.imgui.apply_theme(&self.app.theme);
        }
        let cpu_ms = cpu_start.elapsed().as_secs_f64() * 1000.0;

        let gpu_start = std::time::Instant::now();
        clear_frame();
        // SAFETY: the frontend made its GL context current and initialized the
        // shared renderer before entering this function.
        unsafe {
            imgui_impl_webgl_render_draw_data(draw);
        }
        let gpu_ms = gpu_start.elapsed().as_secs_f64() * 1000.0;

        Some((cpu_ms, gpu_ms))
    }

    pub fn on_theme_changed(&mut self, dark: bool) {
        self.app.on_theme_changed(dark);
        if self.app.take_theme_changed() {
            self.imgui.apply_theme(&self.app.theme);
        }
    }

    pub fn set_font(&mut self, font: &[u8], scale: f32) {
        self.imgui.set_font(font, scale);
        // SAFETY: the frontend made its GL context current and initialized the
        // shared renderer before entering this function.
        unsafe {
            imgui_impl_webgl_destroy_fonts_texture();
            imgui_impl_webgl_create_fonts_texture();
        }
    }
}

fn clear_frame() {
    // SAFETY: an active GL context and ImGui context exist for the frame.
    unsafe {
        let size = ig_get_io_display_size();
        let scale = ig_get_io_display_framebuffer_scale();
        glViewport(0, 0, (size.x * scale.x) as i32, (size.y * scale.y) as i32);
        glClearColor(0.45, 0.55, 0.60, 1.0);
        glClear(0x0000_4000);
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
struct Vec2 {
    x: f32,
    y: f32,
}

unsafe extern "C" {
    fn imgui_impl_webgl_render_draw_data(data: *mut DrawData);
    fn imgui_impl_webgl_create_fonts_texture() -> i32;
    fn imgui_impl_webgl_destroy_fonts_texture();
    fn ig_get_io_display_size() -> Vec2;
    fn ig_get_io_display_framebuffer_scale() -> Vec2;
    fn glViewport(x: i32, y: i32, width: i32, height: i32);
    fn glClearColor(red: f32, green: f32, blue: f32, alpha: f32);
    fn glClear(mask: u32);
}
