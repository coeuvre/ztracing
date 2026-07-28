use std::cmp::Ordering;
use std::collections::HashMap;

use super::data::TraceData;
use crate::string_interner::StringId;

pub const BLOCK_SIZE: usize = 1024;
pub const PYRAMID_FANOUT: usize = 16;

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct PyramidNode {
    pub min_timestamp: i64,
    pub max_timestamp: i64,
    pub max_duration: i64,
    pub dominant_event_index: usize,
    pub event_count: u32,
    pub max_depth: u32,
}

#[derive(Clone, Copy, Debug, Default, Eq, Ord, PartialEq, PartialOrd)]
pub enum TrackType {
    Counter,
    #[default]
    Thread,
}

#[derive(Clone, Debug, Default)]
pub struct Track {
    pub kind: TrackType,
    pub process_id: i32,
    pub thread_id: i32,
    pub name: StringId,
    pub id: StringId,
    pub sort_index: i32,
    pub event_indices: Vec<usize>,
    pub depths: Vec<u32>,
    pub self_durations: Vec<i64>,
    pub counter_series: Vec<StringId>,
    pub counter_palette_indices: Vec<u8>,
    pub block_max_durations: Vec<i64>,
    pub pyramid_levels: Vec<Vec<PyramidNode>>,
    pub counter_max_total: f64,
    pub max_duration: i64,
    pub max_depth: u32,
}

#[derive(Clone, Copy)]
pub(crate) struct SortKey {
    pub(crate) ts: i64,
    pub(crate) dur: i64,
    pub(crate) index: usize,
}

#[derive(Clone, Copy)]
pub(crate) struct StackEvent {
    pub(crate) end: i64,
    pub(crate) depth: u32,
    pub(crate) track_index: usize,
}

impl Track {
    pub fn thread(process_id: i32, thread_id: i32) -> Self {
        Self::new(
            TrackType::Thread,
            process_id,
            thread_id,
            StringId(0),
            StringId(0),
        )
    }

    pub fn counter(process_id: i32, name: StringId, id: StringId) -> Self {
        Self::new(TrackType::Counter, process_id, -1, name, id)
    }

    fn new(kind: TrackType, process_id: i32, thread_id: i32, name: StringId, id: StringId) -> Self {
        Self {
            kind,
            process_id,
            thread_id,
            name,
            id,
            sort_index: 0,
            event_indices: Vec::new(),
            depths: Vec::new(),
            self_durations: Vec::new(),
            counter_series: Vec::new(),
            counter_palette_indices: Vec::new(),
            block_max_durations: Vec::new(),
            pyramid_levels: Vec::new(),
            counter_max_total: 0.0,
            max_duration: 0,
            max_depth: 0,
        }
    }

    fn finish(&mut self, data: &TraceData) {
        self.sort_events(data);
        self.update_max_duration(data);
        if self.kind == TrackType::Thread {
            self.calculate_depths(data);
            self.build_pyramid(data);
        } else {
            self.depths.resize(self.event_indices.len(), 0);
            self.self_durations.resize(self.event_indices.len(), 0);
            self.finish_counter(data);
        }
    }

    pub fn sort_events(&mut self, data: &TraceData) {
        if self.event_indices.len() <= 1 {
            return;
        }

        let mut keys: Vec<SortKey> = self
            .event_indices
            .iter()
            .map(|&index| {
                let e = &data.events[index];
                SortKey {
                    ts: e.timestamp,
                    dur: e.duration,
                    index,
                }
            })
            .collect();

        keys.sort_unstable_by(|a, b| {
            a.ts.cmp(&b.ts)
                .then_with(|| b.dur.cmp(&a.dur))
                .then_with(|| a.index.cmp(&b.index))
        });

        for (slot, key) in self.event_indices.iter_mut().zip(&keys) {
            *slot = key.index;
        }
    }

    pub fn update_max_duration(&mut self, data: &TraceData) {
        self.block_max_durations = self
            .event_indices
            .chunks(BLOCK_SIZE)
            .map(|chunk| {
                chunk
                    .iter()
                    .map(|&index| data.events[index].duration)
                    .max()
                    .unwrap_or(0)
                    .max(0)
            })
            .collect();
        self.max_duration = self.block_max_durations.iter().copied().max().unwrap_or(0);
    }

