pub mod app;
pub mod cli_table;
pub mod colors;
pub mod format;
#[cfg(all(test, target_os = "linux"))]
mod golden;
#[cfg(target_os = "linux")]
pub mod headless;
#[cfg(target_os = "linux")]
pub mod headless_gl;
pub mod imgui;
pub mod platform;
pub mod runtime;
pub mod string_interner;
pub mod trace;
pub mod viewer;
#[cfg(test)]
#[global_allocator]
static TEST_ALLOCATOR: CountingAllocator = CountingAllocator;
#[cfg(test)]
use base::allocation::CountingAllocator;
