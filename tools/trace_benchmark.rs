use base::allocation::CountingAllocator;
use std::fmt::Write as _;
use std::fs::{self, File};
use std::io::{self, Read};
use std::path::Path;
use std::process::ExitCode;
use std::time::Instant;
use ztracing::trace::loader::load_file;

#[global_allocator]
static ALLOCATOR: CountingAllocator = CountingAllocator;

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

fn benchmark(path: &Path) -> Result<String, String> {
    let Ok(metadata) = fs::metadata(path) else {
        return Err(format!("error: could not open file {}", path.display()));
    };
    let gzip =
        probe_gzip(path).map_err(|_| format!("error: could not open file {}", path.display()))?;
    let allocation_baseline = CountingAllocator::live_bytes();
    let start = Instant::now();
    let trace = load_file(path)
        .map_err(|_| format!("error: failed to load trace file {}", path.display()))?;
    let total = start.elapsed().as_secs_f64();
    let consumed_memory = CountingAllocator::live_bytes().saturating_sub(allocation_baseline);
    let ingest = trace.ingestion_ms / 1000.0;
    let disk_mb = metadata.len() as f64 / (1024.0 * 1024.0);
    let stream_mb = trace.decompressed_size as f64 / (1024.0 * 1024.0);
    let mut report = String::new();
    writeln!(report, "----------------------------------------").unwrap();
    writeln!(report, "TRACE LOADING BENCHMARK").unwrap();
    writeln!(report, "----------------------------------------").unwrap();
    writeln!(report, "File Size (Disk):      {disk_mb:.2} MB").unwrap();
    if gzip {
        writeln!(
            report,
            "Decompressed Size:     {stream_mb:.2} MB ({:.1}x compression)",
            trace.decompressed_size as f64 / metadata.len() as f64
        )
        .unwrap();
    }
    writeln!(
        report,
        "Compression:           {}",
        if gzip {
            "GZIP (decompressing on-the-fly)"
        } else {
            "None"
        }
    )
    .unwrap();
    writeln!(report, "Total Events:          {}", trace.data.events.len()).unwrap();
    writeln!(report, "Total Tracks:          {}", trace.tracks.len()).unwrap();
    writeln!(report, "----------------------------------------").unwrap();
    writeln!(report, "Ingest Time (Parse+Add): {ingest:.3} s").unwrap();
    writeln!(
        report,
        "  Throughput (Disk Read):   {:.2} MB/s{}",
        disk_mb / ingest,
        if gzip { " (compressed)" } else { "" }
    )
    .unwrap();
    if gzip {
        writeln!(
            report,
            "  Throughput (Decompress):  {:.2} MB/s (decompressed)",
            stream_mb / ingest
        )
        .unwrap();
    }
    writeln!(
        report,
        "  Ingestion Rate:           {:.2} ev/s",
        trace.data.events.len() as f64 / ingest
    )
    .unwrap();
    writeln!(
        report,
        "Track Organize Time:     {:.3} ms ({:.5} s)",
        trace.organization_ms,
        trace.organization_ms / 1000.0
    )
    .unwrap();
    writeln!(report, "Total Ingestion Time:    {total:.3} s").unwrap();
    writeln!(report, "----------------------------------------").unwrap();
    writeln!(
        report,
        "Consumed Memory:       {:.2} MB ({consumed_memory} bytes)",
        consumed_memory as f64 / (1024.0 * 1024.0)
    )
    .unwrap();
    writeln!(report, "----------------------------------------").unwrap();
    Ok(report)
}

fn probe_gzip(path: &Path) -> io::Result<bool> {
    has_gzip_magic(File::open(path)?)
}

fn has_gzip_magic(mut input: impl Read) -> io::Result<bool> {
    let mut magic = [0_u8; 2];
    let mut read = 0;
    while read < magic.len() {
        let count = input.read(&mut magic[read..])?;
        if count == 0 {
            break;
        }
        read += count;
    }
    Ok(read == magic.len() && magic == [0x1f, 0x8b])
}

