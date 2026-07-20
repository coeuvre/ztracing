use std::ffi::{CStr, CString, c_char, c_int, c_void};
use std::fs::File;
use std::io::{Read, Seek, SeekFrom};
use std::path::Path;
use std::sync::Arc;
use std::thread;
use std::time::Duration;

use base::task::{Status, TaskQueue, ThreadPoolExecutor};

use super::session::{LoadSession, LoadedTrace};
use super::{TraceData, data::EventMatcher, parser::TraceParser};

const CHUNK_SIZE: usize = 1024 * 1024;
const BACKPRESSURE_THRESHOLD: usize = 32 * 1024 * 1024;
const Z_OK: c_int = 0;

#[link(name = "z")]
unsafe extern "C" {
    fn gzopen(path: *const c_char, mode: *const c_char) -> *mut c_void;
    fn gzread(file: *mut c_void, buffer: *mut c_void, length: u32) -> c_int;
    fn gzerror(file: *mut c_void, error: *mut c_int) -> *const c_char;
    fn gzclose(file: *mut c_void) -> c_int;
}

pub fn load_file(path: &Path) -> Result<LoadedTrace, String> {
    let mut file = File::open(path)
        .map_err(|_| format!("Error: Failed to open trace file '{}'", path.display()))?;
    let executor = Arc::new(ThreadPoolExecutor::new(1));
    let queue = TaskQueue::new(executor, 1);
    let session = LoadSession::new(&queue, |result| result)
        .map_err(|error| format!("Error: Failed to start trace loader: {error:?}"))?;
    let mut magic = [0_u8; 2];
    let mut read = 0;
    while read < magic.len() {
        let count = file.read(&mut magic[read..]).map_err(|error| {
            format!(
                "Error: Failed to read trace file '{}': {error}",
                path.display()
            )
        })?;
        if count == 0 {
            break;
        }
        read += count;
    }
    let gzip = read == 2 && magic == [0x1f, 0x8b];

    if gzip {
        drop(file);
        read_gzip(path, |chunk, eof| push_chunk(&session, chunk.to_vec(), eof))?;
    } else {
        file.seek(SeekFrom::Start(0)).map_err(|error| {
            format!(
                "Error: Failed to seek trace file '{}': {error}",
                path.display()
            )
        })?;
        let mut buffer = vec![0_u8; CHUNK_SIZE];
        loop {
            let count = file.read(&mut buffer).map_err(|error| {
                format!(
                    "Error: Failed to read trace file '{}': {error}",
                    path.display()
                )
            })?;
            let eof = count == 0;
            push_chunk(&session, buffer[..count].to_vec(), eof)?;
            if eof {
                break;
            }
        }
    }

    loop {
        let Some(completion) = queue.wait_completion_timeout(Duration::from_millis(10)) else {
            continue;
        };
        return match completion.status {
            Status::Ok => completion
                .output
                .expect("successful loader completion has output")
                .map_err(|error| {
                    if error == "invalid trace JSON" {
                        if gzip {
                            "Error: Gzip decompression failed (invalid or truncated stream)"
                                .to_owned()
                        } else {
                            format!("Error: Failed to parse trace file '{}'", path.display())
                        }
                    } else {
                        error
                    }
                }),
            Status::Cancelled => Err("Error: Trace loading was cancelled".to_owned()),
            Status::Failed => Err("Error: Trace loader failed".to_owned()),
            Status::Panicked => Err("Error: Trace loader panicked".to_owned()),
        };
    }
}

fn push_chunk(session: &LoadSession, bytes: Vec<u8>, eof: bool) -> Result<(), String> {
    session
        .push(bytes, eof)
        .map_err(|_| "Error: Trace loader stopped accepting input".to_owned())?;
    while session.progress().buffered_bytes() > BACKPRESSURE_THRESHOLD {
        thread::sleep(Duration::from_millis(1));
    }
    Ok(())
}

