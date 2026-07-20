use base::allocation::CountingAllocator;
use base::logging::{STDERR_WRITER, set_writer};
use std::path::Path;
use std::process::ExitCode;
use std::str::FromStr;

use ztracing::cli_table::{Align, Table};
use ztracing::trace::loader::load_file;
use ztracing::trace::session::LoadedTrace;
use ztracing::trace::{TraceData, Track, TrackType};
use ztracing::trace::{aggregate, concurrency, diff, histogram};

const USAGE: &str = "Usage: {program} <subcommand> <trace_file> [options]\n\nSubcommands:\n  summary <trace_file>         Print high-level trace metadata (counts, duration).\n                               Options: [--list-tracks]\n  inspect <trace_file>         Inspect detailed event parameters at a timestamp.\n                               Options: --track <name> --ts <ts_us>\n  query <trace_file>           Search and extract matching events.\n                               Options: [--track <name>] [--match <substr>]\n                                        [--t-start <us>] [--t-end <us>]\n                                        [--max-depth <n>] [--limit <n>]\n  concurrency <trace_file>     Visualize system load and concurrency.\n                               Options: [--buckets <n>]\n  aggregate <trace_file>       Aggregate event durations and counts.\n                               Options: [--group-by name|category]\n                                        [--sort duration|count]\n                                        [--min-count <n>]\n  diff <trace_1> <trace_2>     Compare two traces side-by-side.\n                               Options: [--group-by name|category]\n                                        [--sort dur-delta|count-delta]\n  histogram <trace_file>       Compute duration histogram buckets.\n                               Options: [--track <name>] [--match <substr>]\n                                        [--t-start <us>] [--t-end <us>]\n";

#[derive(Default)]
struct Args {
    command: String,
    file: String,
    file2: Option<String>,
    list_tracks: bool,
    track: Option<String>,
    matching: Option<String>,
    start: Option<i64>,
    end: Option<i64>,
    max_depth: Option<i32>,
    limit: Option<i32>,
    buckets: Option<i32>,
    min_count: Option<i32>,
    group_by: Option<String>,
    sort_by: Option<String>,
}

fn main() -> ExitCode {
    let _ = set_writer(&STDERR_WRITER);
    let raw: Vec<String> = std::env::args().collect();
    let program = raw.first().map_or("ztracing", String::as_str);
    let args = match parse(&raw[1..]) {
        Ok(args) => args,
        Err(error) => {
            if !error.is_empty() {
                eprintln!("{error}");
            }
            eprint!("{}", USAGE.replace("{program}", program));
            return ExitCode::FAILURE;
        }
    };
    let loaded = match load_file(Path::new(&args.file)) {
        Ok(loaded) => loaded,
        Err(error) => {
            eprintln!("{error}");
            return ExitCode::FAILURE;
        }
    };
    let result = match args.command.as_str() {
        "summary" => summary(&loaded, &args),
        "concurrency" => show_concurrency(&loaded, &args),
        "aggregate" => show_aggregate(&loaded.data, &args),
        "diff" => args
            .file2
            .as_ref()
            .ok_or_else(|| "Error: Missing second trace file argument for diff.".to_owned())
            .and_then(|file| load_file(Path::new(file)))
            .and_then(|target| show_diff(&loaded.data, &target.data, &args)),
        "histogram" => show_histogram(&loaded, &args),
        "inspect" => inspect(&loaded, &args),
        "query" => query(&loaded, &args),
        other => Err(format!(
            "Error: Subcommand '{other}' is not yet implemented."
        )),
    };
    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("{error}");
            ExitCode::FAILURE
        }
    }
}