    pub fn calculate_depths(&mut self, data: &TraceData) {
        self.depths.resize(self.event_indices.len(), 0);
        self.self_durations.resize(self.event_indices.len(), 0);
        let mut stack: Vec<StackEvent> = Vec::new();
        self.max_depth = 0;
        for (track_index, &event_index) in self.event_indices.iter().enumerate() {
            let event = &data.events[event_index];
            let end = event.timestamp.saturating_add(event.duration);
            self.self_durations[track_index] = event.duration;
            while stack
                .last()
                .is_some_and(|parent| parent.end <= event.timestamp)
            {
                stack.pop();
            }
            let mut depth = 0;
            for parent in stack.iter().rev() {
                if parent.end >= end {
                    depth = parent.depth + 1;
                    self.self_durations[parent.track_index] -= event.duration;
                    break;
                }
            }
            self.depths[track_index] = depth;
            self.max_depth = self.max_depth.max(depth);
            stack.push(StackEvent {
                end,
                depth,
                track_index,
            });
        }
    }

    fn finish_counter(&mut self, data: &TraceData) {
        for &event_index in &self.event_indices {
            let event = &data.events[event_index];
            let mut total = 0.0;
            for arg in data.event_args(event) {
                if !self.counter_series.contains(&arg.key) {
                    self.counter_series.push(arg.key);
                }
                total += arg.number;
            }
            self.counter_max_total = self.counter_max_total.max(total);
        }
        self.counter_series
            .sort_unstable_by(|a, b| data.string(*a).cmp(data.string(*b)));
        self.counter_palette_indices = self
            .counter_series
            .iter()
            .map(|reference| (data.string_hash(*reference) % 8) as u8)
            .collect();
    }

    pub fn build_pyramid(&mut self, data: &TraceData) {
        self.pyramid_levels.clear();
        if self.event_indices.len() <= PYRAMID_FANOUT {
            return;
        }

        let mut level1 = Vec::with_capacity(self.event_indices.len().div_ceil(PYRAMID_FANOUT));
        for (chunk_idx, chunk) in self.event_indices.chunks(PYRAMID_FANOUT).enumerate() {
            let mut min_ts = i64::MAX;
            let mut max_ts = i64::MIN;
            let mut max_dur = -1_i64;
            let mut dom_idx = chunk[0];
            let mut max_d = 0_u32;

            let start_pos = chunk_idx * PYRAMID_FANOUT;
            for (offset, &event_index) in chunk.iter().enumerate() {
                let event = &data.events[event_index];
                let end = event.timestamp.saturating_add(event.duration);
                min_ts = min_ts.min(event.timestamp);
                max_ts = max_ts.max(end);
                if event.duration > max_dur {
                    max_dur = event.duration;
                    dom_idx = event_index;
                }
                if let Some(&d) = self.depths.get(start_pos + offset) {
                    max_d = max_d.max(d);
                }
            }
            level1.push(PyramidNode {
                min_timestamp: min_ts,
                max_timestamp: max_ts,
                max_duration: max_dur,
                dominant_event_index: dom_idx,
                event_count: chunk.len() as u32,
                max_depth: max_d,
            });
        }
        self.pyramid_levels.push(level1);

        while self.pyramid_levels.last().map_or(0, |lvl| lvl.len()) > PYRAMID_FANOUT {
            let prev_level = self.pyramid_levels.last().unwrap();
            let mut next_level = Vec::with_capacity(prev_level.len().div_ceil(PYRAMID_FANOUT));

            for chunk in prev_level.chunks(PYRAMID_FANOUT) {
                let mut min_ts = i64::MAX;
                let mut max_ts = i64::MIN;
                let mut max_dur = -1_i64;
                let mut dom_idx = chunk[0].dominant_event_index;
                let mut total_count = 0_u32;
                let mut max_d = 0_u32;

                for node in chunk {
                    min_ts = min_ts.min(node.min_timestamp);
                    max_ts = max_ts.max(node.max_timestamp);
                    if node.max_duration > max_dur {
                        max_dur = node.max_duration;
                        dom_idx = node.dominant_event_index;
                    }
                    total_count += node.event_count;
                    max_d = max_d.max(node.max_depth);
                }
                next_level.push(PyramidNode {
                    min_timestamp: min_ts,
                    max_timestamp: max_ts,
                    max_duration: max_dur,
                    dominant_event_index: dom_idx,
                    event_count: total_count,
                    max_depth: max_d,
                });
            }
            self.pyramid_levels.push(next_level);
        }
    }