fn read_gzip(
    path: &Path,
    mut consume: impl FnMut(&[u8], bool) -> Result<(), String>,
) -> Result<(), String> {
    let path = CString::new(path.as_os_str().as_encoded_bytes())
        .map_err(|_| "Error: Trace path contains a NUL byte".to_owned())?;
    let mode = c"rb";
    // SAFETY: Both arguments are valid NUL-terminated strings.
    let file = unsafe { gzopen(path.as_ptr(), mode.as_ptr()) };
    if file.is_null() {
        return Err("Error: Failed to initialize gzip decompression".to_owned());
    }
    struct Guard(Option<*mut c_void>);
    impl Guard {
        fn close(&mut self) -> c_int {
            self.0.take().map_or(Z_OK, |file| unsafe { gzclose(file) })
        }
    }
    impl Drop for Guard {
        fn drop(&mut self) {
            if let Some(file) = self.0.take() {
                unsafe {
                    gzclose(file);
                }
            }
        }
    }
    let mut guard = Guard(Some(file));
    let mut buffer = vec![0_u8; CHUNK_SIZE];
    loop {
        // SAFETY: `guard.0` is an open gz handle and the output allocation covers `buffer.len()` bytes.
        let file = guard.0.expect("gzip handle is open");
        let count = unsafe { gzread(file, buffer.as_mut_ptr().cast(), buffer.len() as u32) };
        let mut code = Z_OK;
        // SAFETY: The handle remains open and zlib owns the returned error buffer.
        let message = unsafe { gzerror(file, &mut code) };
        if count < 0 || code != Z_OK {
            let message = if message.is_null() {
                "unknown gzip error".to_owned()
            } else {
                unsafe { CStr::from_ptr(message) }
                    .to_string_lossy()
                    .into_owned()
            };
            return Err(format!("Error: Gzip decompression failed: {message}"));
        }
        let count = count as usize;
        let eof = count < buffer.len();
        consume(&buffer[..count], eof)?;
        if eof {
            break;
        }
    }
    let status = guard.close();
    if status != Z_OK {
        return Err(format!(
            "Error: Gzip decompression failed while closing stream (code {status})"
        ));
    }
    Ok(())
}

pub fn load_bytes(bytes: &[u8]) -> Result<TraceData, String> {
    let mut parser = TraceParser::new();
    parser.feed(bytes, true);
    let mut data = TraceData::new();
    let mut matcher = EventMatcher::default();
    while let Some(event) = parser.next_event() {
        data.add_event(&event, &mut matcher);
    }
    if parser.is_invalid() || !parser.is_complete() {
        Err("invalid trace JSON".to_owned())
    } else {
        Ok(data)
    }
}

#[cfg(test)]
mod tests {
    use super::{CHUNK_SIZE, load_bytes, load_file};
    use std::ffi::{CString, c_char, c_int, c_void};
    use std::fs;
    use std::path::{Path, PathBuf};
    use std::sync::atomic::{AtomicUsize, Ordering};

    static NEXT_FILE: AtomicUsize = AtomicUsize::new(0);
    const TRACE: &[u8] = br#"[{"name":"event","ph":"X","ts":1,"dur":2}]"#;

    #[link(name = "z")]
    unsafe extern "C" {
        fn gzopen(path: *const c_char, mode: *const c_char) -> *mut c_void;
        fn gzwrite(file: *mut c_void, buffer: *const c_void, length: u32) -> c_int;
        fn gzclose(file: *mut c_void) -> c_int;
    }

    fn temporary(name: &str, bytes: &[u8]) -> PathBuf {
        let directory = std::env::var_os("TEST_TMPDIR")
            .map(PathBuf::from)
            .unwrap_or_else(std::env::temp_dir);
        let path = directory.join(format!(
            "loader_{}_{}",
            NEXT_FILE.fetch_add(1, Ordering::Relaxed),
            name
        ));
        fs::write(&path, bytes).unwrap();
        path
    }

    fn gzip(name: &str, bytes: &[u8]) -> PathBuf {
        let path = temporary(name, &[]);
        let path_string = CString::new(path.as_os_str().as_encoded_bytes()).unwrap();
        unsafe {
            let file = gzopen(path_string.as_ptr(), c"wb".as_ptr());
            assert!(!file.is_null());
            assert_eq!(
                gzwrite(file, bytes.as_ptr().cast(), bytes.len() as u32),
                bytes.len() as c_int
            );
            assert_eq!(gzclose(file), 0);
        }
        path
    }

    fn json_with_size(size: usize) -> Vec<u8> {
        const PREFIX: &str = r#"[{"name":"event","padding":""#;
        const SUFFIX: &str = r#""}]"#;
        assert!(size >= PREFIX.len() + SUFFIX.len());
        format!(
            "{PREFIX}{}{SUFFIX}",
            "x".repeat(size - PREFIX.len() - SUFFIX.len())
        )
        .into_bytes()
    }