fn parse(values: &[String]) -> Result<Args, String> {
    if values.is_empty() || values[0] == "-h" || values[0] == "--help" {
        return Err(String::new());
    }
    if values[0].starts_with('-') {
        return Err(format!("Error: Unknown global option '{}'", values[0]));
    }
    if values.len() < 2 {
        return Err("Error: Missing trace file argument.".to_owned());
    }
    if matches!(values[1].as_str(), "-h" | "--help") {
        return Err(String::new());
    }
    let mut output = Args {
        command: values[0].clone(),
        file: values[1].clone(),
        ..Args::default()
    };
    let mut index = 2;
    if output.command == "diff" {
        if index >= values.len() {
            return Err("Error: Missing second trace file argument for diff.".to_owned());
        }
        if matches!(values[index].as_str(), "-h" | "--help") {
            return Err(String::new());
        }
        output.file2 = Some(values[index].clone());
        index += 1;
    }
    while index < values.len() {
        let option = values[index].as_str();
        let mut next = || {
            index += 1;
            values
                .get(index)
                .cloned()
                .ok_or_else(|| format!("Error: Missing value for option '{option}'"))
        };
        match option {
            "--list-tracks" => output.list_tracks = true,
            "--track" => output.track = Some(next()?),
            "--match" => output.matching = Some(next()?),
            "--ts" | "--t-start" => output.start = Some(parse_number(option, &next()?)?),
            "--t-end" => output.end = Some(parse_number(option, &next()?)?),
            "--max-depth" => {
                output.max_depth = Some(parse_nonnegative(option, &next()?)?);
            }
            "--limit" => {
                output.limit = Some(parse_nonnegative(option, &next()?)?);
            }
            "--buckets" => {
                let value = parse_number(option, &next()?)?;
                if value <= 0 {
                    return Err(format!(
                        "Error: Invalid value for option '{option}': expected a positive integer."
                    ));
                }
                output.buckets = Some(value);
            }
            "--min-count" => {
                output.min_count = Some(parse_nonnegative(option, &next()?)?);
            }
            "--group-by" => output.group_by = Some(next()?),
            "--sort" => output.sort_by = Some(next()?),
            _ => {
                return Err(format!(
                    "Error: Unknown option '{option}' for subcommand '{}'",
                    output.command
                ));
            }
        }
        index += 1;
    }
    Ok(output)
}

fn parse_number<T>(option: &str, value: &str) -> Result<T, String>
where
    T: FromStr,
{
    value.parse().map_err(|_| {
        format!("Error: Invalid value for option '{option}': '{value}' is not a valid integer.")
    })
}

fn parse_nonnegative(option: &str, value: &str) -> Result<i32, String> {
    let parsed = parse_number(option, value)?;
    if parsed < 0 {
        return Err(format!(
            "Error: Invalid value for option '{option}': expected a non-negative integer."
        ));
    }
    Ok(parsed)
}

fn summary(loaded: &LoadedTrace, args: &Args) -> Result<(), String> {
    let mut table = Table::new();
    table.column("Metric", Align::Left, 25, true);
    table.column("Value", Align::Left, 15, true);
    table.row(["Event Count".into(), loaded.data.events.len().to_string()]);
    table.row(["Track Count".into(), loaded.tracks.len().to_string()]);
    table.row([
        "Min Timestamp (us)".into(),
        loaded.minimum_timestamp.to_string(),
    ]);
    table.row([
        "Max Timestamp (us)".into(),
        loaded.maximum_timestamp.to_string(),
    ]);
    table.row([
        "Duration (ms)".into(),
        format!(
            "{:.3}",
            (loaded.maximum_timestamp - loaded.minimum_timestamp) as f64 / 1000.0
        ),
    ]);
    print!("{}", table.render());
    if args.list_tracks {
        println!();
        let mut table = Table::new();
        for (h, a, w) in [
            ("Index", Align::Right, 5),
            ("Track Name", Align::Left, 20),
            ("Type", Align::Left, 10),
            ("PID", Align::Right, 8),
            ("TID", Align::Right, 8),
            ("Event Count", Align::Right, 12),
            ("Max Depth", Align::Right, 10),
        ] {
            table.column(h, a, w, true);
        }
        for (index, track) in loaded.tracks.iter().enumerate() {
            table.row([
                index.to_string(),
                text(&loaded.data, track.name),
                if track.kind == TrackType::Thread {
                    "THREAD"
                } else {
                    "COUNTER"
                }
                .into(),
                track.process_id.to_string(),
                track.thread_id.to_string(),
                track.event_indices.len().to_string(),
                track.max_depth.to_string(),
            ]);
        }
        print!("{}", table.render());
    }
    Ok(())
}

fn show_concurrency(loaded: &LoadedTrace, args: &Args) -> Result<(), String> {
    let count = args.buckets.unwrap_or(16) as usize;
    let buckets = concurrency::compute(
        &loaded.tracks,
        &loaded.data,
        loaded.minimum_timestamp,
        loaded.maximum_timestamp,
        count,
    );
    let digits = count.to_string().len();
    let threads = loaded
        .tracks
        .iter()
        .filter(|t| t.kind == TrackType::Thread)
        .count();
    let mut table = Table::new();
    for h in [
        "Bucket",
        "Time Range (s)",
        "Concurrency (Active Threads)",
        "Dominant Events",
    ] {
        table.column(h, Align::Left, 0, true);
    }
    for (i, b) in buckets.iter().enumerate() {
        let pct = if threads == 0 {
            0.0
        } else {
            b.average / threads as f64 * 100.0
        };
        let active = ((pct / 100.0 * 20.0) as i32).clamp(0, 20) as usize;
        let bar = format!(
            "[{}{}] {:3.0}%     ",
            "█".repeat(active),
            "░".repeat(20 - active),
            pct
        );
        let names = b
            .dominant_events
            .iter()
            .map(|r| text(&loaded.data, *r))
            .collect::<Vec<_>>()
            .join(", ");
        table.row([
            format!("[{i:0digits$}]"),
            format!(
                "{:.1} - {:.1}",
                (b.start - loaded.minimum_timestamp as f64) / 1e6,
                (b.end - loaded.minimum_timestamp as f64) / 1e6
            ),
            bar,
            names,
        ]);
    }
    print!("{}", table.render());
    Ok(())
}

