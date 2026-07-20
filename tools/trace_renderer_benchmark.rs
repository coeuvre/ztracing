use std::fmt::Write as _;
use std::fs;
use std::path::Path;
use std::process::ExitCode;
use std::time::Instant;
use ztracing::trace::{TraceData, Track, TrackType, loader::load_file};
use ztracing::viewer::TrackRenderer;

const ITERATIONS: usize = 100;
const VIEWPORT_TRACKS: usize = 25;
const VIEWPORT_WIDTH: f32 = 1000.0;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct Viewport {
    start: usize,
    count: usize,
    events: usize,
    threads: usize,
    counters: usize,
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

fn benchmark(path: &Path) -> Result<String, String> {
    let size = fs::metadata(path)
        .map(|metadata| metadata.len())
        .map_err(|_| format!("error: could not open file {}", path.display()))?;
    let trace = load_file(path)
        .map_err(|_| format!("error: failed to load trace file {}", path.display()))?;
    let viewport = select_viewport(&trace.tracks).ok_or_else(|| {
        format!(
            "error: trace file {} has no renderable tracks",
            path.display()
        )
    })?;
    let selected = &trace.tracks[viewport.start..viewport.start + viewport.count];
    let mut renderer = TrackRenderer::default();
    let milliseconds = measure_frames(ITERATIONS, || {
        render_frame(
            &mut renderer,
            selected,
            &trace.data,
            trace.minimum_timestamp as f64,
            trace.maximum_timestamp as f64,
        )
    });

    let mut report = String::new();
    writeln!(report, "----------------------------------------").unwrap();
    writeln!(report, "TRACE RENDERER BENCHMARK (VERTICAL VIEWPORT SCAN)").unwrap();
    writeln!(report, "----------------------------------------").unwrap();
    writeln!(
        report,
        "File Size:             {:.2} MB",
        size as f64 / (1024.0 * 1024.0)
    )
    .unwrap();
    writeln!(report, "Total Tracks:          {}", trace.tracks.len()).unwrap();
    writeln!(report, "Viewport Size:         {} tracks", viewport.count).unwrap();
    writeln!(report, "  Thread Tracks:       {}", viewport.threads).unwrap();
    writeln!(report, "  Counter Tracks:      {}", viewport.counters).unwrap();
    writeln!(report, "  Total Viewport Events: {}", viewport.events).unwrap();
    writeln!(
        report,
        "  Hottest Track Block: index {} to {}",
        viewport.start,
        viewport.start + viewport.count - 1
    )
    .unwrap();
    writeln!(report, "----------------------------------------").unwrap();
    writeln!(report, "Full Viewport Render (Fully Zoomed Out):").unwrap();
    writeln!(report, "  Total Time:          {milliseconds:.3} ms").unwrap();
    writeln!(
        report,
        "  Average Frame Time:  {:.3} ms (avg of {ITERATIONS} runs)",
        milliseconds / ITERATIONS as f64
    )
    .unwrap();
    writeln!(report, "----------------------------------------").unwrap();
    Ok(report)
}

fn select_viewport(tracks: &[Track]) -> Option<Viewport> {
    if tracks.is_empty() {
        return None;
    }
    let count = VIEWPORT_TRACKS.min(tracks.len());
    let mut best_start = 0;
    let mut best_events = tracks[..count]
        .iter()
        .map(|track| track.event_indices.len())
        .sum();
    for start in 1..=tracks.len() - count {
        let events = tracks[start..start + count]
            .iter()
            .map(|track| track.event_indices.len())
            .sum();
        if events > best_events {
            best_start = start;
            best_events = events;
        }
    }
    let selected = &tracks[best_start..best_start + count];
    let threads = selected
        .iter()
        .filter(|track| track.kind == TrackType::Thread)
        .count();
    let counters = selected
        .iter()
        .filter(|track| track.kind == TrackType::Counter)
        .count();
    Some(Viewport {
        start: best_start,
        count,
        events: best_events,
        threads,
        counters,
    })
}

fn measure_frames(iterations: usize, mut render: impl FnMut() -> usize) -> f64 {
    std::hint::black_box(render());
    let started = Instant::now();
    for _ in 0..iterations {
        std::hint::black_box(render());
    }
    started.elapsed().as_secs_f64() * 1000.0
}

fn render_frame(
    renderer: &mut TrackRenderer,
    tracks: &[Track],
    data: &TraceData,
    viewport_start: f64,
    viewport_end: f64,
) -> usize {
    let mut block_count = 0_usize;
    for track in tracks {
        let count = if track.kind == TrackType::Thread {
            renderer
                .thread_blocks(
                    track,
                    data,
                    viewport_start,
                    viewport_end,
                    VIEWPORT_WIDTH,
                    0.0,
                    None,
                )
                .len()
        } else {
            renderer
                .counter_blocks(
                    track,
                    data,
                    viewport_start,
                    viewport_end,
                    VIEWPORT_WIDTH,
                    0.0,
                    None,
                )
                .len()
        };
        block_count = block_count.wrapping_add(count);
    }
    block_count
}

#[cfg(test)]
mod tests {
    use super::{ITERATIONS, benchmark, measure_frames, select_viewport};
    use std::cell::Cell;
    use std::fs;
    use std::path::PathBuf;
    use std::sync::atomic::{AtomicUsize, Ordering};
    use ztracing::trace::{Track, TrackType};

    static NEXT_FILE: AtomicUsize = AtomicUsize::new(0);

    fn temporary(name: &str, contents: &[u8]) -> PathBuf {
        let directory = std::env::var_os("TEST_TMPDIR")
            .map(PathBuf::from)
            .unwrap_or_else(std::env::temp_dir);
        let path = directory.join(format!(
            "trace_renderer_benchmark_{}_{}",
            NEXT_FILE.fetch_add(1, Ordering::Relaxed),
            name
        ));
        fs::write(&path, contents).unwrap();
        path
    }

    #[test]
    fn viewport_ties_keep_the_first_block() {
        let tracks = (0..26)
            .map(|_| Track {
                event_indices: vec![0],
                ..Track::default()
            })
            .collect::<Vec<_>>();
        let viewport = select_viewport(&tracks).unwrap();
        assert_eq!(viewport.start, 0);
        assert_eq!(viewport.count, 25);
        assert_eq!(viewport.events, 25);
    }

    #[test]
    fn one_frame_is_warmed_before_measurement() {
        let calls = Cell::new(0);
        measure_frames(3, || {
            calls.set(calls.get() + 1);
            0
        });
        assert_eq!(calls.get(), 4);
    }

    #[test]
    fn empty_trace_is_rejected() {
        let path = temporary("empty.json", b"[]");
        let error = benchmark(&path).unwrap_err();
        assert!(error.contains("has no renderable tracks"));
        fs::remove_file(path).unwrap();
    }

    #[test]
    fn mixed_track_report_preserves_workload_and_units() {
        let path = temporary(
            "mixed.json",
            br#"[
                {"name":"thread","ph":"X","pid":1,"tid":1,"ts":0,"dur":100},
                {"name":"counter","ph":"C","pid":1,"tid":1,"ts":10,"args":{"value":1}},
                {"name":"counter","ph":"C","pid":1,"tid":1,"ts":90,"args":{"value":2}}
            ]"#,
        );
        let report = benchmark(&path).unwrap();
        assert!(report.contains("Total Tracks:          2"));
        assert!(report.contains("Viewport Size:         2 tracks"));
        assert!(report.contains("  Thread Tracks:       1"));
        assert!(report.contains("  Counter Tracks:      1"));
        assert!(report.contains("  Total Viewport Events: 3"));
        assert!(report.contains("  Hottest Track Block: index 0 to 1"));
        assert!(report.contains("  Total Time:"));
        assert!(report.contains(" ms"));
        assert!(report.contains(&format!("(avg of {ITERATIONS} runs)")));
        fs::remove_file(path).unwrap();
    }

    #[test]
    fn viewport_reports_exact_track_types() {
        let tracks = [
            Track {
                kind: TrackType::Thread,
                event_indices: vec![0],
                ..Track::default()
            },
            Track {
                kind: TrackType::Counter,
                event_indices: vec![1],
                ..Track::default()
            },
        ];
        let viewport = select_viewport(&tracks).unwrap();
        assert_eq!(viewport.threads, 1);
        assert_eq!(viewport.counters, 1);
    }
}
