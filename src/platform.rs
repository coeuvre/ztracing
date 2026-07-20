use std::sync::OnceLock;
use std::thread::ThreadId;
use std::time::Instant;

#[cfg(target_os = "emscripten")]
use std::ffi::{CStr, CString, c_char};

static START: OnceLock<Instant> = OnceLock::new();
static MAIN_THREAD: OnceLock<ThreadId> = OnceLock::new();

pub fn initialize_main_thread() {
    let _ = MAIN_THREAD.set(std::thread::current().id());
    let _ = START.set(Instant::now());
}

pub fn now_ms() -> f64 {
    #[cfg(target_os = "emscripten")]
    {
        // SAFETY: the JavaScript bridge takes no arguments and returns an f64.
        return unsafe { ztracing_platform_now() };
    }
    #[cfg(not(target_os = "emscripten"))]
    {
        START.get_or_init(Instant::now).elapsed().as_secs_f64() * 1_000.0
    }
}

pub fn is_main_thread() -> bool {
    #[cfg(target_os = "emscripten")]
    {
        // SAFETY: the JavaScript bridge takes no arguments and returns 0 or 1.
        return unsafe { ztracing_platform_is_main_thread() != 0 };
    }
    #[cfg(not(target_os = "emscripten"))]
    {
        matches_registered_thread(MAIN_THREAD.get(), std::thread::current().id())
    }
}

#[cfg(not(target_os = "emscripten"))]
fn matches_registered_thread(registered: Option<&ThreadId>, current: ThreadId) -> bool {
    registered.is_some_and(|registered| registered == &current)
}

pub fn is_dark_mode() -> bool {
    #[cfg(target_os = "emscripten")]
    {
        // SAFETY: the JavaScript bridge takes no arguments and returns 0 or 1.
        unsafe { ztracing_platform_is_dark_mode() != 0 }
    }
    #[cfg(not(target_os = "emscripten"))]
    {
        true
    }
}

pub fn is_mac() -> bool {
    #[cfg(target_os = "emscripten")]
    {
        // SAFETY: the JavaScript bridge takes no arguments and returns 0 or 1.
        unsafe { ztracing_platform_is_mac() != 0 }
    }
    #[cfg(not(target_os = "emscripten"))]
    {
        cfg!(target_os = "macos")
    }
}

pub fn set_setting(key: &str, value: &str) {
    #[cfg(target_os = "emscripten")]
    {
        let (Ok(key), Ok(value)) = (CString::new(key), CString::new(value)) else {
            return;
        };
        // SAFETY: both strings are NUL-terminated and valid for this synchronous call.
        unsafe { ztracing_platform_set_setting(key.as_ptr(), value.as_ptr()) }
    }
    #[cfg(not(target_os = "emscripten"))]
    {
        let _ = (key, value);
    }
}

pub fn get_setting(key: &str) -> Option<String> {
    #[cfg(target_os = "emscripten")]
    {
        let key = CString::new(key).ok()?;
        let mut value = [0_u8; 64];
        // SAFETY: `key` is NUL-terminated and `value` is writable for the supplied length.
        if unsafe {
            ztracing_platform_get_setting(
                key.as_ptr(),
                value.as_mut_ptr().cast(),
                value.len() as i32,
            )
        } == 0
        {
            return None;
        }
        // SAFETY: the JavaScript bridge writes a NUL-terminated UTF-8 string.
        unsafe { CStr::from_ptr(value.as_ptr().cast()) }
            .to_str()
            .ok()
            .map(str::to_owned)
    }
    #[cfg(not(target_os = "emscripten"))]
    {
        let _ = key;
        None
    }
}

pub fn primary_modifier_down(control_down: bool, super_down: bool) -> bool {
    select_primary_modifier(is_mac(), control_down, super_down)
}

fn select_primary_modifier(is_mac: bool, control_down: bool, super_down: bool) -> bool {
    if is_mac { super_down } else { control_down }
}

pub fn open_file_dialog() {
    #[cfg(target_os = "emscripten")]
    // SAFETY: the JavaScript bridge has no arguments and performs the browser interaction.
    unsafe {
        ztracing_platform_open_file_dialog();
    }
}

#[cfg(target_os = "emscripten")]
unsafe extern "C" {
    fn ztracing_platform_now() -> f64;
    fn ztracing_platform_is_main_thread() -> i32;
    fn ztracing_platform_is_dark_mode() -> i32;
    fn ztracing_platform_is_mac() -> i32;
    fn ztracing_platform_open_file_dialog();
    fn ztracing_platform_set_setting(key: *const c_char, value: *const c_char);
    fn ztracing_platform_get_setting(key: *const c_char, value: *mut c_char, length: i32) -> i32;
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn clock_is_monotonic() {
        let first = now_ms();
        let second = now_ms();
        assert!(second >= first);
    }

    #[test]
    fn primary_modifier_uses_command_on_mac_and_control_elsewhere() {
        assert!(select_primary_modifier(true, false, true));
        assert!(!select_primary_modifier(true, true, false));
        assert!(select_primary_modifier(false, true, false));
        assert!(!select_primary_modifier(false, false, true));
    }

    #[test]
    fn main_thread_identity_distinguishes_workers() {
        let registered = std::thread::current().id();
        let worker = std::thread::spawn(|| std::thread::current().id())
            .join()
            .unwrap();
        assert!(matches_registered_thread(Some(&registered), registered));
        assert!(!matches_registered_thread(Some(&registered), worker));
        assert!(!matches_registered_thread(None, registered));
    }
}
