//! Golden image tests for the headless viewer.

use crate::headless::HeadlessApp;
use crate::imgui::{KEY_ENTER, KEY_F, KEY_SLASH, MOD_CTRL, MOD_SHIFT};
use std::sync::Mutex;
use std::time::{Duration, Instant};

static IMGUI: Mutex<()> = Mutex::new(());
const SHORT_WAIT: Duration = Duration::from_secs(5);

#[derive(Debug, Eq, PartialEq)]
struct Image {
    width: usize,
    height: usize,
    rgba: Vec<u8>,
}

fn decode_bmp(bytes: &[u8]) -> Result<Image, String> {
    if bytes.len() < 54 || &bytes[..2] != b"BM" {
        return Err("invalid BMP header".to_owned());
    }
    let read_u16 = |offset| {
        u16::from_le_bytes(
            bytes[offset..offset + 2]
                .try_into()
                .expect("validated BMP header length"),
        )
    };
    let read_u32 = |offset| {
        u32::from_le_bytes(
            bytes[offset..offset + 4]
                .try_into()
                .expect("validated BMP header length"),
        )
    };
    let width = i32::from_le_bytes(bytes[18..22].try_into().unwrap());
    let height = i32::from_le_bytes(bytes[22..26].try_into().unwrap());
    if width <= 0 || height <= 0 {
        return Err("BMP dimensions must be positive".to_owned());
    }
    if read_u16(26) != 1 || read_u16(28) != 24 || read_u32(30) != 0 {
        return Err("only uncompressed 24-bit BMP images are supported".to_owned());
    }
    let width = width as usize;
    let height = height as usize;
    let pixel_offset = read_u32(10) as usize;
    let row_bytes = width
        .checked_mul(3)
        .ok_or_else(|| "BMP row size overflow".to_owned())?;
    let stride = row_bytes
        .checked_add(3)
        .map(|value| value & !3)
        .ok_or_else(|| "BMP row size overflow".to_owned())?;
    let pixel_bytes = stride
        .checked_mul(height)
        .ok_or_else(|| "BMP pixel size overflow".to_owned())?;
    let end = pixel_offset
        .checked_add(pixel_bytes)
        .ok_or_else(|| "BMP file size overflow".to_owned())?;
    if pixel_offset < 54 || end > bytes.len() {
        return Err("truncated BMP pixel data".to_owned());
    }
    let rgba_len = width
        .checked_mul(height)
        .and_then(|pixels| pixels.checked_mul(4))
        .ok_or_else(|| "BMP RGBA size overflow".to_owned())?;
    let mut rgba = vec![255; rgba_len];
    for y in 0..height {
        for x in 0..width {
            let source = pixel_offset + y * stride + x * 3;
            let target = (y * width + x) * 4;
            rgba[target] = bytes[source + 2];
            rgba[target + 1] = bytes[source + 1];
            rgba[target + 2] = bytes[source];
        }
    }
    Ok(Image {
        width,
        height,
        rgba,
    })
}

fn load_bmp(path: &str) -> Result<Image, String> {
    let bytes = std::fs::read(path).map_err(|error| format!("{path}: {error}"))?;
    decode_bmp(&bytes).map_err(|error| format!("{path}: {error}"))
}

fn encode_bmp(image: &Image) -> Result<Vec<u8>, String> {
    let expected_len = image
        .width
        .checked_mul(image.height)
        .and_then(|pixels| pixels.checked_mul(4))
        .ok_or_else(|| "RGBA image size overflow".to_owned())?;
    if image.rgba.len() != expected_len {
        return Err("RGBA buffer length does not match image dimensions".to_owned());
    }
    let width = i32::try_from(image.width).map_err(|_| "BMP width is too large".to_owned())?;
    let height = i32::try_from(image.height).map_err(|_| "BMP height is too large".to_owned())?;
    let width = width as usize;
    let height = height as usize;
    let stride = width
        .checked_mul(3)
        .and_then(|row_bytes| row_bytes.checked_add(3))
        .map(|padded| padded & !3)
        .ok_or_else(|| "BMP row size overflow".to_owned())?;
    let size = stride
        .checked_mul(height)
        .and_then(|pixel_bytes| pixel_bytes.checked_add(54))
        .ok_or_else(|| "BMP file size overflow".to_owned())?;
    let file_size = u32::try_from(size).map_err(|_| "BMP file is too large".to_owned())?;
    let mut bytes = vec![0u8; size];
    bytes[0..2].copy_from_slice(b"BM");
    bytes[2..6].copy_from_slice(&file_size.to_le_bytes());
    bytes[10..14].copy_from_slice(&54u32.to_le_bytes());
    bytes[14..18].copy_from_slice(&40u32.to_le_bytes());
    bytes[18..22].copy_from_slice(&(width as i32).to_le_bytes());
    bytes[22..26].copy_from_slice(&(height as i32).to_le_bytes());
    bytes[26..28].copy_from_slice(&1u16.to_le_bytes());
    bytes[28..30].copy_from_slice(&24u16.to_le_bytes());
    for y in 0..height {
        for x in 0..width {
            let source = (y * width + x) * 4;
            let target = 54 + y * stride + x * 3;
            bytes[target] = image.rgba[source + 2];
            bytes[target + 1] = image.rgba[source + 1];
            bytes[target + 2] = image.rgba[source]
        }
    }
    Ok(bytes)
}

fn save_bmp(path: &str, image: &Image) -> Result<(), String> {
    let bytes = encode_bmp(image)?;
    std::fs::write(path, bytes).map_err(|error| format!("{path}: {error}"))
}

fn difference(left: &[u8], right: &[u8]) -> f64 {
    assert_eq!(
        left.len(),
        right.len(),
        "pixel buffers must have identical lengths"
    );
    let changed = left
        .chunks_exact(4)
        .zip(right.chunks_exact(4))
        .filter(|(a, b)| {
            (i16::from(a[0]) - i16::from(b[0])).abs() > 2
                || (i16::from(a[1]) - i16::from(b[1])).abs() > 2
                || (i16::from(a[2]) - i16::from(b[2])).abs() > 2
        })
        .count();
    changed as f64 / (left.len() / 4) as f64 * 100.0
}