fn show_aggregate(data: &TraceData, args: &Args) -> Result<(), String> {
    let group = args.group_by.as_deref().unwrap_or("name");
    let sort = args.sort_by.as_deref().unwrap_or("duration");
    if !matches!(group, "name" | "category") {
        return Err(format!(
            "Error: Invalid value for --group-by: '{group}'. Expected 'name' or 'category'."
        ));
    }
    if !matches!(sort, "duration" | "count") {
        return Err(format!(
            "Error: Invalid value for --sort: '{sort}'. Expected 'duration' or 'count'."
        ));
    }
    let entries = aggregate::compute(data, group.as_bytes(), sort.as_bytes());
    let minimum = args.min_count.unwrap_or(2);
    let mut skipped = 0;
    let mut table = Table::new();
    table.column(
        if group == "category" {
            "Event Category"
        } else {
            "Event Name"
        },
        Align::Left,
        30,
        true,
    );
    table.column("Total Duration (s)", Align::Right, 18, true);
    table.column("Event Count", Align::Right, 11, true);
    table.column("Average Duration (ms)", Align::Right, 20, true);
    for e in entries {
        if (e.count as i32) < minimum {
            skipped += 1;
            continue;
        }
        table.row([
            text(data, e.key),
            format!("{:.2}", e.total_duration / 1e6),
            e.count.to_string(),
            format!("{:.2}", e.total_duration / e.count as f64 / 1000.0),
        ]);
    }
    print!("{}", table.render());
    if skipped > 0 {
        if minimum == 2 {
            println!("\n* Skipped {skipped} single-instance events (count = 1).");
        } else {
            println!("\n* Skipped {skipped} events with count < {minimum}.");
        }
    }
    Ok(())
}

fn show_diff(base: &TraceData, target: &TraceData, args: &Args) -> Result<(), String> {
    let group = args.group_by.as_deref().unwrap_or("name");
    let sort = args.sort_by.as_deref().unwrap_or("dur-delta");
    if !matches!(group, "name" | "category") {
        return Err(format!(
            "Error: Invalid value for --group-by: '{group}'. Expected 'name' or 'category'."
        ));
    }
    if !matches!(sort, "dur-delta" | "count-delta") {
        return Err(format!(
            "Error: Invalid value for --sort: '{sort}'. Expected 'dur-delta' or 'count-delta'."
        ));
    }
    let mut table = Table::new();
    table.column(
        if group == "category" {
            "Event Category"
        } else {
            "Event Name"
        },
        Align::Left,
        30,
        true,
    );
    table.column("Baseline Dur (s)", Align::Right, 16, true);
    table.column("Target Dur (s)", Align::Right, 14, true);
    table.column("Delta Dur (s)", Align::Right, 14, true);
    table.column("Delta Count", Align::Right, 11, true);
    for e in diff::compute(base, target, group.as_bytes(), sort.as_bytes()) {
        table.row([
            String::from_utf8_lossy(&e.key).into_owned(),
            format!("{:.2}", e.baseline_duration / 1e6),
            format!("{:.2}", e.target_duration / 1e6),
            format!("{:+.2}", e.delta_duration / 1e6),
            format!("{:+}", e.delta_count),
        ]);
    }
    print!("{}", table.render());
    Ok(())
}