    pub fn visible_start(&self, data: &TraceData, viewport_start: i64) -> usize {
        if self.event_indices.is_empty() {
            return 0;
        }
        let mut first_block = 0;
        for (block, &max_duration) in self.block_max_durations.iter().enumerate() {
            let end = ((block + 1) * BLOCK_SIZE).min(self.event_indices.len());
            let last_timestamp = data.events[self.event_indices[end - 1]].timestamp;
            if last_timestamp.saturating_add(max_duration) < viewport_start {
                first_block = block + 1;
            } else {
                break;
            }
        }
        let start = (first_block * BLOCK_SIZE).min(self.event_indices.len());
        self.event_indices[start..].partition_point(|&index| {
            data.events[index].timestamp < viewport_start.saturating_sub(self.max_duration)
        }) + start
    }
}

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
struct TrackKey {
    process_id: i32,
    thread_id: i32,
    name: StringId,
    id: StringId,
}

pub fn organize_tracks(data: &TraceData) -> (Vec<Track>, i64, i64) {
    if data.events.is_empty() {
        return (Vec::new(), 0, 0);
    }
    let counter_phase = data.find(b"C");
    let metadata_phase = data.find(b"M");
    let mut tracks = Vec::<Track>::new();
    let mut lookup = HashMap::<TrackKey, usize>::new();
    let mut last_key: Option<TrackKey> = None;
    let mut last_track_index: usize = 0;
    let mut minimum = 0;
    let mut maximum = 0;
    let mut have_range = false;

    // Compact index array to store assigned track index per event (u16::MAX for metadata).
    let mut event_track_ids = vec![u16::MAX; data.events.len()];
    let mut counts = Vec::<usize>::new();

    for (event_index, event) in data.events.iter().enumerate() {
        let counter = event.phase == counter_phase && counter_phase != StringId(0);
        let metadata = event.phase == metadata_phase && metadata_phase != StringId(0);
        let key = if counter {
            TrackKey {
                process_id: event.process_id,
                thread_id: -1,
                name: event.name,
                id: event.id,
            }
        } else {
            TrackKey {
                process_id: event.process_id,
                thread_id: event.thread_id,
                name: StringId(0),
                id: StringId(0),
            }
        };

        let track_index = if last_key == Some(key) {
            last_track_index
        } else {
            let index = *lookup.entry(key).or_insert_with(|| {
                let idx = tracks.len();
                tracks.push(Track::new(
                    if counter {
                        TrackType::Counter
                    } else {
                        TrackType::Thread
                    },
                    key.process_id,
                    key.thread_id,
                    key.name,
                    key.id,
                ));
                counts.push(0);
                idx
            });
            last_key = Some(key);
            last_track_index = index;
            index
        };

        let track = &mut tracks[track_index];
        if metadata {
            match data.string(event.name) {
                b"thread_name" => {
                    if let Some(arg) = data
                        .event_args(event)
                        .iter()
                        .find(|arg| data.string(arg.key) == b"name")
                    {
                        track.name = arg.value;
                    }
                }
                b"thread_sort_index" => {
                    if let Some(arg) = data
                        .event_args(event)
                        .iter()
                        .find(|arg| data.string(arg.key) == b"sort_index")
                    {
                        track.sort_index = parse_i32_prefix(data.string(arg.value));
                    }
                }
                _ => {}
            }
        } else {
            event_track_ids[event_index] = track_index as u16;
            counts[track_index] += 1;
            if !have_range {
                minimum = event.timestamp;
                maximum = event.timestamp.saturating_add(event.duration);
                have_range = true;
            } else {
                minimum = minimum.min(event.timestamp);
                maximum = maximum.max(event.timestamp.saturating_add(event.duration));
            }
        }
    }

    // Pre-reserve exact capacity for every track's event_indices vector (eliminates reallocations)
    for (track, count) in tracks.iter_mut().zip(counts) {
        track.event_indices.reserve(count);
    }

    // Pass 2: Grouping into contiguous pre-reserved vectors
    for (event_index, &track_id) in event_track_ids.iter().enumerate() {
        if track_id != u16::MAX {
            tracks[track_id as usize].event_indices.push(event_index);
        }
    }
    for track in &mut tracks {
        track.finish(data);
    }
    tracks.sort_by(|a, b| compare_tracks(a, b, data));
    (tracks, minimum, maximum)
}

