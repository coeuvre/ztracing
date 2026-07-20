use base::{error, info};
use std::ffi::{c_int, c_uint, c_void};
use std::ptr;
use std::sync::atomic::{AtomicBool, Ordering};

static CONTEXT_ACTIVE: AtomicBool = AtomicBool::new(false);

const EGL_NONE: i32 = 0x3038;
const EGL_SURFACE_TYPE: i32 = 0x3033;
const EGL_PBUFFER_BIT: i32 = 1;
const EGL_BLUE_SIZE: i32 = 0x3022;
const EGL_GREEN_SIZE: i32 = 0x3023;
const EGL_RED_SIZE: i32 = 0x3024;
const EGL_ALPHA_SIZE: i32 = 0x3021;
const EGL_DEPTH_SIZE: i32 = 0x3025;
const EGL_RENDERABLE_TYPE: i32 = 0x3040;
const EGL_OPENGL_ES3_BIT: i32 = 0x40;
const EGL_CONTEXT_CLIENT_VERSION: i32 = 0x3098;
const EGL_WIDTH: i32 = 0x3057;
const EGL_HEIGHT: i32 = 0x3056;
const GL_FRAMEBUFFER: u32 = 0x8D40;
const GL_RENDERBUFFER: u32 = 0x8D41;
const GL_RGBA8: u32 = 0x8058;
const GL_COLOR_ATTACHMENT0: u32 = 0x8CE0;
const GL_FRAMEBUFFER_COMPLETE: u32 = 0x8CD5;
pub struct Context {
    display: *mut c_void,
    context: *mut c_void,
    surface: *mut c_void,
    framebuffer: u32,
    color_buffer: u32,
    width: i32,
    height: i32,
    _reservation: ContextReservation,
}

struct ContextReservation {
    active: &'static AtomicBool,
}

impl ContextReservation {
    fn acquire(active: &'static AtomicBool) -> Result<Self, String> {
        active
            .compare_exchange(false, true, Ordering::AcqRel, Ordering::Acquire)
            .map(|_| Self { active })
            .map_err(|_| "only one headless GL context may be active".to_owned())
    }
}

impl Drop for ContextReservation {
    fn drop(&mut self) {
        self.active.store(false, Ordering::Release);
    }
}

impl Context {
    pub fn create(width: i32, height: i32) -> Result<Self, String> {
        if width <= 0 || height <= 0 {
            return Err("headless dimensions must be positive".to_owned());
        }
        let reservation = ContextReservation::acquire(&CONTEXT_ACTIVE)?;
        if std::env::var_os("EGL_PLATFORM").is_none() {
            // SAFETY: headless context creation occurs before App constructs its
            // worker pool, and the reservation serializes context initialization.
            unsafe { std::env::set_var("EGL_PLATFORM", "surfaceless") };
        }
        let display = unsafe { eglGetDisplay(ptr::null_mut()) };
        if display.is_null() {
            return Err(last_egl_error("get EGL display"));
        }
        let mut major = 0;
        let mut minor = 0;
        if unsafe { eglInitialize(display, &mut major, &mut minor) } == 0 {
            return Err(last_egl_error("initialize EGL"));
        }
        let attributes = [
            EGL_SURFACE_TYPE,
            EGL_PBUFFER_BIT,
            EGL_BLUE_SIZE,
            8,
            EGL_GREEN_SIZE,
            8,
            EGL_RED_SIZE,
            8,
            EGL_ALPHA_SIZE,
            8,
            EGL_DEPTH_SIZE,
            0,
            EGL_RENDERABLE_TYPE,
            EGL_OPENGL_ES3_BIT,
            EGL_NONE,
        ];
        let mut config = ptr::null_mut();
        let mut count = 0;
        if unsafe { eglChooseConfig(display, attributes.as_ptr(), &mut config, 1, &mut count) } == 0
            || count == 0
        {
            let error = unsafe { eglGetError() };
            unsafe { eglTerminate(display) };
            return Err(egl_error("choose EGL config", error));
        }
        let context_attributes = [EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE];
        let context = unsafe {
            eglCreateContext(
                display,
                config,
                ptr::null_mut(),
                context_attributes.as_ptr(),
            )
        };
        if context.is_null() {
            let error = unsafe { eglGetError() };
            unsafe { eglTerminate(display) };
            return Err(egl_error("create EGL context", error));
        }
        let surface_attributes = [EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE];
        let surface =
            unsafe { eglCreatePbufferSurface(display, config, surface_attributes.as_ptr()) };
        if surface.is_null() {
            let error = unsafe { eglGetError() };
            unsafe {
                eglDestroyContext(display, context);
                eglTerminate(display)
            };
            return Err(egl_error("create EGL surface", error));
        }
        if unsafe { eglMakeCurrent(display, surface, surface, context) } == 0 {
            let error = unsafe { eglGetError() };
            unsafe {
                eglDestroySurface(display, surface);
                eglDestroyContext(display, context);
                eglTerminate(display)
            };
            return Err(egl_error("make EGL context current", error));
        }
        let mut framebuffer = 0;
        let mut color_buffer = 0;
        unsafe {
            glGenFramebuffers(1, &mut framebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            glGenRenderbuffers(1, &mut color_buffer);
            glBindRenderbuffer(GL_RENDERBUFFER, color_buffer);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
            glFramebufferRenderbuffer(
                GL_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT0,
                GL_RENDERBUFFER,
                color_buffer,
            )
        }
        let framebuffer_status = unsafe { glCheckFramebufferStatus(GL_FRAMEBUFFER) };
        if framebuffer_status != GL_FRAMEBUFFER_COMPLETE {
            unsafe {
                glDeleteFramebuffers(1, &framebuffer);
                glDeleteRenderbuffers(1, &color_buffer);
                eglMakeCurrent(display, ptr::null_mut(), ptr::null_mut(), ptr::null_mut());
                eglDestroySurface(display, surface);
                eglDestroyContext(display, context);
                eglTerminate(display)
            };
            let message =
                format!("headless framebuffer is incomplete (status: 0x{framebuffer_status:x})");
            error!("{message}");
            return Err(message);
        }
        info!(
            "Headless GL context initialized successfully (EGL {major}.{minor}, FBO: {framebuffer})"
        );
        Ok(Self {
            display,
            context,
            surface,
            framebuffer,
            color_buffer,
            width,
            height,
            _reservation: reservation,
        })
    }

    pub fn width(&self) -> i32 {
        self.width
    }

    pub fn height(&self) -> i32 {
        self.height
    }
}
impl Drop for Context {
    fn drop(&mut self) {
        // SAFETY: all handles are live, uniquely owned, and destroyed in reverse order.
        unsafe {
            glDeleteFramebuffers(1, &self.framebuffer);
            glDeleteRenderbuffers(1, &self.color_buffer);
            eglMakeCurrent(
                self.display,
                ptr::null_mut(),
                ptr::null_mut(),
                ptr::null_mut(),
            );
            eglDestroySurface(self.display, self.surface);
            eglDestroyContext(self.display, self.context);
            eglTerminate(self.display);
        }
        info!("Headless GL context shut down.");
    }
}

fn last_egl_error(operation: &str) -> String {
    egl_error(operation, unsafe { eglGetError() })
}

fn egl_error(operation: &str, error_code: c_int) -> String {
    let message = format!("failed to {operation} (EGL error: 0x{error_code:x})");
    error!("{message}");
    message
}

#[cfg(test)]
mod tests {
    use super::*;