fn filter_indices(
    loaded: &LoadedTrace,
    args: &Args,
    histogram_mode: bool,
) -> Result<Vec<i64>, String> {
    let tracks: Vec<&Track> = if let Some(name) = &args.track {
        if histogram_mode {
            loaded
                .tracks
                .iter()
                .filter(|track| loaded.data.string(track.name) == name.as_bytes())
                .collect()
        } else {
            let Some(track) = loaded
                .tracks
                .iter()
                .find(|track| loaded.data.string(track.name) == name.as_bytes())
            else {
                return Err(format!("Error: Track '{name}' not found."));
            };
            vec![track]
        }
    } else {
        loaded.tracks.iter().collect()
    };
    let mut result = Vec::new();
    if histogram_mode && args.track.is_none() {
        for (i, e) in loaded.data.events.iter().enumerate() {
            if matches_event(&loaded.data, e, args, false) {
                result.push(i as i64)
            }
        }
    } else {
        for track in tracks {
            for (position, &index) in track.event_indices.iter().enumerate() {
                let e = &loaded.data.events[index];
                if matches_event(&loaded.data, e, args, !histogram_mode)
                    && args
                        .max_depth
                        .is_none_or(|m| histogram_mode || track.depths[position] as i32 <= m)
                {
                    result.push(index as i64)
                }
            }
        }
    }
    Ok(result)
}
fn matches_event(
    data: &TraceData,
    e: &ztracing::trace::data::PersistedEvent,
    args: &Args,
    overlap: bool,
) -> bool {
    if let Some(q) = &args.matching {
        let q = q.as_bytes();
        if !contains_ascii(data.string(e.name), q) && !contains_ascii(data.string(e.category), q) {
            return false;
        }
    }
    if let Some(s) = args.start {
        if if overlap {
            e.timestamp + e.duration < s
        } else {
            e.timestamp < s
        } {
            return false;
        }
    }
    if let Some(end) = args.end {
        if e.timestamp > end {
            return false;
        }
    }
    true
}
fn contains_ascii(haystack: &[u8], needle: &[u8]) -> bool {
    if needle.is_empty() {
        return true;
    }
    haystack
        .windows(needle.len())
        .any(|w| w.iter().zip(needle).all(|(a, b)| a.eq_ignore_ascii_case(b)))
}

fn show_histogram(loaded: &LoadedTrace, args: &Args) -> Result<(), String> {
    let selected = filter_indices(loaded, args, true)?;
    let h = histogram::compute(&selected, &loaded.data);
    let logarithmic = h
        .buckets
        .first()
        .zip(h.buckets.last())
        .is_some_and(|(a, b)| {
            (b.max_duration - b.min_duration) > (a.max_duration - a.min_duration) * 2
        });
    println!(
        "Scale: {}, Total Events: {}\n",
        if logarithmic { "logarithmic" } else { "linear" },
        selected.len()
    );
    let digits = h.buckets.len().to_string().len();
    let mut table = Table::new();
    table.column("Bucket", Align::Left, 0, true);
    table.column("Range (us)", Align::Left, 0, true);
    table.column("Count", Align::Right, 0, true);
    table.column("Distribution", Align::Left, 0, true);
    for (i, b) in h.buckets.iter().enumerate() {
        let pct = if selected.is_empty() {
            0.0
        } else {
            b.count as f64 / selected.len() as f64 * 100.0
        };
        let active = ((pct / 100.0 * 20.0) as i32).clamp(0, 20) as usize;
        table.row([
            format!("[{i:0digits$}]"),
            format!("{} - {}", b.min_duration, b.max_duration),
            b.count.to_string(),
            format!(
                "[{}{}] {:3.0}%",
                "█".repeat(active),
                "░".repeat(20 - active),
                pct
            ),
        ]);
    }
    print!("{}", table.render());
    Ok(())
}

fn query(loaded: &LoadedTrace, args: &Args) -> Result<(), String> {
    let indices = filter_indices(loaded, args, false)?;
    let mut matches = Vec::new();
    for index in indices {
        let index = index as usize;
        for track in &loaded.tracks {
            if let Some(pos) = track.event_indices.iter().position(|&i| i == index) {
                matches.push((index, track, pos));
                break;
            }
        }
    }
    matches.sort_by_key(|m| loaded.data.events[m.0].timestamp);
    let limit = args.limit.unwrap_or(matches.len() as i32).max(0) as usize;
    let mut table = Table::new();
    table.column("Event Name", Align::Left, 30, true);
    table.column("Track", Align::Left, 20, true);
    table.column("Start Time (us)", Align::Right, 17, true);
    table.column("Duration (us)", Align::Right, 15, true);
    table.column("Depth", Align::Right, 5, true);
    for (index, track, pos) in matches.into_iter().take(limit) {
        let e = &loaded.data.events[index];
        table.row([
            text(&loaded.data, e.name),
            text(&loaded.data, track.name),
            e.timestamp.to_string(),
            e.duration.to_string(),
            track.depths[pos].to_string(),
        ]);
    }
    print!("{}", table.render());
    Ok(())
}