fn parse_i32_prefix(bytes: &[u8]) -> i32 {
    let mut index = 0;
    let mut sign = 1_i32;
    if bytes.first() == Some(&b'-') {
        sign = -1;
        index = 1;
    } else if bytes.first() == Some(&b'+') {
        index = 1;
    }
    let mut value = 0_i32;
    while let Some(b'0'..=b'9') = bytes.get(index) {
        value = value
            .wrapping_mul(10)
            .wrapping_add(i32::from(bytes[index] - b'0'));
        index += 1;
    }
    value.wrapping_mul(sign)
}

fn ascii_case_cmp(a: &[u8], b: &[u8]) -> Ordering {
    a.iter()
        .map(u8::to_ascii_lowercase)
        .cmp(b.iter().map(u8::to_ascii_lowercase))
}

fn compare_tracks(a: &Track, b: &Track, data: &TraceData) -> Ordering {
    a.sort_index
        .cmp(&b.sort_index)
        .then_with(|| a.process_id.cmp(&b.process_id))
        .then_with(|| a.kind.cmp(&b.kind))
        .then_with(|| {
            if a.kind == TrackType::Counter {
                ascii_case_cmp(data.string(a.name), data.string(b.name))
                    .then_with(|| data.string(a.id).cmp(data.string(b.id)))
            } else {
                a.thread_id
                    .cmp(&b.thread_id)
                    .then_with(|| a.name.cmp(&b.name))
                    .then_with(|| a.id.cmp(&b.id))
            }
        })
}

#[cfg(test)]
mod tests {
    use super::{BLOCK_SIZE, Track, TrackType, organize_tracks};
    use crate::trace::data::{EventMatcher, PersistedEvent, TraceData};
    use crate::trace::parser::{TraceArg, TraceEvent};

    fn data_events(values: &[(i64, i64)]) -> TraceData {
        let mut data = TraceData::new();
        data.events = values
            .iter()
            .map(|&(timestamp, duration)| PersistedEvent {
                timestamp,
                duration,
                ..PersistedEvent::default()
            })
            .collect();
        data
    }
    fn add(
        data: &mut TraceData,
        phase: &[u8],
        name: &[u8],
        id: &[u8],
        pid: i32,
        tid: i32,
        ts: i64,
        dur: i64,
        args: Vec<TraceArg<'_>>,
    ) {
        let mut matcher = EventMatcher::default();
        data.add_event(
            &TraceEvent {
                phase,
                name,
                id,
                process_id: pid,
                thread_id: tid,
                timestamp: ts,
                duration: dur,
                args,
                ..TraceEvent::default()
            },
            &mut matcher,
        )
    }
    fn string_arg<'a>(key: &'a [u8], value: &'a [u8]) -> TraceArg<'a> {
        TraceArg {
            key,
            value,
            number: 0.0,
        }
    }

    #[test]
    fn sort_events() {
        let data = data_events(&[(200, 0), (100, 0)]);
        let mut track = Track::thread(0, 0);
        track.event_indices = vec![0, 1];
        track.sort_events(&data);
        assert_eq!(track.event_indices, [1, 0]);
    }

    #[test]
    fn sort_events_breaks_timestamp_ties_by_duration_then_original_index() {
        let data = data_events(&[(100, 10), (100, 20), (100, 20)]);
        let mut track = Track::thread(0, 0);
        track.event_indices = vec![0, 2, 1];
        track.sort_events(&data);
        assert_eq!(track.event_indices, [1, 2, 0]);
    }

    #[test]
    fn update_max_dur() {
        let data = data_events(&[(0, 100), (0, 500)]);
        let mut track = Track::thread(0, 0);
        track.event_indices = vec![0, 1];
        track.update_max_duration(&data);
        assert_eq!(track.max_duration, 500);
    }
    #[test]
    fn find_visible_start_index() {
        let data = data_events(&[(0, 100), (200, 100), (400, 600), (1200, 100)]);
        let mut track = Track::thread(0, 0);
        track.event_indices = (0..4).collect();
        track.sort_events(&data);
        track.update_max_duration(&data);
        assert_eq!(track.visible_start(&data, 50), 0);
        assert_eq!(track.visible_start(&data, 150), 0);
        assert_eq!(track.visible_start(&data, 800), 1);
        assert_eq!(track.visible_start(&data, 1100), 3);
    }