    static TEST_CONTEXT_ACTIVE: AtomicBool = AtomicBool::new(false);

    #[test]
    fn dimensions_must_be_positive() {
        assert_eq!(
            Context::create(0, 600).err().as_deref(),
            Some("headless dimensions must be positive")
        );
        assert_eq!(
            Context::create(800, -1).err().as_deref(),
            Some("headless dimensions must be positive")
        );
    }

    #[test]
    fn context_reservation_is_exclusive_and_released_on_drop() {
        let first = ContextReservation::acquire(&TEST_CONTEXT_ACTIVE).unwrap();
        assert_eq!(
            ContextReservation::acquire(&TEST_CONTEXT_ACTIVE)
                .err()
                .as_deref(),
            Some("only one headless GL context may be active")
        );
        drop(first);
        assert!(ContextReservation::acquire(&TEST_CONTEXT_ACTIVE).is_ok());
    }
}

unsafe extern "C" {
    fn eglGetDisplay(display: *mut c_void) -> *mut c_void;
    fn eglInitialize(display: *mut c_void, major: *mut c_int, minor: *mut c_int) -> c_uint;
    fn eglChooseConfig(
        display: *mut c_void,
        attributes: *const c_int,
        config: *mut *mut c_void,
        size: c_int,
        count: *mut c_int,
    ) -> c_uint;
    fn eglCreateContext(
        display: *mut c_void,
        config: *mut c_void,
        share: *mut c_void,
        attributes: *const c_int,
    ) -> *mut c_void;
    fn eglCreatePbufferSurface(
        display: *mut c_void,
        config: *mut c_void,
        attributes: *const c_int,
    ) -> *mut c_void;
    fn eglMakeCurrent(
        display: *mut c_void,
        draw: *mut c_void,
        read: *mut c_void,
        context: *mut c_void,
    ) -> c_uint;
    fn eglDestroySurface(display: *mut c_void, surface: *mut c_void) -> c_uint;
    fn eglDestroyContext(display: *mut c_void, context: *mut c_void) -> c_uint;
    fn eglTerminate(display: *mut c_void) -> c_uint;
    fn eglGetError() -> c_int;
    fn glGenFramebuffers(count: c_int, values: *mut c_uint);
    fn glBindFramebuffer(target: c_uint, value: c_uint);
    fn glGenRenderbuffers(count: c_int, values: *mut c_uint);
    fn glBindRenderbuffer(target: c_uint, value: c_uint);
    fn glRenderbufferStorage(target: c_uint, format: c_uint, width: c_int, height: c_int);
    fn glFramebufferRenderbuffer(
        target: c_uint,
        attachment: c_uint,
        render_target: c_uint,
        value: c_uint,
    );
    fn glCheckFramebufferStatus(target: c_uint) -> c_uint;
    fn glDeleteFramebuffers(count: c_int, values: *const c_uint);
    fn glDeleteRenderbuffers(count: c_int, values: *const c_uint);
}