fn inspect(loaded: &LoadedTrace, args: &Args) -> Result<(), String> {
    let Some(name) = &args.track else {
        return Err(
            "Error: Missing required option '--track <name>' for inspect subcommand.".into(),
        );
    };
    let Some(timestamp) = args.start else {
        return Err("Error: Missing required option '--ts <ts_us>' for inspect subcommand.".into());
    };
    let Some(track) = loaded
        .tracks
        .iter()
        .find(|t| loaded.data.string(t.name) == name.as_bytes())
    else {
        return Err(format!("Error: Track '{name}' not found."));
    };
    let positions: Vec<_> = track
        .event_indices
        .iter()
        .enumerate()
        .filter(|(_, i)| loaded.data.events[**i].timestamp == timestamp)
        .map(|(p, _)| p)
        .collect();
    for (iteration, &position) in positions.iter().enumerate() {
        if iteration > 0 {
            println!("\n---\n")
        }
        let e = &loaded.data.events[track.event_indices[position]];
        let mut table = Table::new();
        table.column("Property", Align::Left, 20, true);
        table.column("Value", Align::Left, 30, true);
        table.row(["Name".into(), text(&loaded.data, e.name)]);
        table.row(["Track".into(), name.clone()]);
        table.row(["Timestamp (us)".into(), e.timestamp.to_string()]);
        table.row(["Duration (us)".into(), e.duration.to_string()]);
        if track.kind == TrackType::Thread {
            table.row([
                "Self Time (us)".into(),
                track.self_durations[position].to_string(),
            ]);
            table.row(["Depth".into(), track.depths[position].to_string()]);
            if track.depths[position] > 0 {
                if let Some(parent_pos) = (0..position)
                    .rev()
                    .find(|&p| track.depths[p] == track.depths[position] - 1)
                {
                    let parent = &loaded.data.events[track.event_indices[parent_pos]];
                    table.row(["Parent Name".into(), text(&loaded.data, parent.name)]);
                    table.row(["Parent TS (us)".into(), parent.timestamp.to_string()]);
                }
            }
        }
        for arg in loaded.data.event_args(e) {
            table.row([
                format!("Arg: {}", text(&loaded.data, arg.key)),
                if arg.value.0 != 0 {
                    text(&loaded.data, arg.value)
                } else {
                    format!("{:.6}", arg.number)
                },
            ]);
        }
        print!("{}", table.render());
        if track.kind == TrackType::Thread {
            let depth = track.depths[position];
            let children: Vec<_> = ((position + 1)..track.event_indices.len())
                .take_while(|&p| track.depths[p] > depth)
                .filter(|&p| track.depths[p] == depth + 1)
                .collect();
            if !children.is_empty() {
                println!("\nChildren:");
                let mut table = Table::new();
                table.column("Child Name", Align::Left, 20, true);
                table.column("Timestamp (us)", Align::Right, 15, true);
                table.column("Duration (us)", Align::Right, 15, true);
                for p in children {
                    let child = &loaded.data.events[track.event_indices[p]];
                    table.row([
                        text(&loaded.data, child.name),
                        child.timestamp.to_string(),
                        child.duration.to_string(),
                    ]);
                }
                print!("{}", table.render());
            }
        }
    }
    Ok(())
}
fn text(data: &TraceData, reference: ztracing::string_interner::StringId) -> String {
    String::from_utf8_lossy(data.string(reference)).into_owned()
}

#[cfg(test)]
mod tests {
    use std::ffi::{CString, c_char, c_int, c_void};
    use std::fs;
    use std::path::PathBuf;
    use std::process::Command;
    use std::sync::atomic::{AtomicUsize, Ordering};