    #[test]
    fn visible_start_skips_completed_blocks() {
        let mut data = TraceData::new();
        data.events = (0..=BLOCK_SIZE)
            .map(|index| PersistedEvent {
                timestamp: i64::try_from(index).unwrap() * 10,
                duration: if index == BLOCK_SIZE { 500 } else { 1 },
                ..PersistedEvent::default()
            })
            .collect();
        let mut track = Track::thread(0, 0);
        track.event_indices = (0..=BLOCK_SIZE).collect();
        track.update_max_duration(&data);

        assert_eq!(track.block_max_durations, [1, 500]);
        assert_eq!(track.visible_start(&data, 10_240), BLOCK_SIZE);
    }

    #[test]
    fn calculate_depths() {
        let data = data_events(&[(0, 100), (10, 40), (10, 10), (60, 30), (110, 10)]);
        let mut track = Track::thread(0, 0);
        track.event_indices = vec![4, 2, 0, 3, 1];
        track.sort_events(&data);
        track.calculate_depths(&data);
        assert_eq!(track.event_indices, [0, 1, 2, 3, 4]);
        assert_eq!(track.depths, [0, 1, 2, 1, 0]);
        assert_eq!(track.max_depth, 2);
        assert_eq!(track.self_durations, [30, 30, 10, 30, 10]);
    }
    #[test]
    fn calculate_depths_siblings() {
        let data = data_events(&[(0, 100), (10, 40), (50, 50)]);
        let mut track = Track::thread(0, 0);
        track.event_indices = vec![0, 1, 2];
        track.sort_events(&data);
        track.calculate_depths(&data);
        assert_eq!(track.depths, [0, 1, 1]);
        assert_eq!(track.self_durations, [10, 40, 50]);
    }
    #[test]
    fn calculate_depths_duplicates() {
        let data = data_events(&[(0, 100), (0, 100)]);
        let mut track = Track::thread(0, 0);
        track.event_indices = vec![0, 1];
        track.sort_events(&data);
        track.calculate_depths(&data);
        assert_eq!(track.depths, [0, 1]);
        assert_eq!(track.self_durations, [0, 100]);
    }
    #[test]
    fn calculate_depths_non_strict() {
        let data = data_events(&[(0, 100), (10, 100), (105, 15)]);
        let mut track = Track::thread(0, 0);
        track.event_indices = vec![0, 1, 2];
        track.sort_events(&data);
        track.calculate_depths(&data);
        assert_eq!(track.depths, [0, 0, 0]);
        assert_eq!(track.self_durations, [100, 100, 15]);
    }
    #[test]
    fn organize_tracks_empty() {
        let (tracks, min, max) = organize_tracks(&TraceData::new());
        assert!(tracks.is_empty());
        assert_eq!((min, max), (0, 0));
    }

    #[test]
    fn missing_phase_is_a_thread_event() {
        let mut data = TraceData::new();
        add(&mut data, b"", b"event", b"", 1, 2, 100, 25, vec![]);

        let (tracks, min, max) = organize_tracks(&data);

        assert_eq!(tracks.len(), 1);
        assert_eq!(tracks[0].kind, TrackType::Thread);
        assert_eq!(tracks[0].event_indices, [0]);
        assert_eq!((min, max), (100, 125));
    }