#[cfg(test)]
mod tests {
    use super::{benchmark, has_gzip_magic};
    use std::ffi::{CString, c_char, c_int, c_void};
    use std::fs;
    use std::io::{self, Read};
    use std::path::PathBuf;
    use std::sync::atomic::{AtomicUsize, Ordering};

    const TRACE: &[u8] = br#"[{"name":"event","ph":"X","ts":1,"dur":2}]"#;
    static NEXT_FILE: AtomicUsize = AtomicUsize::new(0);

    #[link(name = "z")]
    unsafe extern "C" {
        fn gzopen(path: *const c_char, mode: *const c_char) -> *mut c_void;
        fn gzwrite(file: *mut c_void, buffer: *const c_void, length: u32) -> c_int;
        fn gzclose(file: *mut c_void) -> c_int;
    }

    struct PrefixReader {
        bytes: &'static [u8],
        consumed: usize,
    }

    impl Read for PrefixReader {
        fn read(&mut self, output: &mut [u8]) -> io::Result<usize> {
            let count = output.len().min(self.bytes.len() - self.consumed);
            output[..count].copy_from_slice(&self.bytes[self.consumed..self.consumed + count]);
            self.consumed += count;
            Ok(count)
        }
    }

    fn temporary(name: &str) -> PathBuf {
        let directory = std::env::var_os("TEST_TMPDIR")
            .map(PathBuf::from)
            .unwrap_or_else(std::env::temp_dir);
        directory.join(format!(
            "trace_benchmark_{}_{}",
            NEXT_FILE.fetch_add(1, Ordering::Relaxed),
            name
        ))
    }

    fn assert_common_report(report: &str) {
        assert!(report.contains("TRACE LOADING BENCHMARK"));
        assert!(report.contains("Total Events:          1"));
        assert!(report.contains("Total Tracks:          1"));
        assert!(report.contains("Ingest Time (Parse+Add):"));
        assert!(report.contains(" MB/s"));
        assert!(report.contains(" ev/s"));
        assert!(report.contains("Track Organize Time:"));
        assert!(report.contains("Total Ingestion Time:"));
        assert!(report.contains("Consumed Memory:"));
        assert!(report.contains(" bytes)"));
    }

    #[test]
    fn magic_probe_reads_only_two_bytes() {
        let mut reader = PrefixReader {
            bytes: b"\x1f\x8bpayload that must not be read",
            consumed: 0,
        };
        assert!(has_gzip_magic(&mut reader).unwrap());
        assert_eq!(reader.consumed, 2);
    }

    #[test]
    fn raw_report_preserves_legacy_fields_and_units() {
        let path = temporary("raw.json");
        fs::write(&path, TRACE).unwrap();
        let report = benchmark(&path).unwrap();
        assert_common_report(&report);
        assert!(report.contains("Compression:           None"));
        assert!(!report.contains("Decompressed Size:"));
        assert!(!report.contains("(compressed)"));
        fs::remove_file(path).unwrap();
    }

    #[test]
    fn gzip_report_preserves_legacy_fields_and_units() {
        let path = temporary("trace.json.gz");
        let path_string = CString::new(path.as_os_str().as_encoded_bytes()).unwrap();
        unsafe {
            let file = gzopen(path_string.as_ptr(), c"wb".as_ptr());
            assert!(!file.is_null());
            assert_eq!(
                gzwrite(file, TRACE.as_ptr().cast(), TRACE.len() as u32),
                TRACE.len() as c_int
            );
            assert_eq!(gzclose(file), 0);
        }
        let report = benchmark(&path).unwrap();
        assert_common_report(&report);
        assert!(report.contains("Decompressed Size:"));
        assert!(report.contains("GZIP (decompressing on-the-fly)"));
        assert!(report.contains("MB/s (compressed)"));
        assert!(report.contains("MB/s (decompressed)"));
        fs::remove_file(path).unwrap();
    }
}