fn wait_until(
    headless: &mut HeadlessApp,
    timeout: Duration,
    description: &str,
    mut complete: impl FnMut(&HeadlessApp) -> bool,
) {
    let deadline = Instant::now() + timeout;
    while !complete(headless) {
        assert!(
            Instant::now() < deadline,
            "timed out waiting for {description}"
        );
        headless.update();
        std::thread::sleep(Duration::from_millis(1));
    }
}

fn load_trace_with_session(headless: &mut HeadlessApp, session_id: i32, json: &str) {
    headless
        .runtime
        .app
        .begin_session(session_id, "test_trace.json", json.len());
    headless
        .runtime
        .app
        .handle_file_chunk(session_id, json.as_bytes().to_vec(), json.len(), true);
    wait_until(headless, SHORT_WAIT, "trace loading", |headless| {
        !headless.runtime.app.loading.active
    });
    headless.update();
    headless.update();
    headless.update();
}

fn load_trace(headless: &mut HeadlessApp, json: &str) {
    load_trace_with_session(headless, 1, json)
}

fn finish_pending_search(headless: &mut HeadlessApp) {
    // Input helpers already rendered the edit frame. This frame either submits
    // that pending search or polls a search submitted by a multi-frame click.
    headless.update();
    wait_until(headless, SHORT_WAIT, "search", |headless| {
        !headless.runtime.app.search_active()
    });
}

fn assert_golden(headless: &HeadlessApp, name: &str) {
    const TOLERANCE: f64 = 0.2;
    let (width, height) = headless.dimensions();
    let actual = Image {
        width,
        height,
        rgba: headless.read_rgba(),
    };
    let path = format!("src/testdata/{name}.bmp");
    let expected = load_bmp(&path);
    let mismatch = expected.as_ref().map_or(true, |expected| {
        expected.width != actual.width || expected.height != actual.height
    });
    let diff = expected
        .as_ref()
        .ok()
        .and_then(|expected| (!mismatch).then(|| difference(&actual.rgba, &expected.rgba)));
    if mismatch || diff.is_some_and(|diff| diff > TOLERANCE) {
        let directory =
            std::env::var("TEST_UNDECLARED_OUTPUTS_DIR").unwrap_or_else(|_| "/tmp".to_owned());
        let failed = format!("{directory}/{name}_failed.bmp");
        save_bmp(&failed, &actual).unwrap_or_else(|error| panic!("{error}"));
        match expected {
            Err(error) => panic!("{error}; captured frame saved to {failed}"),
            Ok(expected) if mismatch => panic!(
                "{name} dimensions differ: actual={}x{}, expected={}x{}; captured frame saved to {failed}",
                actual.width, actual.height, expected.width, expected.height
            ),
            Ok(_) => {}
        }
    }
    let diff = diff.expect("matching dimensions produce an image difference");
    assert!(
        diff <= TOLERANCE,
        "{name} difference: {diff:.3}% (limit {TOLERANCE:.3}%)"
    );
}

#[test]
fn bmp_round_trip_preserves_non_default_dimensions() {
    let image = Image {
        width: 2,
        height: 3,
        rgba: vec![
            1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255, 10, 11, 12, 255, 13, 14, 15, 255, 16, 17, 18,
            255,
        ],
    };
    assert_eq!(decode_bmp(&encode_bmp(&image).unwrap()).unwrap(), image);
}

#[test]
fn bmp_decoder_rejects_truncated_pixel_data() {
    let image = Image {
        width: 1,
        height: 1,
        rgba: vec![1, 2, 3, 255],
    };
    let mut bytes = encode_bmp(&image).unwrap();
    bytes.pop();
    assert_eq!(
        decode_bmp(&bytes).err().as_deref(),
        Some("truncated BMP pixel data")
    );
}

#[test]
fn welcome_screen_golden() {
    let _guard = IMGUI.lock().unwrap_or_else(|error| error.into_inner());
    let mut headless = HeadlessApp::create(800, 600).unwrap();
    headless.update();
    headless.update();
    assert_golden(&headless, "welcome_screen_golden");
}

#[test]
fn error_screen_golden() {
    let _guard = IMGUI.lock().unwrap_or_else(|error| error.into_inner());
    let mut headless = HeadlessApp::create(800, 600).unwrap();
    headless
        .runtime
        .app
        .report_error("Trace JSON is incomplete or malformed.");
    headless.update();
    headless.update();
    assert_golden(&headless, "error_screen_golden");
}