    #[test]
    fn organize_tracks_sorting() {
        let mut data = TraceData::new();
        add(&mut data, b"X", b"", b"", 10, 1, 100, 10, vec![]);
        add(&mut data, b"X", b"", b"", 1, 2, 100, 10, vec![]);
        add(&mut data, b"X", b"", b"", 1, 1, 100, 10, vec![]);
        add(
            &mut data,
            b"M",
            b"thread_sort_index",
            b"",
            10,
            1,
            0,
            0,
            vec![string_arg(b"sort_index", b"-5")],
        );
        add(
            &mut data,
            b"M",
            b"thread_sort_index",
            b"",
            1,
            2,
            0,
            0,
            vec![string_arg(b"sort_index", b"5")],
        );
        let (tracks, _, _) = organize_tracks(&data);
        assert_eq!(tracks.len(), 3);
        assert_eq!((tracks[0].process_id, tracks[0].sort_index), (10, -5));
        assert_eq!(
            (
                tracks[1].process_id,
                tracks[1].thread_id,
                tracks[1].sort_index
            ),
            (1, 1, 0)
        );
        assert_eq!(
            (
                tracks[2].process_id,
                tracks[2].thread_id,
                tracks[2].sort_index
            ),
            (1, 2, 5)
        );
    }
    #[test]
    fn organize_tracks_metadata_only() {
        let mut data = TraceData::new();
        add(
            &mut data,
            b"M",
            b"thread_name",
            b"",
            1,
            1,
            0,
            0,
            vec![string_arg(b"name", b"Meta Only")],
        );
        let (tracks, min, max) = organize_tracks(&data);
        assert_eq!(tracks.len(), 1);
        assert_eq!(data.string(tracks[0].name), b"Meta Only");
        assert!(tracks[0].event_indices.is_empty());
        assert_eq!((min, max), (0, 0));
    }

    #[test]
    fn repeated_and_malformed_metadata_use_last_or_parsed_prefix() {
        let mut data = TraceData::new();
        add(&mut data, b"X", b"", b"", 1, 1, 100, 10, vec![]);
        add(
            &mut data,
            b"M",
            b"thread_name",
            b"",
            1,
            1,
            0,
            0,
            vec![string_arg(b"name", b"First")],
        );
        add(
            &mut data,
            b"M",
            b"thread_name",
            b"",
            1,
            1,
            0,
            0,
            vec![string_arg(b"name", b"Last")],
        );
        add(
            &mut data,
            b"M",
            b"thread_sort_index",
            b"",
            1,
            1,
            0,
            0,
            vec![string_arg(b"sort_index", b"-12trailing")],
        );
        add(&mut data, b"X", b"", b"", 2, 1, 100, 10, vec![]);
        add(
            &mut data,
            b"M",
            b"thread_sort_index",
            b"",
            2,
            1,
            0,
            0,
            vec![string_arg(b"sort_index", b"invalid")],
        );

        let (tracks, _, _) = organize_tracks(&data);
        let first = tracks.iter().find(|track| track.process_id == 1).unwrap();
        let second = tracks.iter().find(|track| track.process_id == 2).unwrap();

        assert_eq!(data.string(first.name), b"Last");
        assert_eq!(first.sort_index, -12);
        assert_eq!(second.sort_index, 0);
    }

    #[test]
    fn organize_tracks_mixed_order() {
        let mut data = TraceData::new();
        add(&mut data, b"X", b"", b"", 1, 1, 500, 100, vec![]);
        add(
            &mut data,
            b"M",
            b"thread_name",
            b"",
            1,
            1,
            0,
            0,
            vec![string_arg(b"name", b"Mixed")],
        );
        add(&mut data, b"X", b"", b"", 2, 1, 100, 50, vec![]);
        let (tracks, min, max) = organize_tracks(&data);
        assert_eq!(tracks.len(), 2);
        assert_eq!(
            (
                tracks[0].process_id,
                data.string(tracks[0].name),
                &tracks[0].event_indices[..]
            ),
            (1, b"Mixed".as_slice(), [0].as_slice())
        );
        assert_eq!(
            (tracks[1].process_id, &tracks[1].event_indices[..]),
            (2, [2].as_slice())
        );
        assert_eq!((min, max), (100, 600));
    }
    #[test]
    fn organize_tracks_counters() {
        let mut data = TraceData::new();
        add(&mut data, b"X", b"", b"", 1, 1, 100, 0, vec![]);
        add(
            &mut data,
            b"C",
            b"my_counter",
            b"",
            1,
            1,
            150,
            0,
            vec![TraceArg {
                key: b"val",
                value: b"10",
                number: 10.0,
            }],
        );
        add(&mut data, b"C", b"my_counter", b"1", 1, 0, 200, 0, vec![]);
        let (tracks, _, _) = organize_tracks(&data);
        assert_eq!(tracks.len(), 3);
        assert_eq!(tracks[0].kind, TrackType::Counter);
        assert_eq!(
            (
                tracks[0].thread_id,
                data.string(tracks[0].name),
                tracks[0].id
            ),
            (-1, b"my_counter".as_slice(), Default::default())
        );
        assert_eq!(data.string(tracks[1].id), b"1");
        assert_eq!(tracks[2].kind, TrackType::Thread);
        assert_eq!(tracks[0].self_durations, [0]);
        assert_eq!(tracks[1].self_durations, [0]);
    }

