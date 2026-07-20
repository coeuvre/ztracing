//! Minimal logging without an external facade dependency.

use std::fmt;
#[cfg(not(target_os = "emscripten"))]
use std::io::{self, Write};
use std::sync::OnceLock;

#[repr(i32)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Level {
    Debug,
    Info,
    Warn,
    Error,
}

impl Level {
    pub const fn name(self) -> &'static str {
        match self {
            Self::Debug => "DEBUG",
            Self::Info => "INFO",
            Self::Warn => "WARN",
            Self::Error => "ERROR",
        }
    }
}

pub const fn enabled(level: Level) -> bool {
    !matches!(level, Level::Debug) || cfg!(debug_assertions)
}

pub trait LogWriter: Sync {
    fn write(&self, level: Level, args: fmt::Arguments<'_>);
}

static WRITER: OnceLock<&'static dyn LogWriter> = OnceLock::new();

pub fn set_writer(writer: &'static dyn LogWriter) -> Result<(), &'static dyn LogWriter> {
    WRITER.set(writer)
}

#[cfg(not(target_os = "emscripten"))]
pub struct StderrWriter;

#[cfg(not(target_os = "emscripten"))]
pub static STDERR_WRITER: StderrWriter = StderrWriter;

#[cfg(not(target_os = "emscripten"))]
impl LogWriter for StderrWriter {
    fn write(&self, level: Level, args: fmt::Arguments<'_>) {
        let mut stderr = io::stderr().lock();
        let _ = write_line(&mut stderr, level, args);
    }
}

#[cfg(not(target_os = "emscripten"))]
fn write_line(output: &mut impl Write, level: Level, args: fmt::Arguments<'_>) -> io::Result<()> {
    writeln!(output, "[{}] {args}", level.name())
}

pub fn log(level: Level, args: fmt::Arguments<'_>) {
    if !enabled(level) {
        return;
    }
    if let Some(writer) = WRITER.get() {
        writer.write(level, args);
    }
}

#[macro_export]
macro_rules! debug {
    ($($arg:tt)*) => {
        if $crate::logging::enabled($crate::logging::Level::Debug) {
            $crate::logging::log($crate::logging::Level::Debug, format_args!($($arg)*))
        }
    };
}

#[macro_export]
macro_rules! info {
    ($($arg:tt)*) => {
        if $crate::logging::enabled($crate::logging::Level::Info) {
            $crate::logging::log($crate::logging::Level::Info, format_args!($($arg)*))
        }
    };
}

#[macro_export]
macro_rules! warn {
    ($($arg:tt)*) => {
        if $crate::logging::enabled($crate::logging::Level::Warn) {
            $crate::logging::log($crate::logging::Level::Warn, format_args!($($arg)*))
        }
    };
}

#[macro_export]
macro_rules! error {
    ($($arg:tt)*) => {
        if $crate::logging::enabled($crate::logging::Level::Error) {
            $crate::logging::log($crate::logging::Level::Error, format_args!($($arg)*))
        }
    };
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Mutex;

    static MESSAGES: Mutex<Vec<(Level, String)>> = Mutex::new(Vec::new());

    struct TestWriter;

    impl LogWriter for TestWriter {
        fn write(&self, level: Level, args: fmt::Arguments<'_>) {
            MESSAGES.lock().unwrap().push((level, args.to_string()));
        }
    }

    static TEST_WRITER: TestWriter = TestWriter;

    #[test]
    fn severity_names_follow_rust_logging_conventions() {
        assert_eq!(Level::Debug.name(), "DEBUG");
        assert_eq!(Level::Info.name(), "INFO");
        assert_eq!(Level::Warn.name(), "WARN");
        assert_eq!(Level::Error.name(), "ERROR");
    }

    #[test]
    fn installed_writer_receives_level_and_message() {
        let _ = set_writer(&TEST_WRITER);
        log(Level::Warn, format_args!("value {}", 7));
        assert_eq!(
            MESSAGES.lock().unwrap().pop(),
            Some((Level::Warn, "value 7".to_owned()))
        );
    }

    #[test]
    fn native_writer_format_has_level_message_and_newline() {
        let mut output = Vec::new();
        write_line(&mut output, Level::Warn, format_args!("value {}", 7)).unwrap();
        assert_eq!(output, b"[WARN] value 7\n");
    }

    #[test]
    fn native_writer_format_does_not_truncate_long_messages() {
        let message = "x".repeat(2048);
        let mut output = Vec::new();
        write_line(&mut output, Level::Info, format_args!("{message}")).unwrap();
        assert_eq!(output.len(), "[INFO] ".len() + message.len() + 1);
        assert!(output.ends_with(b"\n"));
    }

    #[test]
    fn debug_filter_follows_build_mode() {
        assert_eq!(enabled(Level::Debug), cfg!(debug_assertions));
        assert!(enabled(Level::Info));
        assert!(enabled(Level::Warn));
        assert!(enabled(Level::Error));
    }

    #[test]
    fn wasm_level_values_match_the_javascript_bridge() {
        assert_eq!(Level::Debug as i32, 0);
        assert_eq!(Level::Info as i32, 1);
        assert_eq!(Level::Warn as i32, 2);
        assert_eq!(Level::Error as i32, 3);
    }
}