    fn assert_loaded(path: &Path, expected_size: usize, expected_events: usize) {
        let loaded = load_file(path).unwrap();
        assert_eq!(loaded.decompressed_size, expected_size);
        assert_eq!(loaded.data.events.len(), expected_events);
    }

    #[test]
    fn rejects_malformed_and_incomplete_json() {
        assert!(load_bytes(br#"[{"name":"ok"},{"ts":01}]"#).is_err());
        assert!(load_bytes(br#"[{"name":"ok"},"#).is_err());
        assert!(load_bytes(b" \n\r\t").is_err());
    }

    #[test]
    fn loads_raw_and_gzip_by_magic_not_extension() {
        let raw = temporary("plain.json.gz", TRACE);
        let compressed = gzip("compressed.bin", TRACE);

        assert_loaded(&raw, TRACE.len(), 1);
        assert_loaded(&compressed, TRACE.len(), 1);

        fs::remove_file(raw).unwrap();
        fs::remove_file(compressed).unwrap();
    }

    #[test]
    fn raw_payloads_across_chunk_boundary_are_complete() {
        for size in [CHUNK_SIZE - 1, CHUNK_SIZE, CHUNK_SIZE + 1] {
            let json = json_with_size(size);
            let path = temporary("boundary.json", &json);
            assert_loaded(&path, size, 1);
            fs::remove_file(path).unwrap();
        }
    }

    #[test]
    fn gzip_payload_at_chunk_boundary_is_complete() {
        let json = json_with_size(CHUNK_SIZE);
        let path = gzip("boundary.gz", &json);
        assert_loaded(&path, json.len(), 1);
        fs::remove_file(path).unwrap();
    }

    #[test]
    fn rejects_gzip_with_truncated_trailer() {
        let path = gzip("truncated.gz", TRACE);
        let bytes = fs::read(&path).unwrap();
        fs::write(&path, &bytes[..bytes.len() - 4]).unwrap();

        let error = load_file(&path).err().expect("truncated gzip was accepted");

        assert!(error.contains("Gzip decompression failed"));
        assert_eq!(open_descriptors_for(&path), 0);
        fs::remove_file(path).unwrap();
    }

    #[test]
    fn rejects_gzip_with_corrupt_checksum() {
        let path = gzip("checksum.gz", TRACE);
        let mut bytes = fs::read(&path).unwrap();
        let checksum = bytes.len() - 8;
        bytes[checksum] ^= 0xff;
        fs::write(&path, bytes).unwrap();

        let error = load_file(&path).err().expect("corrupt gzip was accepted");

        assert!(error.contains("Gzip decompression failed"));
        assert_eq!(open_descriptors_for(&path), 0);
        fs::remove_file(path).unwrap();
    }

    #[test]
    fn concatenated_gzip_members_form_one_input_stream() {
        let first = gzip("first.gz", br#"[{"name":"first"},"#);
        let second = gzip("second.gz", br#"{"name":"second"}]"#);
        let mut concatenated = fs::read(&first).unwrap();
        concatenated.extend(fs::read(&second).unwrap());
        let path = temporary("concatenated.gz", &concatenated);

        assert_loaded(&path, br#"[{"name":"first"},{"name":"second"}]"#.len(), 2);

        fs::remove_file(first).unwrap();
        fs::remove_file(second).unwrap();
        fs::remove_file(path).unwrap();
    }

    #[test]
    fn empty_raw_and_gzip_inputs_are_rejected_and_closed() {
        let raw = temporary("empty.json", &[]);
        let compressed = gzip("empty.gz", &[]);

        assert!(load_file(&raw).is_err());
        assert!(load_file(&compressed).is_err());
        assert_eq!(open_descriptors_for(&raw), 0);
        assert_eq!(open_descriptors_for(&compressed), 0);

        fs::remove_file(raw).unwrap();
        fs::remove_file(compressed).unwrap();
    }

    #[cfg(target_os = "linux")]
    fn open_descriptors_for(path: &Path) -> usize {
        let expected = fs::canonicalize(path).unwrap();
        fs::read_dir("/proc/self/fd")
            .unwrap()
            .filter_map(Result::ok)
            .filter_map(|entry| fs::read_link(entry.path()).ok())
            .filter(|target| target == &expected)
            .count()
    }

    #[cfg(not(target_os = "linux"))]
    fn open_descriptors_for(_path: &Path) -> usize {
        0
    }
}