    #[test]
    fn counter_and_event_names_use_the_same_cached_palette_hash() {
        let mut data = TraceData::new();
        add(&mut data, b"X", b"foo", b"", 1, 1, 100, 0, vec![]);
        add(
            &mut data,
            b"C",
            b"counter",
            b"",
            1,
            1,
            150,
            0,
            vec![TraceArg {
                key: b"foo",
                value: b"10",
                number: 10.0,
            }],
        );

        let event_palette_index = data.events[0].palette_index;
        let (tracks, _, _) = organize_tracks(&data);
        let counter = tracks
            .iter()
            .find(|track| track.kind == TrackType::Counter)
            .unwrap();

        assert_eq!(counter.counter_palette_indices, [event_palette_index]);
    }

    #[test]
    fn counter_series_are_deduplicated_sorted_and_summarized() {
        let mut data = TraceData::new();
        add(
            &mut data,
            b"C",
            b"counter",
            b"",
            1,
            1,
            100,
            0,
            vec![
                TraceArg {
                    key: b"z",
                    value: b"2",
                    number: 2.0,
                },
                TraceArg {
                    key: b"a",
                    value: b"3",
                    number: 3.0,
                },
            ],
        );
        add(
            &mut data,
            b"C",
            b"counter",
            b"",
            1,
            2,
            200,
            0,
            vec![TraceArg {
                key: b"z",
                value: b"10",
                number: 10.0,
            }],
        );

        let (tracks, _, _) = organize_tracks(&data);
        let counter = &tracks[0];

        assert_eq!(
            counter
                .counter_series
                .iter()
                .map(|id| data.string(*id))
                .collect::<Vec<_>>(),
            [b"a".as_slice(), b"z".as_slice()]
        );
        assert_eq!(counter.counter_max_total, 10.0);
        assert_eq!(
            counter.counter_palette_indices,
            counter
                .counter_series
                .iter()
                .map(|id| (data.string_hash(*id) % 8) as u8)
                .collect::<Vec<_>>()
        );
    }

    #[test]
    fn timestamp_arithmetic_saturates_at_i64_boundaries() {
        let mut data = TraceData::new();
        add(
            &mut data,
            b"X",
            b"maximum",
            b"",
            1,
            1,
            i64::MAX - 5,
            10,
            vec![],
        );
        add(
            &mut data,
            b"X",
            b"minimum",
            b"",
            1,
            1,
            i64::MIN + 5,
            -10,
            vec![],
        );

        let (tracks, min, max) = organize_tracks(&data);

        assert_eq!((min, max), (i64::MIN + 5, i64::MAX));
        assert_eq!(tracks[0].visible_start(&data, i64::MIN), 0);
    }

    fn counter_order(values: &[(&[u8], &[u8])]) -> (TraceData, Vec<Track>) {
        let mut data = TraceData::new();
        for &(name, id) in values {
            add(&mut data, b"C", name, id, 1, 0, 0, 0, vec![])
        }
        let (tracks, _, _) = organize_tracks(&data);
        (data, tracks)
    }
    #[test]
    fn organize_tracks_counters_sorting() {
        let (data, tracks) = counter_order(&[
            (b"zebra", b""),
            (b"apple", b""),
            (b"apple", b"2"),
            (b"apple", b"1"),
        ]);
        let values: Vec<_> = tracks
            .iter()
            .map(|t| (data.string(t.name), data.string(t.id)))
            .collect();
        assert_eq!(
            values,
            [
                (b"apple".as_slice(), b"".as_slice()),
                (b"apple".as_slice(), b"1".as_slice()),
                (b"apple".as_slice(), b"2".as_slice()),
                (b"zebra".as_slice(), b"".as_slice())
            ]
        );
    }
    #[test]
    fn organize_tracks_counters_sorting_ignore_case() {
        let (data, tracks) = counter_order(&[(b"zebra", b""), (b"APPLE", b""), (b"apple", b"1")]);
        let values: Vec<_> = tracks.iter().map(|t| data.string(t.name)).collect();
        assert_eq!(
            values,
            [
                b"APPLE".as_slice(),
                b"apple".as_slice(),
                b"zebra".as_slice()
            ]
        );
    }
}