    static NEXT_FILE: AtomicUsize = AtomicUsize::new(0);
    const STANDARD: &str = r#"[
      {"name": "task_A", "cat": "test", "ph": "X", "ts": 1000, "dur": 500, "pid": 1, "tid": 1},
      {"name": "task_B", "cat": "test", "ph": "B", "ts": 2000, "pid": 1, "tid": 1},
      {"name": "task_B", "cat": "test", "ph": "E", "ts": 3000, "pid": 1, "tid": 1}
    ]"#;

    #[link(name = "z")]
    unsafe extern "C" {
        fn gzopen(path: *const c_char, mode: *const c_char) -> *mut c_void;
        fn gzwrite(file: *mut c_void, buffer: *const c_void, length: u32) -> c_int;
        fn gzclose(file: *mut c_void) -> c_int;
    }

    fn temporary(name: &str, content: &str) -> PathBuf {
        let dir = std::env::var_os("TEST_TMPDIR")
            .map(PathBuf::from)
            .unwrap_or_else(std::env::temp_dir);
        let path = dir.join(format!(
            "{}_{name}",
            NEXT_FILE.fetch_add(1, Ordering::Relaxed)
        ));
        fs::write(&path, content).unwrap();
        path
    }
    fn gzip(name: &str, content: &str) -> PathBuf {
        let path = temporary(name, "");
        let cpath = CString::new(path.as_os_str().as_encoded_bytes()).unwrap();
        unsafe {
            let file = gzopen(cpath.as_ptr(), c"wb".as_ptr());
            assert!(!file.is_null());
            assert_eq!(
                gzwrite(file, content.as_ptr().cast(), content.len() as u32),
                content.len() as c_int
            );
            assert_eq!(gzclose(file), 0);
        }
        path
    }

    struct CommandOutput {
        status: i32,
        stdout: String,
        stderr: String,
    }

    fn run(args: &[&str]) -> CommandOutput {
        let output = Command::new("src/ztracing").args(args).output().unwrap();
        CommandOutput {
            status: output.status.code().unwrap_or(-1),
            stdout: String::from_utf8(output.stdout).unwrap(),
            stderr: String::from_utf8(output.stderr).unwrap(),
        }
    }

    fn golden(args: &[&str], name: &str, status: i32) {
        let output = run(args);
        assert_eq!(output.status, status, "args: {args:?}");
        let expected = fs::read_to_string(format!("src/testdata/cli/{name}"))
            .unwrap()
            .lines()
            .skip(1)
            .map(|line| format!("{line}\n"))
            .collect::<String>();
        if status == 0 {
            assert_eq!(output.stdout, expected, "args: {args:?}");
            assert_eq!(output.stderr, "", "args: {args:?}");
        } else {
            assert_eq!(output.stdout, "", "args: {args:?}");
            assert_eq!(output.stderr, expected, "args: {args:?}");
        }
    }

    #[test]
    fn no_arguments_matches_golden_help() {
        golden(&[], "help.golden", 1)
    }
    #[test]
    fn help_flag_matches_golden_help() {
        golden(&["--help"], "help.golden", 1)
    }

    #[test]
    fn help_in_a_required_file_position_matches_golden_help() {
        golden(&["summary", "--help"], "help.golden", 1);
        let p = temporary("diff_help.json", "[]");
        golden(&["diff", p.to_str().unwrap(), "--help"], "help.golden", 1);
    }
    #[test]
    fn summary_output_matches_golden() {
        let p = temporary("summary.json", STANDARD);
        golden(&["summary", p.to_str().unwrap()], "summary.golden", 0)
    }
    #[test]
    fn summary_gzip_output_matches_golden() {
        let p = gzip("summary.json.gz", STANDARD);
        golden(&["summary", p.to_str().unwrap()], "summary.golden", 0)
    }
    #[test]
    fn unknown_subcommand_matches_golden_error() {
        let p = temporary("empty.json", "[]");
        golden(
            &["unknown_subcommand", p.to_str().unwrap()],
            "error_unknown_subcommand.golden",
            1,
        )
    }
    #[test]
    fn non_existent_trace_file_matches_golden_error() {
        golden(
            &["summary", "non_existent_file.json"],
            "error_non_existent_file.golden",
            1,
        )
    }
    #[test]
    fn corrupted_gzip_file_errors() {
        let path = temporary("corrupted.json.gz", "");
        fs::write(
            &path,
            [0x1f, 0x8b, 0x08, 0, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff],
        )
        .unwrap();
        let output = run(&["summary", path.to_str().unwrap()]);
        assert_eq!(output.status, 1);
        assert!(output.stdout.is_empty());
        assert!(output.stderr.contains("Error: Gzip decompression failed"));
    }
    #[test]
    fn summary_list_tracks_output_matches_golden() {
        let p = temporary("summary_list_tracks.json", STANDARD);
        golden(
            &["summary", p.to_str().unwrap(), "--list-tracks"],
            "summary_list_tracks.golden",
            0,
        )
    }
    #[test]
    fn concurrency_output_matches_golden() {
        let p = temporary("concurrency.json", STANDARD);
        golden(
            &["concurrency", p.to_str().unwrap()],
            "concurrency.golden",
            0,
        )
    }
    #[test]
    fn histogram_output_matches_golden() {
        let p = temporary("histogram.json", STANDARD);
        golden(&["histogram", p.to_str().unwrap()], "histogram.golden", 0)
    }
    #[test]
    fn histogram_filtered_output_matches_golden() {
        let p = temporary(
            "histogram_filtered.json",
            r#"[
    {"name":"render_frame","cat":"gpu","ph":"X","ts":1000,"dur":500,"pid":1,"tid":1},
    {"name":"network_request","cat":"net","ph":"X","ts":1500,"dur":8000,"pid":1,"tid":2},
    {"name":"parse_json","cat":"cpu","ph":"X","ts":2000,"dur":300,"pid":1,"tid":2},
    {"name":"render_frame","cat":"gpu","ph":"X","ts":3000,"dur":600,"pid":1,"tid":1}]"#,
        );
        let s = p.to_str().unwrap();
        golden(
            &["histogram", s, "--match", "render"],
            "histogram_match_render.golden",
            0,
        );
        golden(
            &["histogram", s, "--t-start", "1200", "--t-end", "2500"],
            "histogram_time_filtered.golden",
            0,
        )
    }

    #[test]
    fn histogram_missing_track_is_an_empty_success() {
        let p = temporary("histogram_missing_track.json", STANDARD);
        let output = run(&["histogram", p.to_str().unwrap(), "--track", "missing"]);
        assert_eq!(output.status, 0);
        assert!(output.stderr.is_empty());
        assert!(
            output
                .stdout
                .starts_with("Scale: linear, Total Events: 0\n\n")
        );
    }

    #[test]
    fn histogram_combines_duplicate_track_names() {
        let p = temporary(
            "histogram_duplicate_tracks.json",
            r#"[
    {"name":"thread_name","ph":"M","pid":1,"tid":1,"args":{"name":"worker"}},
    {"name":"thread_name","ph":"M","pid":2,"tid":2,"args":{"name":"worker"}},
    {"name":"first","ph":"X","ts":1000,"dur":100,"pid":1,"tid":1},
    {"name":"second","ph":"X","ts":2000,"dur":200,"pid":2,"tid":2}]"#,
        );
        let output = run(&["histogram", p.to_str().unwrap(), "--track", "worker"]);
        assert_eq!(output.status, 0);
        assert!(output.stderr.is_empty());
        assert!(output.stdout.contains("Total Events: 2"));
    }

    #[test]
    fn histogram_ignores_query_only_max_depth() {
        let p = temporary(
            "histogram_max_depth.json",
            r#"[
    {"name":"parent","ph":"X","ts":1000,"dur":1000,"pid":1,"tid":1},
    {"name":"child","ph":"X","ts":1100,"dur":100,"pid":1,"tid":1}]"#,
        );
        let output = run(&[
            "histogram",
            p.to_str().unwrap(),
            "--track",
            "",
            "--max-depth",
            "0",
        ]);
        assert_eq!(output.status, 0);
        assert!(output.stderr.is_empty());
        assert!(output.stdout.contains("Total Events: 2"));
    }

    #[test]
    fn malformed_and_out_of_range_numbers_are_rejected() {
        let cases = [
            ["query", "unused.json", "--limit", "12items"],
            ["query", "unused.json", "--limit", "-1"],
            ["query", "unused.json", "--max-depth", "-1"],
            ["concurrency", "unused.json", "--buckets", "0"],
            [
                "histogram",
                "unused.json",
                "--t-start",
                "999999999999999999999999",
            ],
        ];
        for args in cases {
            let output = run(&args);
            assert_eq!(output.status, 1, "args: {args:?}");
            assert!(output.stdout.is_empty(), "args: {args:?}");
            assert!(
                output.stderr.starts_with("Error: Invalid value for option"),
                "args: {args:?}, stderr: {}",
                output.stderr
            );
            assert!(output.stderr.contains("Usage:"), "args: {args:?}");
        }
    }

    #[test]
    fn inspect_output_matches_golden() {
        let p = temporary("inspect.json", STANDARD);
        golden(
            &[
                "inspect",
                p.to_str().unwrap(),
                "--track",
                "",
                "--ts",
                "1000",
            ],
            "inspect.golden",
            0,
        )
    }
    #[test]
    fn inspect_nested_hierarchy_matches_golden() {
        let p = temporary(
            "nested.json",
            r#"[
    {"name":"parent_task","cat":"test","ph":"X","ts":1000,"dur":1000,"pid":1,"tid":1},
    {"name":"child_task_A","cat":"test","ph":"X","ts":1100,"dur":300,"pid":1,"tid":1},
    {"name":"child_task_B","cat":"test","ph":"X","ts":1500,"dur":400,"pid":1,"tid":1}]"#,
        );
        let s = p.to_str().unwrap();
        golden(
            &["inspect", s, "--track", "", "--ts", "1000"],
            "inspect_nested_parent.golden",
            0,
        );
        golden(
            &["inspect", s, "--track", "", "--ts", "1100"],
            "inspect_nested_child.golden",
            0,
        )
    }
    #[test]
    fn query_output_matches_golden() {
        let p = temporary("query.json", STANDARD);
        golden(&["query", p.to_str().unwrap()], "query.golden", 0)
    }
    fn filtered_trace() -> PathBuf {
        temporary(
            "filtered.json",
            r#"[
    {"name":"thread_name","ph":"M","pid":1,"tid":1,"args":{"name":"main_thread"}},
    {"name":"thread_name","ph":"M","pid":1,"tid":2,"args":{"name":"worker_thread"}},
    {"name":"parent_task","cat":"cpu","ph":"X","ts":1000,"dur":1000,"pid":1,"tid":1},
    {"name":"child_task_A","cat":"cpu","ph":"X","ts":1100,"dur":300,"pid":1,"tid":1},
    {"name":"network_request","cat":"net","ph":"X","ts":1500,"dur":800,"pid":1,"tid":2},
    {"name":"render_frame","cat":"gpu","ph":"X","ts":2000,"dur":500,"pid":1,"tid":1}]"#,
        )
    }
    #[test]
    fn query_filtered_output_matches_golden() {
        let p = filtered_trace();
        let s = p.to_str().unwrap();
        golden(
            &["query", s, "--match", "task", "--max-depth", "0"],
            "query_match_depth.golden",
            0,
        );
        golden(
            &["query", s, "--t-start", "1200", "--limit", "1"],
            "query_time_limit.golden",
            0,
        );
        golden(
            &["query", s, "--track", "worker_thread"],
            "query_track_filter.golden",
            0,
        )
    }

    #[test]
    fn query_equal_timestamps_have_deterministic_track_order() {
        let p = temporary(
            "query_equal_timestamps.json",
            r#"[
    {"name":"second_track_event","ph":"X","ts":1000,"dur":100,"pid":1,"tid":2},
    {"name":"first_track_event","ph":"X","ts":1000,"dur":100,"pid":1,"tid":1}]"#,
        );
        let output = run(&["query", p.to_str().unwrap()]);
        assert_eq!(output.status, 0);
        assert!(output.stderr.is_empty());
        let first = output.stdout.find("first_track_event").unwrap();
        let second = output.stdout.find("second_track_event").unwrap();
        assert!(first < second);
    }

    fn aggregate_trace() -> PathBuf {
        temporary(
            "aggregate.json",
            r#"[
    {"name":"task_A","cat":"cpu","ph":"X","ts":1000,"dur":500,"pid":1,"tid":1},
    {"name":"task_B","cat":"gpu","ph":"X","ts":1500,"dur":1000,"pid":1,"tid":1},
    {"name":"task_A","cat":"cpu","ph":"X","ts":2000,"dur":300,"pid":1,"tid":1}]"#,
        )
    }
    #[test]
    fn aggregate_output_matches_golden() {
        let p = aggregate_trace();
        let s = p.to_str().unwrap();
        golden(&["aggregate", s], "aggregate_default.golden", 0);
        golden(
            &["aggregate", s, "--group-by", "category"],
            "aggregate_by_category.golden",
            0,
        );
        golden(
            &["aggregate", s, "--sort", "count"],
            "aggregate_sort_by_count.golden",
            0,
        )
    }
    #[test]
    fn aggregate_min_count_matches_golden() {
        let p = aggregate_trace();
        let s = p.to_str().unwrap();
        golden(
            &["aggregate", s, "--min-count", "1"],
            "aggregate_min_count_1.golden",
            0,
        );
        golden(
            &["aggregate", s, "--min-count", "5"],
            "aggregate_min_count_5.golden",
            0,
        )
    }
    #[test]
    fn diff_output_matches_golden() {
        let b = temporary(
            "diff_base.json",
            r#"[{"name":"task_A","cat":"cpu","ph":"X","ts":1000,"dur":500,"pid":1,"tid":1},{"name":"task_B","cat":"gpu","ph":"X","ts":1500,"dur":1000,"pid":1,"tid":1}]"#,
        );
        let t = temporary(
            "diff_target.json",
            r#"[{"name":"task_A","cat":"cpu","ph":"X","ts":1000,"dur":800,"pid":1,"tid":1},{"name":"task_B","cat":"gpu","ph":"X","ts":1500,"dur":900,"pid":1,"tid":1},{"name":"task_C","cat":"cpu","ph":"X","ts":2500,"dur":300,"pid":1,"tid":1}]"#,
        );
        let (b, t) = (b.to_str().unwrap(), t.to_str().unwrap());
        golden(&["diff", b, t], "diff_default.golden", 0);
        golden(
            &["diff", b, t, "--group-by", "category"],
            "diff_by_category.golden",
            0,
        );
        golden(
            &["diff", b, t, "--sort", "count-delta"],
            "diff_sort_by_count_delta.golden",
            0,
        )
    }
}
#[global_allocator]
static ALLOCATOR: CountingAllocator = CountingAllocator;
