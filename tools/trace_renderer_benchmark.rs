use base::allocation::CountingAllocator;
use std::ffi::{CString, c_char, c_int, c_void};
use std::fmt::Write as _;
use std::fs::{self, File};
use std::io::Read;
use std::path::Path;
use std::process::ExitCode;
use std::time::Instant;
use ztracing::headless::HeadlessApp;

#[global_allocator]
static ALLOCATOR: CountingAllocator = CountingAllocator;

const ITERATIONS: usize = 100;

#[link(name = "z")]
unsafe extern "C" {
    fn gzopen(path: *const c_char, mode: *const c_char) -> *mut c_void;
    fn gzread(file: *mut c_void, buffer: *mut c_void, length: u32) -> c_int;
    fn gzclose(file: *mut c_void) -> c_int;
}

fn main() -> ExitCode {
    let args: Vec<_> = std::env::args().collect();
    if args.len() != 2 {
        eprintln!("usage: {} <trace_file>", args[0]);
        return ExitCode::FAILURE;
    }
    let path = Path::new(&args[1]);
    match benchmark(path) {
        Ok(report) => {
            print!("{report}");
            ExitCode::SUCCESS
        }
        Err(error) => {
            eprintln!("{error}");
            ExitCode::FAILURE
        }
    }
}

fn read_file_bytes(path: &Path) -> Result<Vec<u8>, String> {
    let mut file = File::open(path)
        .map_err(|_| format!("error: could not open file {}", path.display()))?;
    let mut magic = [0_u8; 2];
    let is_gzip = if file.read_exact(&mut magic).is_ok() {
        magic == [0x1f, 0x8b]
    } else {
        false
    };

    if is_gzip {
        let path_c = CString::new(path.as_os_str().as_encoded_bytes())
            .map_err(|_| "error: trace path contains NUL byte".to_owned())?;
        let mode = c"rb";
        let gz_file = unsafe { gzopen(path_c.as_ptr(), mode.as_ptr()) };
        if gz_file.is_null() {
            return Err("error: failed to initialize gzip decompression".to_owned());
        }
        let mut bytes = Vec::new();
        let mut buffer = vec![0_u8; 1024 * 1024];
        loop {
            let read = unsafe { gzread(gz_file, buffer.as_mut_ptr().cast(), buffer.len() as u32) };
            if read <= 0 {
                break;
            }
            bytes.extend_from_slice(&buffer[..read as usize]);
        }
        unsafe { gzclose(gz_file) };
        Ok(bytes)
    } else {
        let mut file = File::open(path)
            .map_err(|_| format!("error: could not open file {}", path.display()))?;
        let mut bytes = Vec::new();
        file.read_to_end(&mut bytes)
            .map_err(|_| format!("error: failed to read file {}", path.display()))?;
        Ok(bytes)
    }
}

fn benchmark(path: &Path) -> Result<String, String> {
    let metadata = fs::metadata(path)
        .map_err(|_| format!("error: could not open file {}", path.display()))?;
    let bytes = read_file_bytes(path)?;
    let filename = path
        .file_name()
        .and_then(|n| n.to_str())
        .unwrap_or("trace.json");

    let mut headless = HeadlessApp::create(1920, 1080)
        .map_err(|e| format!("error: failed to create headless app: {e}"))?;

    // Load trace into production App state via session API
    headless
        .runtime
        .app
        .begin_session(1, filename, bytes.len());
    headless
        .runtime
        .app
        .handle_file_chunk(1, bytes.clone(), bytes.len(), true);

    let started_load = Instant::now();
    while headless.runtime.app.loading.active {
        headless.update();
        if started_load.elapsed().as_secs() > 60 {
            return Err("error: timeout loading trace into headless app".to_owned());
        }
    }

    // Warm up 3 full production UI frames
    headless.update();
    headless.update();
    headless.update();

    let event_count = headless
        .runtime
        .app
        .trace_data
        .as_ref()
        .map_or(0, |td| td.events.len());
    let track_count = headless.runtime.app.viewer.tracks().len();

    // Measure full production UI frame updates (CPU renderer vs GPU software rasterizer)
    let mut total_cpu_ms = 0.0_f64;
    let mut total_gpu_ms = 0.0_f64;
    for _ in 0..ITERATIONS {
        if let Some((cpu, gpu)) = headless.update() {
            let gpu_submit_start = Instant::now();
            headless.submit_frame();
            let gpu_submit_ms = gpu_submit_start.elapsed().as_secs_f64() * 1000.0;
            total_cpu_ms += cpu;
            total_gpu_ms += gpu + gpu_submit_ms;
        }
    }
    let total_ms = total_cpu_ms + total_gpu_ms;
    let avg_cpu_ms = total_cpu_ms / ITERATIONS as f64;
    let avg_gpu_ms = total_gpu_ms / ITERATIONS as f64;
    let avg_total_ms = total_ms / ITERATIONS as f64;
    let cpu_fps = 1000.0 / avg_cpu_ms;
    let total_fps = 1000.0 / avg_total_ms;

    let mut report = String::new();
    writeln!(report, "----------------------------------------").unwrap();
    writeln!(report, "HEADLESS PRODUCTION RENDERER BENCHMARK").unwrap();
    writeln!(report, "----------------------------------------").unwrap();
    writeln!(
        report,
        "File Size:             {:.2} MB (Disk), {:.2} MB (Decompressed)",
        metadata.len() as f64 / (1024.0 * 1024.0),
        bytes.len() as f64 / (1024.0 * 1024.0)
    )
    .unwrap();
    writeln!(report, "Canvas Resolution:     1920 x 1080 (1080p)").unwrap();
    writeln!(report, "Total Events:          {event_count}").unwrap();
    writeln!(report, "Total Tracks:          {track_count}").unwrap();
    writeln!(report, "----------------------------------------").unwrap();
    writeln!(report, "Production Performance Breakdown (avg of {ITERATIONS} frames):").unwrap();
    writeln!(
        report,
        "  Our Renderer (CPU, excl GPU): {avg_cpu_ms:.3} ms / frame ({cpu_fps:.1} FPS)"
    )
    .unwrap();
    writeln!(
        report,
        "  GPU Draw Call Execution:      {avg_gpu_ms:.3} ms / frame"
    )
    .unwrap();
    writeln!(
        report,
        "  Total Full Frame Time:        {avg_total_ms:.3} ms / frame ({total_fps:.1} FPS)"
    )
    .unwrap();
    writeln!(report, "----------------------------------------").unwrap();
    Ok(report)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn headless_benchmark_runs_without_panicking() {
        let path = std::env::var_os("TEST_TMPDIR")
            .map(std::path::PathBuf::from)
            .unwrap_or_else(std::env::temp_dir)
            .join("simple_test_trace.json");
        let content = br#"[{"name":"a","ph":"X","ts":0,"dur":10,"pid":1,"tid":1}]"#;
        std::fs::write(&path, content).unwrap();
        let report = benchmark(&path).unwrap();
        assert!(report.contains("HEADLESS PRODUCTION RENDERER BENCHMARK"));
        assert!(report.contains("FPS"));
        let _ = std::fs::remove_file(path);
    }
}