#[test]
fn vertical_minimap_golden() {
    let _guard = IMGUI.lock().unwrap_or_else(|error| error.into_inner());
    let mut headless = HeadlessApp::create(800, 600).unwrap();
    let mut trace = String::from("[");
    for index in 1..=40 {
        if index > 1 {
            trace.push(',')
        }
        trace.push_str(&format!(r#"{{"name":"thread_name","ph":"M","pid":1,"tid":{index},"args":{{"name":"Thread_{index}"}}}},{{"name":"Event_{index}","cat":"test","ph":"X","pid":1,"tid":{index},"ts":1000,"dur":2000}}"#))
    }
    trace.push(']');
    load_trace(&mut headless, &trace);
    headless.update();
    assert_golden(&headless, "vertical_minimap_golden")
}

#[test]
fn timeline_selection_golden() {
    let _guard = IMGUI.lock().unwrap_or_else(|error| error.into_inner());
    let mut headless = HeadlessApp::create(800, 600).unwrap();
    let trace = r#"[{"name":"process_name","ph":"M","pid":1,"args":{"name":"TestProcess"}},{"name":"thread_name","ph":"M","pid":1,"tid":1,"args":{"name":"MainThread"}},{"name":"EventA","cat":"ui","ph":"B","pid":1,"tid":1,"ts":1000},{"name":"EventSub","cat":"render","ph":"B","pid":1,"tid":1,"ts":1200},{"name":"EventSub","cat":"render","ph":"E","pid":1,"tid":1,"ts":1600},{"name":"EventA","cat":"ui","ph":"E","pid":1,"tid":1,"ts":2500},{"name":"EventB","cat":"network","ph":"B","pid":1,"tid":1,"ts":3000},{"name":"EventB","cat":"network","ph":"E","pid":1,"tid":1,"ts":4500}]"#;
    load_trace(&mut headless, trace);
    headless.drag((250.0, 28.0), (450.0, 28.0));
    headless.update();
    assert_golden(&headless, "timeline_selection_golden")
}

#[test]
fn light_theme_timeline_golden() {
    let _guard = IMGUI.lock().unwrap_or_else(|error| error.into_inner());
    let mut headless = HeadlessApp::create(800, 600).unwrap();
    let trace = r#"[{"name":"process_name","ph":"M","pid":1,"args":{"name":"TestProcess"}},{"name":"thread_name","ph":"M","pid":1,"tid":1,"args":{"name":"MainThread"}},{"name":"EventA","cat":"ui","ph":"B","pid":1,"tid":1,"ts":1000},{"name":"EventSub","cat":"render","ph":"B","pid":1,"tid":1,"ts":1200},{"name":"EventSub","cat":"render","ph":"E","pid":1,"tid":1,"ts":1600},{"name":"EventA","cat":"ui","ph":"E","pid":1,"tid":1,"ts":2500},{"name":"EventB","cat":"network","ph":"B","pid":1,"tid":1,"ts":3000},{"name":"EventB","cat":"network","ph":"E","pid":1,"tid":1,"ts":4500}]"#;
    load_trace(&mut headless, trace);
    headless.runtime.on_theme_changed(false);
    headless.update();
    assert_golden(&headless, "light_theme_timeline_golden")
}

#[test]
fn embedded_nul_event_names_use_their_full_explicit_length() {
    let _guard = IMGUI.lock().unwrap_or_else(|error| error.into_inner());
    let render = |name: &str| {
        let mut headless = HeadlessApp::create(800, 600).unwrap();
        let trace = format!(
            r#"[{{"name":"process_name","ph":"M","pid":1,"args":{{"name":"Process"}}}},{{"name":"thread_name","ph":"M","pid":1,"tid":1,"args":{{"name":"Thread"}}}},{{"name":"{name}","ph":"X","pid":1,"tid":1,"ts":1000,"dur":5000}}]"#
        );
        load_trace(&mut headless, &trace);
        headless.read_rgba()
    };

    let truncated = render("Before");
    let explicit_length = render(r"Before\u0000After");
    assert!(
        difference(&truncated, &explicit_length) > 0.0,
        "text after an embedded NUL was not rendered"
    );
}

#[test]
fn search_highlights_golden() {
    let _guard = IMGUI.lock().unwrap_or_else(|error| error.into_inner());
    let mut headless = HeadlessApp::create(800, 600).unwrap();
    let mut trace =
        String::from(r#"[{"name":"process_name","ph":"M","pid":1,"args":{"name":"TestProcess"}}"#);
    for tid in 1..=20 {
        trace.push_str(&format!(
            r#",{{"name":"thread_name","ph":"M","pid":1,"tid":{tid},"args":{{"name":"Thread_{tid}"}}}},{{"name":"OtherEvent","cat":"ui","ph":"B","pid":1,"tid":{tid},"ts":1200}},{{"name":"OtherEvent","cat":"ui","ph":"E","pid":1,"tid":{tid},"ts":2200}}"#
        ));
    }
    trace.push_str(
        r#",{"name":"SearchTarget","cat":"ui","ph":"B","pid":1,"tid":1,"ts":1000},{"name":"SearchTarget","cat":"ui","ph":"E","pid":1,"tid":1,"ts":2000},{"name":"SearchTarget","cat":"ui","ph":"B","pid":1,"tid":2,"ts":3000},{"name":"SearchTarget","cat":"ui","ph":"E","pid":1,"tid":2,"ts":4500},{"name":"SearchTarget","cat":"ui","ph":"B","pid":1,"tid":3,"ts":10000},{"name":"SearchTarget","cat":"ui","ph":"E","pid":1,"tid":3,"ts":11000},{"name":"SearchTarget","cat":"ui","ph":"B","pid":1,"tid":18,"ts":2000},{"name":"SearchTarget","cat":"ui","ph":"E","pid":1,"tid":18,"ts":3200},{"name":"SearchTarget","cat":"ui","ph":"B","pid":1,"tid":19,"ts":10000},{"name":"SearchTarget","cat":"ui","ph":"E","pid":1,"tid":19,"ts":11000}]"#,
    );
    load_trace(&mut headless, &trace);
    headless.zoom(250.0, 300.0, 2.0);
    // Ctrl+F opens Details, settle, Ctrl+F focuses the search field, then type.
    headless.key_shortcut(KEY_F, MOD_CTRL);
    headless.update();
    headless.update();
    headless.update();
    headless.key_shortcut(KEY_F, MOD_CTRL);
    headless.text_input("SearchTarget");
    finish_pending_search(&mut headless);
    // Render the first results frame before ImGui column autofit widens fixed columns.
    assert_golden(&headless, "search_highlights_golden");
}

#[test]
fn counter_tracks_golden() {
    let _guard = IMGUI.lock().unwrap_or_else(|error| error.into_inner());
    let mut headless = HeadlessApp::create(800, 600).unwrap();
    load_trace(
        &mut headless,
        r#"[{"name":"process_name","ph":"M","pid":1,"args":{"name":"TestProcess"}},{"name":"thread_name","ph":"M","pid":1,"tid":1,"args":{"name":"MainThread"}},{"name":"MyCounter","ph":"C","pid":1,"tid":1,"ts":1000,"args":{"Series1":10,"Series2":20}},{"name":"MyCounter","ph":"C","pid":1,"tid":1,"ts":2000,"args":{"Series1":30,"Series2":15}},{"name":"MyCounter","ph":"C","pid":1,"tid":1,"ts":3000,"args":{"Series1":15,"Series2":40}},{"name":"MyCounter","ph":"C","pid":1,"tid":1,"ts":4000,"args":{"Series1":50,"Series2":10}}]"#,
    );
    headless.update();
    assert_golden(&headless, "counter_tracks_golden")
}

#[test]
fn multi_lane_golden() {
    let _guard = IMGUI.lock().unwrap_or_else(|error| error.into_inner());
    let mut headless = HeadlessApp::create(800, 600).unwrap();
    load_trace(
        &mut headless,
        r#"[{"name":"Event1","cat":"cat","ph":"X","pid":1,"tid":1,"ts":1000,"dur":1000},{"name":"Event2","cat":"cat","ph":"X","pid":1,"tid":1,"ts":1500,"dur":1000},{"name":"Event3","cat":"cat","ph":"X","pid":1,"tid":1,"ts":1800,"dur":500}]"#,
    );
    headless.update();
    assert_golden(&headless, "multi_lane_golden")
}

#[test]
fn event_selection_golden() {
    let _guard = IMGUI.lock().unwrap_or_else(|error| error.into_inner());
    let mut headless = HeadlessApp::create(800, 600).unwrap();
    let trace = r#"[{"name":"process_name","ph":"M","pid":1,"args":{"name":"TestProcess"}},{"name":"thread_name","ph":"M","pid":1,"tid":1,"args":{"name":"MainThread"}},{"name":"EventA","cat":"ui","ph":"B","pid":1,"tid":1,"ts":1000},{"name":"EventSub","cat":"render","ph":"B","pid":1,"tid":1,"ts":1200},{"name":"EventSub","cat":"render","ph":"E","pid":1,"tid":1,"ts":1600},{"name":"EventA","cat":"ui","ph":"E","pid":1,"tid":1,"ts":2500},{"name":"EventB","cat":"network","ph":"B","pid":1,"tid":1,"ts":3000},{"name":"EventB","cat":"network","ph":"E","pid":1,"tid":1,"ts":4500}]"#;
    load_trace(&mut headless, trace);
    headless.click(250.0, 104.0);
    headless.update();
    headless.update();
    headless.update();
    assert_golden(&headless, "event_selection_golden")
}

#[test]
fn timeline_navigation_golden() {
    let _guard = IMGUI.lock().unwrap_or_else(|error| error.into_inner());
    let mut headless = HeadlessApp::create(800, 600).unwrap();
    let mut trace = String::from(
        r#"[{"name":"process_name","ph":"M","pid":1,"args":{"name":"TestProcess"}},{"name":"thread_name","ph":"M","pid":1,"tid":1,"args":{"name":"MainThread"}}"#,
    );
    for index in 0..100 {
        trace.push_str(&format!(r#",{{"name":"Event_{index}","cat":"test","ph":"X","pid":1,"tid":1,"ts":{},"dur":500}}"#,index*1000))
    }
    trace.push(']');
    load_trace(&mut headless, &trace);
    headless.update();
    assert_golden(&headless, "timeline_navigation_initial");
    headless.zoom(400.0, 300.0, 1.0);
    assert_golden(&headless, "timeline_navigation_zoomed");
    headless.drag((400.0, 300.0), (500.0, 300.0));
    assert_golden(&headless, "timeline_navigation_panned");
}

#[test]
fn main_timeline_golden() {
    let _guard = IMGUI.lock().unwrap_or_else(|error| error.into_inner());
    let mut headless = HeadlessApp::create(800, 600).unwrap();
    let trace = r#"[{"name":"process_name","ph":"M","pid":1,"args":{"name":"TestProcess"}},{"name":"thread_name","ph":"M","pid":1,"tid":1,"args":{"name":"MainThread"}},{"name":"EventA","cat":"ui","ph":"B","pid":1,"tid":1,"ts":1000},{"name":"EventSub","cat":"render","ph":"B","pid":1,"tid":1,"ts":1200},{"name":"EventSub","cat":"render","ph":"E","pid":1,"tid":1,"ts":1600},{"name":"EventA","cat":"ui","ph":"E","pid":1,"tid":1,"ts":2500},{"name":"EventB","cat":"network","ph":"B","pid":1,"tid":1,"ts":3000},{"name":"EventB","cat":"network","ph":"E","pid":1,"tid":1,"ts":4500}]"#;
    load_trace(&mut headless, trace);
    assert_golden(&headless, "main_timeline_golden");
}

#[test]
fn loading_screen_golden() {
    let _guard = IMGUI.lock().unwrap_or_else(|error| error.into_inner());
    let mut headless = HeadlessApp::create(800, 600).expect("headless GL context");
    headless
        .runtime
        .app
        .begin_session(1, "large_trace.json", 10_000);
    headless
        .runtime
        .app
        .handle_file_chunk(1, br#"[{"name":"Event","#.to_vec(), 5_000, false);
    headless.update();
    headless.update();
    headless.update();
    assert_golden(&headless, "loading_screen_golden");
}

#[test]
fn shortcuts_cheatsheet_golden() {
    let _guard = IMGUI.lock().unwrap_or_else(|error| error.into_inner());
    let mut headless = HeadlessApp::create(800, 600).unwrap();
    let trace = r#"[{"name":"process_name","ph":"M","pid":1,"args":{"name":"TestProcess"}},{"name":"thread_name","ph":"M","pid":1,"tid":1,"args":{"name":"MainThread"}},{"name":"EventA","cat":"ui","ph":"B","pid":1,"tid":1,"ts":1000},{"name":"EventSub","cat":"render","ph":"B","pid":1,"tid":1,"ts":1200},{"name":"EventSub","cat":"render","ph":"E","pid":1,"tid":1,"ts":1600},{"name":"EventA","cat":"ui","ph":"E","pid":1,"tid":1,"ts":2500},{"name":"EventB","cat":"network","ph":"B","pid":1,"tid":1,"ts":3000},{"name":"EventB","cat":"network","ph":"E","pid":1,"tid":1,"ts":4500}]"#;
    load_trace(&mut headless, trace);
    headless.key_shortcut(KEY_SLASH, MOD_SHIFT);
    assert_golden(&headless, "shortcuts_cheatsheet_golden");
}

#[test]
fn question_mark_does_not_open_shortcuts_while_search_input_is_active() {
    let _guard = IMGUI.lock().unwrap_or_else(|error| error.into_inner());
    let mut headless = HeadlessApp::create(800, 600).unwrap();
    load_trace(
        &mut headless,
        r#"[{"name":"event","ph":"X","pid":1,"tid":1,"ts":1,"dur":1}]"#,
    );
    headless.key_shortcut(KEY_F, MOD_CTRL);
    headless.update();
    headless.key_shortcut(KEY_SLASH, MOD_SHIFT);

    assert!(!headless.runtime.app.show_shortcuts);
}

#[test]
fn timeline_double_click_zoom_golden() {
    let _guard = IMGUI.lock().unwrap_or_else(|error| error.into_inner());
    let mut headless = HeadlessApp::create(800, 600).unwrap();
    let trace = r#"[{"name":"process_name","ph":"M","pid":1,"args":{"name":"TestProcess"}},{"name":"thread_name","ph":"M","pid":1,"tid":1,"args":{"name":"MainThread"}},{"name":"EventStart","cat":"ui","ph":"B","pid":1,"tid":1,"ts":1000},{"name":"EventStart","cat":"ui","ph":"E","pid":1,"tid":1,"ts":2000},{"name":"EventA","cat":"ui","ph":"B","pid":1,"tid":1,"ts":24000},{"name":"EventA","cat":"ui","ph":"E","pid":1,"tid":1,"ts":26000},{"name":"EventEnd","cat":"ui","ph":"B","pid":1,"tid":1,"ts":49000},{"name":"EventEnd","cat":"ui","ph":"E","pid":1,"tid":1,"ts":50000}]"#;
    load_trace(&mut headless, trace);
    headless.click(368.0, 104.0);
    headless.update();
    headless.update();
    headless.update();
    assert_golden(&headless, "timeline_double_click_zoom_initial");
    headless.double_click(242.8, 104.0);
    headless.update();
    let viewport = headless.runtime.app.viewer.viewport_range();
    assert!(
        viewport.1 - viewport.0 < 10_000.0,
        "double-click zoom failed: viewport={:?} focused={:?}",
        viewport,
        headless.runtime.app.viewer.focused_event()
    );
    assert_golden(&headless, "timeline_double_click_zoom_zoomed");
}

#[test]
fn vertical_minimap_scroll_golden() {
    let _guard = IMGUI.lock().unwrap_or_else(|error| error.into_inner());
    let mut headless = HeadlessApp::create(800, 600).unwrap();
    let mut trace =
        String::from(r#"[{"name":"process_name","ph":"M","pid":1,"args":{"name":"TestProcess"}}"#);
    for tid in 1..=300 {
        trace.push_str(&format!(r#",{{"name":"thread_name","ph":"M","pid":1,"tid":{tid},"args":{{"name":"Thread_{tid}"}}}},{{"name":"OtherEvent","cat":"ui","ph":"B","pid":1,"tid":{tid},"ts":1200}},{{"name":"OtherEvent","cat":"ui","ph":"E","pid":1,"tid":{tid},"ts":2200}}"#));
    }
    trace.push(']');
    load_trace(&mut headless, &trace);
    headless.runtime.app.viewer.set_show_details(false);
    headless.update();
    assert_golden(&headless, "vertical_minimap_scroll_initial");
    headless.click(768.0, 400.0);
    headless.update();
    assert_golden(&headless, "vertical_minimap_scroll_scrolled");
}

#[test]
fn vertical_minimap_slider_drag_golden() {
    let _guard = IMGUI.lock().unwrap_or_else(|error| error.into_inner());
    let mut headless = HeadlessApp::create(800, 600).unwrap();
    let mut trace =
        String::from(r#"[{"name":"process_name","ph":"M","pid":1,"args":{"name":"TestProcess"}}"#);
    for tid in 1..=300 {
        let color_band = (tid - 1) / 8 % 8;
        trace.push_str(&format!(r#",{{"name":"thread_name","ph":"M","pid":1,"tid":{tid},"args":{{"name":"Thread_{tid}"}}}}"#));
        for bucket in 0..16 {
            let timestamp = 1_000 + bucket * 1_000;
            trace.push_str(&format!(r#",{{"name":"MinimapColor{color_band}","cat":"ui","ph":"X","pid":1,"tid":{tid},"ts":{timestamp},"dur":500}}"#));
        }
    }
    trace.push(']');
    load_trace(&mut headless, &trace);
    headless.runtime.app.viewer.set_show_details(false);
    headless.update();

    let initial_scroll = headless.runtime.app.viewer.tracks_scroll_y();
    let initial_minimap_scroll = headless.runtime.app.viewer.minimap_scroll_y();
    let slider_bounds = headless.runtime.app.viewer.minimap_slider_bounds().unwrap();
    let slider_center = (
        (slider_bounds.0 + slider_bounds.2) * 0.5,
        (slider_bounds.1 + slider_bounds.3) * 0.5,
    );
    headless.drag(slider_center, (slider_center.0, slider_center.1 + 100.0));
    headless.update();

    assert!(
        headless.runtime.app.viewer.tracks_scroll_y() > initial_scroll,
        "dragging the minimap slider did not scroll the track child"
    );
    assert!(
        headless.runtime.app.viewer.minimap_scroll_y() > initial_minimap_scroll,
        "the minimap contents did not follow the slider"
    );
    assert_golden(&headless, "vertical_minimap_slider_drag_golden");
}

#[test]
fn vertical_track_drag_golden() {
    let _guard = IMGUI.lock().unwrap_or_else(|error| error.into_inner());
    let mut headless = HeadlessApp::create(800, 600).unwrap();
    let mut trace =
        String::from(r#"[{"name":"process_name","ph":"M","pid":1,"args":{"name":"TestProcess"}}"#);
    for tid in 1..=80 {
        trace.push_str(&format!(r#",{{"name":"thread_name","ph":"M","pid":1,"tid":{tid},"args":{{"name":"Thread_{tid}"}}}},{{"name":"Event_{tid}","cat":"ui","ph":"X","pid":1,"tid":{tid},"ts":1000,"dur":1000}}"#));
    }
    trace.push(']');
    load_trace(&mut headless, &trace);
    headless.runtime.app.viewer.set_show_details(false);
    headless.update();

    let initial_scroll = headless.runtime.app.viewer.tracks_scroll_y();
    headless.drag((400.0, 400.0), (400.0, 150.0));
    headless.runtime.imgui.add_mouse_position(799.0, 599.0);
    headless.update();

    assert!(
        headless.runtime.app.viewer.tracks_scroll_y() > initial_scroll,
        "dragging upward in the track list did not scroll it downward"
    );
    assert_golden(&headless, "vertical_track_drag_golden");
}

#[test]
fn track_header_tooltip_golden() {
    let _guard = IMGUI.lock().unwrap_or_else(|error| error.into_inner());
    let mut headless = HeadlessApp::create(800, 600).unwrap();
    load_trace(
        &mut headless,
        r#"[{"name":"thread_name","ph":"M","pid":7,"tid":42,"args":{"name":"Worker"}},{"name":"Event","cat":"ui","ph":"X","pid":7,"tid":42,"ts":1000,"dur":1000}]"#,
    );
    headless.runtime.app.viewer.set_show_details(false);
    headless.runtime.imgui.add_mouse_position(10.0, 50.0);
    headless.update();
    headless.update();
    assert_golden(&headless, "track_header_tooltip_golden");
}

#[test]
fn timeline_selection_resized_golden() {
    let _guard = IMGUI.lock().unwrap_or_else(|error| error.into_inner());
    let mut headless = HeadlessApp::create(800, 600).unwrap();
    let trace = r#"[{"name":"process_name","ph":"M","pid":1,"args":{"name":"TestProcess"}},{"name":"thread_name","ph":"M","pid":1,"tid":1,"args":{"name":"MainThread"}},{"name":"EventA","cat":"ui","ph":"B","pid":1,"tid":1,"ts":1000},{"name":"EventSub","cat":"render","ph":"B","pid":1,"tid":1,"ts":1200},{"name":"EventSub","cat":"render","ph":"E","pid":1,"tid":1,"ts":1600},{"name":"EventA","cat":"ui","ph":"E","pid":1,"tid":1,"ts":2500},{"name":"EventB","cat":"network","ph":"B","pid":1,"tid":1,"ts":3000},{"name":"EventB","cat":"network","ph":"E","pid":1,"tid":1,"ts":4500}]"#;
    load_trace(&mut headless, trace);
    headless.drag((250.0, 28.0), (450.0, 28.0));
    headless.update();
    assert_golden(&headless, "timeline_selection_resized_initial");
    headless.drag((250.0, 28.0), (150.0, 28.0));
    headless.update();
    assert_golden(&headless, "timeline_selection_resized_resized");
}

#[test]
fn timeline_box_selection_golden() {
    let _guard = IMGUI.lock().unwrap_or_else(|error| error.into_inner());
    let mut headless = HeadlessApp::create(800, 600).unwrap();
    let trace = r#"[{"name":"process_name","ph":"M","pid":1,"args":{"name":"TestProcess"}},{"name":"thread_name","ph":"M","pid":1,"tid":1,"args":{"name":"MainThread"}},{"name":"EventA","cat":"ui","ph":"B","pid":1,"tid":1,"ts":1000},{"name":"EventSub","cat":"render","ph":"B","pid":1,"tid":1,"ts":1200},{"name":"EventSub","cat":"render","ph":"E","pid":1,"tid":1,"ts":1600},{"name":"EventA","cat":"ui","ph":"E","pid":1,"tid":1,"ts":2500},{"name":"EventB","cat":"network","ph":"B","pid":1,"tid":1,"ts":3000},{"name":"EventB","cat":"network","ph":"E","pid":1,"tid":1,"ts":4500}]"#;
    load_trace(&mut headless, trace);
    // Keep Shift + button down while capturing the dragging golden.
    headless.runtime.imgui.add_key(MOD_SHIFT, true);
    headless.runtime.imgui.add_mouse_position(100.0, 80.0);
    headless.runtime.imgui.add_mouse_button(0, true);
    headless.update();
    headless.runtime.imgui.add_mouse_position(700.0, 130.0);
    headless.update();
    headless.update();
    assert_golden(&headless, "timeline_box_selection_dragging");
    headless.runtime.imgui.add_mouse_button(0, false);
    headless.runtime.imgui.add_key(MOD_SHIFT, false);
    headless.update();
    assert_golden(&headless, "timeline_box_selection_selected");
}

#[test]
fn timeline_hover_tooltip_thread_golden() {
    let _guard = IMGUI.lock().unwrap_or_else(|error| error.into_inner());
    let mut headless = HeadlessApp::create(800, 600).unwrap();
    let trace = r#"[{"name":"process_name","ph":"M","pid":1,"args":{"name":"TestProcess"}},{"name":"thread_name","ph":"M","pid":1,"tid":1,"args":{"name":"MainThread"}},{"name":"EventA","cat":"ui","ph":"B","pid":1,"tid":1,"ts":1000},{"name":"EventSub","cat":"render","ph":"B","pid":1,"tid":1,"ts":1200},{"name":"EventSub","cat":"render","ph":"E","pid":1,"tid":1,"ts":1600},{"name":"EventA","cat":"ui","ph":"E","pid":1,"tid":1,"ts":2500},{"name":"EventB","cat":"network","ph":"B","pid":1,"tid":1,"ts":3000},{"name":"EventB","cat":"network","ph":"E","pid":1,"tid":1,"ts":4500}]"#;
    load_trace(&mut headless, trace);
    headless.update();
    headless.runtime.imgui.add_mouse_position(200.0, 104.0);
    headless.update();
    headless.update();
    headless.update();
    assert_golden(&headless, "timeline_hover_tooltip_thread_golden");
}

#[test]
fn timeline_hover_tooltip_counter_golden() {
    let _guard = IMGUI.lock().unwrap_or_else(|error| error.into_inner());
    let mut headless = HeadlessApp::create(800, 600).unwrap();
    let trace = r#"[{"name":"process_name","ph":"M","pid":1,"args":{"name":"TestProcess"}},{"name":"thread_name","ph":"M","pid":1,"tid":1,"args":{"name":"MainThread"}},{"name":"MyCounter","ph":"C","pid":1,"tid":1,"ts":1000,"args":{"Series1":10,"Series2":20}},{"name":"MyCounter","ph":"C","pid":1,"tid":1,"ts":2000,"args":{"Series1":30,"Series2":15}},{"name":"MyCounter","ph":"C","pid":1,"tid":1,"ts":3000,"args":{"Series1":15,"Series2":40}},{"name":"MyCounter","ph":"C","pid":1,"tid":1,"ts":4000,"args":{"Series1":50,"Series2":10}}]"#;
    load_trace(&mut headless, trace);
    headless.update();
    headless.runtime.imgui.add_mouse_position(360.0, 76.0);
    headless.update();
    headless.update();
    headless.update();
    assert_golden(&headless, "timeline_hover_tooltip_counter_golden");
}

#[test]
fn details_click_to_focus_golden() {
    let _guard = IMGUI.lock().unwrap_or_else(|error| error.into_inner());
    let mut headless = HeadlessApp::create(800, 600).unwrap();
    let mut trace = String::from(
        r#"[{"name":"process_name","ph":"M","pid":1,"args":{"name":"TestProcess"}},{"name":"thread_name","ph":"M","pid":1,"tid":1,"args":{"name":"MainThread"}},"#,
    );
    for t in 1..=13 {
        trace.push_str(&format!(r#"{{"name":"thread_name","ph":"M","pid":1,"tid":{},"args":{{"name":"NoiseThread{t}"}}}},"#, t + 1));
    }
    trace.push_str(r#"{"name":"thread_name","ph":"M","pid":1,"tid":15,"args":{"name":"TargetThread"}},{"name":"EventA","cat":"ui","ph":"X","pid":1,"tid":1,"ts":1000,"dur":10000},"#);
    for t in 1..=13 {
        let tid = t + 1;
        let ts1 = 20000 + t * 2000;
        let ts2 = 80000 + t * 2000;
        trace.push_str(&format!(r#"{{"name":"Noise","cat":"noise","ph":"X","pid":1,"tid":{tid},"ts":{ts1},"dur":1000}},{{"name":"Noise","cat":"noise","ph":"X","pid":1,"tid":{tid},"ts":{ts2},"dur":1000}},"#));
    }
    trace.push_str(
        r#"{"name":"EventB","cat":"ui","ph":"X","pid":1,"tid":15,"ts":112000,"dur":10000}]"#,
    );
    load_trace(&mut headless, &trace);
    headless.update();
    headless.update();

    headless.drag_with_modifier((5.0, 45.0), (730.0, 640.0), MOD_SHIFT);
    headless.update();
    headless.update();
    // Double-click EventA after box-select (details already open).
    headless.double_click(58.0, 104.5);
    let viewport = headless.runtime.app.viewer.viewport_range();
    assert!(
        viewport.1 - viewport.0 < 20_000.0,
        "expected zoom after double-click, viewport={:?} focused={:?} selected={} details={}",
        viewport,
        headless.runtime.app.viewer.focused_event(),
        headless.runtime.app.viewer.selected_events().len(),
        headless.runtime.app.viewer.show_details()
    );
    headless.click(650.0, 75.0);
    headless.text_input("Event");
    headless.update();
    headless.update();
    headless.update();
    assert_golden(&headless, "details_click_to_focus_before_golden");

    headless.click(650.0, 298.0);
    headless.update();
    headless.update();
    headless.update();
    assert_golden(&headless, "details_click_to_focus_after_golden");
}

#[test]
fn search_filter_counters_only_golden() {
    let _guard = IMGUI.lock().unwrap_or_else(|error| error.into_inner());
    let mut headless = HeadlessApp::create(800, 600).unwrap();
    let trace = r#"[{"name":"process_name","ph":"M","pid":1,"args":{"name":"TestProcess"}},{"name":"thread_name","ph":"M","pid":1,"tid":1,"args":{"name":"MainThread"}},{"name":"EventA","cat":"ui","ph":"B","pid":1,"tid":1,"ts":1000},{"name":"EventA","cat":"ui","ph":"E","pid":1,"tid":1,"ts":2000},{"name":"EventA","ph":"C","pid":1,"tid":1,"ts":1200,"args":{"val":10}},{"name":"EventA","ph":"C","pid":1,"tid":1,"ts":1500,"args":{"val":30}},{"name":"EventA","ph":"C","pid":1,"tid":1,"ts":1800,"args":{"val":20}}]"#;
    load_trace(&mut headless, trace);
    headless.update();

    headless.key_shortcut(KEY_F, MOD_CTRL);
    headless.update();
    headless.update();
    headless.update();
    headless.key_shortcut(KEY_F, MOD_CTRL);
    headless.text_input("EventA");
    finish_pending_search(&mut headless);
    headless.update();
    headless.update();
    assert_golden(&headless, "search_filter_counters_only_before_golden");

    headless.click(580.0, 96.0);
    // Move off the checkbox so ImGui hover chrome matches the idle reference.
    headless.runtime.imgui.add_mouse_position(0.0, 0.0);
    finish_pending_search(&mut headless);
    headless.update();
    headless.update();
    assert_golden(&headless, "search_filter_counters_only_after_golden");
}

#[test]
fn shortcuts_cheatsheet_dismissal_golden() {
    let _guard = IMGUI.lock().unwrap_or_else(|error| error.into_inner());
    let mut headless = HeadlessApp::create(800, 600).unwrap();
    let trace = r#"[{"name":"process_name","ph":"M","pid":1,"args":{"name":"TestProcess"}},{"name":"thread_name","ph":"M","pid":1,"tid":1,"args":{"name":"MainThread"}},{"name":"EventA","cat":"ui","ph":"B","pid":1,"tid":1,"ts":1000},{"name":"EventSub","cat":"render","ph":"B","pid":1,"tid":1,"ts":1200},{"name":"EventSub","cat":"render","ph":"E","pid":1,"tid":1,"ts":1600},{"name":"EventA","cat":"ui","ph":"E","pid":1,"tid":1,"ts":2500},{"name":"EventB","cat":"network","ph":"B","pid":1,"tid":1,"ts":3000},{"name":"EventB","cat":"network","ph":"E","pid":1,"tid":1,"ts":4500}]"#;
    load_trace(&mut headless, trace);
    headless.key_shortcut(KEY_SLASH, MOD_SHIFT);
    headless.update();
    headless.click(50.0, 300.0);
    headless.update();
    assert_golden(&headless, "shortcuts_cheatsheet_dismissed_golden");
}

#[test]
fn regression_rapid_session_restart_with_active_search() {
    let _guard = IMGUI.lock().unwrap_or_else(|error| error.into_inner());
    let mut headless = HeadlessApp::create(800, 600).unwrap();
    let trace1 = r#"[{"name":"process_name","ph":"M","pid":1,"args":{"name":"TestProcess"}},{"name":"thread_name","ph":"M","pid":1,"tid":1,"args":{"name":"MainThread"}},{"name":"EventA","cat":"ui","ph":"B","pid":1,"tid":1,"ts":1000},{"name":"EventA","cat":"ui","ph":"E","pid":1,"tid":1,"ts":2500}]"#;
    load_trace(&mut headless, trace1);

    headless.key_shortcut(KEY_F, MOD_CTRL);
    headless.update();
    headless.text_input("Event");
    headless.update();
    assert!(headless.runtime.app.search_active());

    let trace2 = r#"[{"name":"process_name","ph":"M","pid":2,"args":{"name":"NewProcess"}},{"name":"EventNew","cat":"ui","ph":"X","pid":2,"tid":1,"ts":1000,"dur":100}]"#;
    load_trace(&mut headless, trace2);

    assert!(!headless.runtime.app.loading.active);
    assert_eq!(
        headless
            .runtime
            .app
            .trace_data
            .as_ref()
            .unwrap()
            .events
            .len(),
        2
    );
    assert!(!headless.runtime.app.search_active());
    assert!(headless.runtime.app.viewer.selected_events().is_empty());
}

#[test]
fn search_input_grows_beyond_the_legacy_initial_capacity() {
    let _guard = IMGUI.lock().unwrap_or_else(|error| error.into_inner());
    let mut headless = HeadlessApp::create(800, 600).unwrap();
    load_trace(
        &mut headless,
        r#"[{"name":"event","ph":"X","pid":1,"tid":1,"ts":1,"dur":1}]"#,
    );
    headless.key_shortcut(KEY_F, MOD_CTRL);
    headless.update();
    headless.key_shortcut(KEY_F, MOD_CTRL);
    let query = "x".repeat(700);
    headless.text_input(&query);
    headless.update();
    assert_eq!(headless.runtime.app.search.query, query);
}

#[test]
fn enter_resubmits_an_unchanged_search_query() {
    let _guard = IMGUI.lock().unwrap_or_else(|error| error.into_inner());
    let mut headless = HeadlessApp::create(800, 600).unwrap();
    load_trace(
        &mut headless,
        r#"[{"name":"event","ph":"X","pid":1,"tid":1,"ts":1,"dur":1}]"#,
    );
    headless.key_shortcut(KEY_F, MOD_CTRL);
    headless.update();
    headless.key_shortcut(KEY_F, MOD_CTRL);
    headless.text_input("event");
    finish_pending_search(&mut headless);
    assert!(!headless.runtime.app.search_active());

    headless.runtime.imgui.add_key(KEY_ENTER, true);
    headless.update();
    headless.runtime.imgui.add_key(KEY_ENTER, false);
    headless.update();
    assert!(headless.runtime.app.search_active());

    wait_until(
        &mut headless,
        SHORT_WAIT,
        "resubmitted search",
        |headless| !headless.runtime.app.search_active(),
    );
    assert_eq!(headless.runtime.app.viewer.selected_events(), [0]);
}

#[test]
fn regression_rapid_loader_session_restart_race() {
    let _guard = IMGUI.lock().unwrap_or_else(|error| error.into_inner());
    let mut headless = HeadlessApp::create(800, 600).unwrap();
    let trace1 = r#"[{"name":"Event1","cat":"ui","ph":"B","pid":1,"tid":1,"ts":100}]"#;
    headless
        .runtime
        .app
        .begin_session(1, "trace1.json", trace1.len());
    headless
        .runtime
        .app
        .handle_file_chunk(1, trace1.as_bytes().to_vec(), trace1.len(), false);

    let trace2 = r#"[{"name":"Event2","cat":"ui","ph":"X","pid":1,"tid":1,"ts":200,"dur":50}]"#;
    load_trace_with_session(&mut headless, 2, trace2);

    assert!(!headless.runtime.app.loading.active);
    let data = headless.runtime.app.trace_data.as_ref().unwrap();
    assert_eq!(data.events.len(), 1);
    assert_eq!(data.string(data.events[0].name), b"Event2");
}
