use crate::string_interner::StringId;
use crate::trace::{TraceData, Track};
use std::num::NonZeroUsize;

const MIN_EVENT_WIDTH: f32 = 3.0;

#[derive(Clone, Debug, PartialEq)]
pub struct RenderBlock {
    pub x1: f32,
    pub x2: f32,
    pub palette_index: u8,
    pub name: StringId,
    pub depth: u32,
    pub count: u32,
    pub selected: bool,
    pub focused: bool,
    pub event_index: usize,
}

#[derive(Clone, Debug, PartialEq)]
pub struct CounterBlock {
    pub x1: f32,
    pub x2: f32,
    pub selected: bool,
    pub focused: bool,
    event_index: Option<NonZeroUsize>,
}

impl CounterBlock {
    pub fn event_index(&self) -> Option<usize> {
        self.event_index.map(|index| index.get() - 1)
    }
}

#[derive(Clone, Copy, Default)]
pub struct CounterBlocks<'a> {
    blocks: &'a [CounterBlock],
    peaks: &'a [f64],
    series_count: usize,
}

impl CounterBlocks<'_> {
    pub fn peaks(&self, block_index: usize) -> &[f64] {
        let start = block_index * self.series_count;
        &self.peaks[start..start + self.series_count]
    }
}

impl std::ops::Deref for CounterBlocks<'_> {
    type Target = [CounterBlock];

    fn deref(&self) -> &Self::Target {
        &self.blocks
    }
}

#[derive(Clone, Copy, Default)]
struct ThreadBucketRepresentative {
    event_index: usize,
    maximum_duration: i64,
    count: u32,
    present: bool,
}

#[derive(Default)]
pub struct State {
    selected: Vec<bool>,
    thread_blocks: Vec<RenderBlock>,
    thread_blocked_until: Vec<i64>,
    thread_bucket_representatives: Vec<ThreadBucketRepresentative>,
    counter_blocks: Vec<CounterBlock>,
    counter_peaks: Vec<f64>,
    counter_values: Vec<f64>,
    counter_bucket_peaks: Vec<f64>,
    counter_updated: Vec<bool>,
}

impl State {
    pub fn update_selection(&mut self, event_count: usize, selected: &[i64]) {
        self.selected.clear();
        self.selected.resize(event_count, false);
        for &index in selected {
            if let Ok(index) = usize::try_from(index)
                && let Some(value) = self.selected.get_mut(index)
            {
                *value = true;
            }
        }
    }

    pub fn thread_blocks(
        &mut self,
        track: &Track,
        data: &TraceData,
        viewport_start: f64,
        viewport_end: f64,
        width: f32,
        canvas_x: f32,
        focused: Option<usize>,
    ) -> &[RenderBlock] {
        self.thread_blocks.clear();
        let duration = viewport_end - viewport_start;
        if track.event_indices.is_empty() || duration <= 0.0 || width <= 0.0 {
            return &self.thread_blocks;
        }
        let scale = f64::from(width) / duration;
        let bucket_duration = f64::from(MIN_EVENT_WIDTH) / scale;
        let mut bucket_start = (viewport_start / bucket_duration).floor() * bucket_duration;
        self.thread_blocked_until.clear();
        self.thread_blocked_until
            .resize(track.max_depth as usize + 1, -1);
        let selected = &self.selected;
        let blocked_until = &mut self.thread_blocked_until;
        let output = &mut self.thread_blocks;

        // Events that begin before the first stable bucket can still span the viewport.
        if track.block_max_durations.is_empty() {
            Self::collect_spanning_events(
                selected,
                track,
                data,
                0..track.event_indices.len(),
                bucket_start,
                viewport_start,
                scale,
                canvas_x,
                focused,
                blocked_until,
                output,
            );
        } else {
            for (block, &maximum_duration) in track.block_max_durations.iter().enumerate() {
                let start = block * crate::trace::track::BLOCK_SIZE;
                let end = (start + crate::trace::track::BLOCK_SIZE).min(track.event_indices.len());
                if data.events[track.event_indices[start]].timestamp as f64 >= bucket_start {
                    break;
                }
                let last_timestamp = data.events[track.event_indices[end - 1]].timestamp;
                if last_timestamp.saturating_add(maximum_duration) <= viewport_start as i64 {
                    continue;
                }
                Self::collect_spanning_events(
                    selected,
                    track,
                    data,
                    start..end,
                    bucket_start,
                    viewport_start,
                    scale,
                    canvas_x,
                    focused,
                    blocked_until,
                    output,
                );
            }
        }

        let mut position = track
            .event_indices
            .partition_point(|&index| data.events[index].timestamp < bucket_start as i64);
        self.thread_bucket_representatives.clear();
        self.thread_bucket_representatives
            .resize(blocked_until.len(), ThreadBucketRepresentative::default());
        let representatives = &mut self.thread_bucket_representatives;
        while bucket_start < viewport_end {
            let bucket_end = bucket_start + bucket_duration;
            representatives.fill(ThreadBucketRepresentative::default());
            while let Some(&index) = track.event_indices.get(position) {
                let event = &data.events[index];
                if event.timestamp as f64 >= bucket_end {
                    break;
                }
                let depth = track.depths[position] as usize;
                let is_selected = selected.get(index).copied().unwrap_or(false);
                let is_focused = focused == Some(index);
                let large = event.duration as f64 * scale >= f64::from(MIN_EVENT_WIDTH) - 0.01;
                if is_selected || is_focused || large {
                    flush_representative(
                        output,
                        &mut representatives[depth],
                        depth as u32,
                        data,
                        viewport_start,
                        scale,
                        canvas_x,
                        bucket_start,
                        bucket_end,
                    );
                    output.push(Self::event_block(
                        selected,
                        event,
                        index,
                        depth as u32,
                        viewport_start,
                        scale,
                        canvas_x,
                        focused,
                    ));
                    blocked_until[depth] =
                        blocked_until[depth].max(event.timestamp.saturating_add(event.duration));
                } else if blocked_until[depth] < bucket_end as i64 {
                    let representative = &mut representatives[depth];
                    if !representative.present {
                        *representative = ThreadBucketRepresentative {
                            event_index: index,
                            maximum_duration: event.duration,
                            count: 1,
                            present: true,
                        }
                    } else {
                        representative.count += 1;
                        if event.duration > representative.maximum_duration {
                            representative.event_index = index;
                            representative.maximum_duration = event.duration;
                        }
                    }
                }
                position += 1;
            }
            for (depth, representative) in representatives.iter_mut().enumerate() {
                flush_representative(
                    output,
                    representative,
                    depth as u32,
                    data,
                    viewport_start,
                    scale,
                    canvas_x,
                    bucket_start,
                    bucket_end,
                );
            }
            bucket_start = bucket_end;
        }
        if output.len() > 1 {
            let mut write = 0;
            for read in 1..output.len() {
                let block = output[read].clone();
                let previous = &mut output[write];
                if !previous.selected
                    && !previous.focused
                    && !block.selected
                    && !block.focused
                    && previous.depth == block.depth
                    && previous.event_index == block.event_index
                {
                    previous.x2 = block.x2;
                    previous.count += block.count;
                } else {
                    write += 1;
                    output[write] = block;
                }
            }
            output.truncate(write + 1);
        }
        output
    }

    #[allow(clippy::too_many_arguments)]
    fn collect_spanning_events(
        selected: &[bool],
        track: &Track,
        data: &TraceData,
        positions: std::ops::Range<usize>,
        bucket_start: f64,
        viewport_start: f64,
        scale: f64,
        canvas_x: f32,
        focused: Option<usize>,
        blocked_until: &mut [i64],
        output: &mut Vec<RenderBlock>,
    ) {
        for position in positions {
            let index = track.event_indices[position];
            let event = &data.events[index];
            if event.timestamp as f64 >= bucket_start {
                break;
            }
            if event.timestamp.saturating_add(event.duration) <= viewport_start as i64 {
                continue;
            }
            let depth = track.depths[position];
            output.push(Self::event_block(
                selected,
                event,
                index,
                depth,
                viewport_start,
                scale,
                canvas_x,
                focused,
            ));
            blocked_until[depth as usize] =
                blocked_until[depth as usize].max(event.timestamp.saturating_add(event.duration));
        }
    }

    fn event_block(
        selected: &[bool],
        event: &crate::trace::data::PersistedEvent,
        index: usize,
        depth: u32,
        start: f64,
        scale: f64,
        canvas_x: f32,
        focused: Option<usize>,
    ) -> RenderBlock {
        let x1 = canvas_x + ((event.timestamp as f64 - start) * scale) as f32;
        let x2 = (x1 + (event.duration as f64 * scale) as f32).max(x1 + MIN_EVENT_WIDTH);
        RenderBlock {
            x1,
            x2,
            palette_index: event.palette_index,
            name: event.name,
            depth,
            count: 1,
            selected: selected.get(index).copied().unwrap_or(false),
            focused: focused == Some(index),
            event_index: index,
        }
    }

    pub fn counter_blocks(
        &mut self,
        track: &Track,
        data: &TraceData,
        viewport_start: f64,
        viewport_end: f64,
        width: f32,
        canvas_x: f32,
        focused: Option<usize>,
    ) -> CounterBlocks<'_> {
        self.counter_blocks.clear();
        self.counter_peaks.clear();
        if track.event_indices.is_empty() || viewport_end <= viewport_start {
            return CounterBlocks {
                blocks: &self.counter_blocks,
                peaks: &self.counter_peaks,
                series_count: track.counter_series.len(),
            };
        }
        let first = data.events[*track.event_indices.first().unwrap()].timestamp as f64;
        let last = data.events[*track.event_indices.last().unwrap()].timestamp as f64;
        if viewport_end <= first || viewport_start >= last {
            return CounterBlocks {
                blocks: &self.counter_blocks,
                peaks: &self.counter_peaks,
                series_count: track.counter_series.len(),
            };
        }
        let scale = f64::from(width) / (viewport_end - viewport_start);
        let bucket_duration = 3.0 / scale;
        let mut bucket_start = (viewport_start / bucket_duration).floor() * bucket_duration;
        if bucket_start < first {
            bucket_start = (first / bucket_duration).floor() * bucket_duration;
        }
        let mut position = track
            .event_indices
            .partition_point(|&index| data.events[index].timestamp < bucket_start as i64);
        self.counter_values.clear();
        self.counter_values.resize(track.counter_series.len(), 0.0);
        if position > 0 {
            apply_counter(
                &mut self.counter_values,
                track,
                data,
                &data.events[track.event_indices[position - 1]],
            );
        }
        self.counter_bucket_peaks.clear();
        self.counter_bucket_peaks
            .resize(self.counter_values.len(), 0.0);
        self.counter_updated.clear();
        self.counter_updated
            .resize(self.counter_values.len(), false);
        let selected_events = &self.selected;
        let output = &mut self.counter_blocks;
        let output_peaks = &mut self.counter_peaks;
        let peaks = &mut self.counter_bucket_peaks;
        let updated = &mut self.counter_updated;
        while bucket_start < viewport_end {
            let bucket_end = bucket_start + bucket_duration;
            peaks.clone_from_slice(&self.counter_values);
            updated.fill(false);
            let mut last_index = None;
            let mut selected = false;
            let mut is_focused = false;
            while let Some(&index) = track.event_indices.get(position) {
                let event = &data.events[index];
                if event.timestamp as f64 >= bucket_end {
                    break;
                }
                last_index = Some(index);
                selected |= selected_events.get(index).copied().unwrap_or(false);
                is_focused |= focused == Some(index);
                for arg in data.event_args(event) {
                    if let Some(series) =
                        track.counter_series.iter().position(|key| *key == arg.key)
                    {
                        self.counter_values[series] = arg.number;
                        if updated[series] {
                            peaks[series] = peaks[series].max(arg.number)
                        } else {
                            peaks[series] = arg.number;
                            updated[series] = true
                        }
                    }
                }
                position += 1;
            }
            let draw_start = bucket_start.max(first);
            let draw_end = bucket_end.min(last);
            let x1 = (canvas_x + ((draw_start - viewport_start) * scale) as f32).max(canvas_x);
            let x2 =
                (canvas_x + ((draw_end - viewport_start) * scale) as f32).min(canvas_x + width);
            if x2 > x1 {
                let representative =
                    last_index.or_else(|| position.checked_sub(1).map(|p| track.event_indices[p]));
                let can_merge = output.last().is_some_and(|previous| {
                    previous.event_index == encode_event_index(last_index)
                        && previous.selected == selected
                        && previous.focused == is_focused
                        && output_peaks[output_peaks.len() - peaks.len()..] == *peaks
                });
                if can_merge {
                    output.last_mut().unwrap().x2 = x2
                } else {
                    output_peaks.extend_from_slice(peaks);
                    output.push(CounterBlock {
                        x1,
                        x2,
                        selected,
                        focused: is_focused,
                        event_index: encode_event_index(representative),
                    })
                }
            }
            if draw_end >= last {
                break;
            }
            bucket_start = bucket_end;
        }
        CounterBlocks {
            blocks: output,
            peaks: output_peaks,
            series_count: track.counter_series.len(),
        }
    }
}

fn encode_event_index(index: Option<usize>) -> Option<NonZeroUsize> {
    index.map(|index| NonZeroUsize::new(index + 1).unwrap())
}

fn flush_representative(
    output: &mut Vec<RenderBlock>,
    representative: &mut ThreadBucketRepresentative,
    depth: u32,
    data: &TraceData,
    viewport_start: f64,
    scale: f64,
    canvas_x: f32,
    start: f64,
    end: f64,
) {
    if representative.present {
        let index = representative.event_index;
        let event = &data.events[index];
        output.push(RenderBlock {
            x1: canvas_x + ((start - viewport_start) * scale) as f32,
            x2: canvas_x + ((end - viewport_start) * scale) as f32,
            palette_index: event.palette_index,
            name: event.name,
            depth,
            count: representative.count,
            selected: false,
            focused: false,
            event_index: index,
        });
        representative.present = false
    }
}

fn apply_counter(
    values: &mut [f64],
    track: &Track,
    data: &TraceData,
    event: &crate::trace::data::PersistedEvent,
) {
    for arg in data.event_args(event) {
        if let Some(index) = track.counter_series.iter().position(|key| *key == arg.key) {
            values[index] = arg.number
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::trace::{PersistedArg, PersistedEvent, TraceData, Track, TrackType};

    fn add_event(data: &mut TraceData, ts: i64, dur: i64, name: &str, palette_index: u8) -> usize {
        let name_ref = data.intern(name.as_bytes());
        let idx = data.events.len();
        data.events.push(PersistedEvent {
            timestamp: ts,
            duration: dur,
            name: name_ref,
            palette_index,
            ..Default::default()
        });
        idx
    }

    fn add_counter_event(
        data: &mut TraceData,
        timestamp: i64,
        series: Option<(StringId, f64)>,
    ) -> usize {
        let args_offset = data.args.len() as u32;
        if let Some((key, number)) = series {
            data.args.push(PersistedArg {
                key,
                number,
                ..Default::default()
            });
        }
        let index = data.events.len();
        data.events.push(PersistedEvent {
            timestamp,
            args_offset,
            args_count: u32::from(series.is_some()),
            ..Default::default()
        });
        index
    }

    fn counter_track(event_indices: Vec<usize>, series: Vec<StringId>) -> Track {
        Track {
            kind: TrackType::Counter,
            event_indices,
            counter_series: series,
            ..Default::default()
        }
    }

    #[test]
    fn coalesce_same_color() {
        let mut data = TraceData::new();
        let e1 = add_event(&mut data, 100, 5, "e", 0);
        let e2 = add_event(&mut data, 105, 5, "e", 0);
        let e3 = add_event(&mut data, 110, 5, "e", 0);

        let track = Track {
            kind: TrackType::Thread,
            event_indices: vec![e1, e2, e3],
            depths: vec![0, 0, 0],
            max_depth: 0,
            ..Default::default()
        };

        let mut state = State::default();
        let blocks = state.thread_blocks(&track, &data, 0.0, 10000.0, 1000.0, 0.0, None);

        assert_eq!(blocks.len(), 1);
        assert!((blocks[0].x1 - 9.0).abs() < 1e-3);
        assert!((blocks[0].x2 - 12.0).abs() < 1e-3);
    }

    #[test]
    fn thread_bucketing_stability() {
        let mut data = TraceData::new();
        let e0 = add_event(&mut data, 85, 20, "e0", 0);
        let e1 = add_event(&mut data, 100, 1, "e1", 0);
        let e2 = add_event(&mut data, 105, 1, "e2", 0);
        let e3 = add_event(&mut data, 110, 1, "e3", 0);

        let track = Track {
            kind: TrackType::Thread,
            event_indices: vec![e0, e1, e2, e3],
            depths: vec![0, 0, 0, 0],
            max_depth: 0,
            ..Default::default()
        };

        let mut state = State::default();
        let blocks_a = state.thread_blocks(&track, &data, 0.0, 10000.0, 1000.0, 0.0, None);
        let count_90_120 = blocks_a
            .iter()
            .find(|b| (b.x1 - 9.0).abs() < 1e-3)
            .map(|b| b.count)
            .unwrap_or(0);
        assert_eq!(count_90_120, 3);

        let blocks_b = state.thread_blocks(&track, &data, 95.0, 10095.0, 1000.0, 0.0, None);
        let count_90_120_panned = blocks_b
            .iter()
            .find(|b| (b.x1 - (-0.5)).abs() < 1e-3)
            .map(|b| b.count)
            .unwrap_or(0);
        assert_eq!(count_90_120_panned, 3);
    }

    #[test]
    fn coalesce_different_colors() {
        let mut data = TraceData::new();
        let e1 = add_event(&mut data, 100, 5, "e1", 0);
        let e2 = add_event(&mut data, 105, 5, "e2", 1);

        let track = Track {
            kind: TrackType::Thread,
            event_indices: vec![e1, e2],
            depths: vec![0, 0],
            max_depth: 0,
            ..Default::default()
        };

        let mut state = State::default();
        let blocks = state.thread_blocks(&track, &data, 0.0, 10000.0, 1000.0, 0.0, None);
        assert_eq!(blocks.len(), 1);
    }

    #[test]
    fn multiple_blocks_close_together() {
        let mut data = TraceData::new();
        let e1 = add_event(&mut data, 100, 5, "e1", 0);
        let e2 = add_event(&mut data, 111, 5, "e2", 0);

        let track = Track {
            kind: TrackType::Thread,
            event_indices: vec![e1, e2],
            depths: vec![0, 0],
            max_depth: 0,
            ..Default::default()
        };

        let mut state = State::default();
        let blocks = state.thread_blocks(&track, &data, 0.0, 10000.0, 1000.0, 0.0, None);
        assert_eq!(blocks.len(), 1);
    }

    #[test]
    fn culling_after_merge_flush() {
        let mut data = TraceData::new();
        let e1 = add_event(&mut data, 100, 1, "e1", 0);
        let e2 = add_event(&mut data, 101, 1, "e2", 0);
        let e3 = add_event(&mut data, 130, 1, "e3", 0);
        let e4 = add_event(&mut data, 131, 1, "e4", 0);
        let e5 = add_event(&mut data, 132, 1, "e5", 0);

        let track = Track {
            kind: TrackType::Thread,
            event_indices: vec![e1, e2, e3, e4, e5],
            depths: vec![0, 0, 0, 0, 0],
            max_depth: 0,
            ..Default::default()
        };

        let mut state = State::default();
        let blocks = state.thread_blocks(&track, &data, 0.0, 10000.0, 1000.0, 0.0, None);
        assert_eq!(blocks.len(), 2);
    }

    #[test]
    fn selected_event_never_skipped() {
        let mut data = TraceData::new();
        let e1 = add_event(&mut data, 100, 10, "e1", 0);
        let e2 = add_event(&mut data, 101, 10, "e2", 0);

        let track = Track {
            kind: TrackType::Thread,
            event_indices: vec![e1, e2],
            depths: vec![0, 0],
            max_depth: 0,
            ..Default::default()
        };

        let mut state = State::default();
        state.update_selection(data.events.len(), &[1]);
        let blocks = state.thread_blocks(&track, &data, 0.0, 10000.0, 1000.0, 0.0, None);
        assert_eq!(blocks.len(), 2);
        assert!(blocks[1].selected);
    }

    #[test]
    fn same_lane_overlap() {
        let mut data = TraceData::new();
        let e1 = add_event(&mut data, 100, 100, "e1", 0);
        let e2 = add_event(&mut data, 150, 100, "e2", 0);

        let track = Track {
            kind: TrackType::Thread,
            event_indices: vec![e1, e2],
            depths: vec![0, 0],
            max_depth: 0,
            ..Default::default()
        };

        let mut state = State::default();
        let blocks = state.thread_blocks(&track, &data, 0.0, 10000.0, 1000.0, 0.0, None);
        assert_eq!(blocks.len(), 2);
        assert_eq!(blocks[0].depth, 0);
        assert_eq!(blocks[1].depth, 0);
    }

    #[test]
    fn selected_event_overlap() {
        let mut data = TraceData::new();
        let e1 = add_event(&mut data, 100, 1, "e1", 0);
        let e2 = add_event(&mut data, 101, 1, "e2", 0);
        let e3 = add_event(&mut data, 102, 1, "e3", 0);

        let track = Track {
            kind: TrackType::Thread,
            event_indices: vec![e1, e2, e3],
            depths: vec![0, 0, 0],
            max_depth: 0,
            ..Default::default()
        };

        let mut state = State::default();
        state.update_selection(data.events.len(), &[1]);
        let blocks = state.thread_blocks(&track, &data, 0.0, 10000.0, 1000.0, 0.0, None);
        assert_eq!(blocks.len(), 3);
    }

    #[test]
    fn selected_event_no_overlap() {
        let mut data = TraceData::new();
        let e1 = add_event(&mut data, 100, 1, "e1", 0);
        let e2 = add_event(&mut data, 110, 1, "e2", 0);
        let e3 = add_event(&mut data, 120, 1, "e3", 0);

        let track = Track {
            kind: TrackType::Thread,
            event_indices: vec![e1, e2, e3],
            depths: vec![0, 0, 0],
            max_depth: 0,
            ..Default::default()
        };

        let mut state = State::default();
        state.update_selection(data.events.len(), &[1]);
        let blocks = state.thread_blocks(&track, &data, 0.0, 10000.0, 1000.0, 0.0, None);
        assert_eq!(blocks.len(), 3);
    }

    #[test]
    fn extreme_zoom_out() {
        let mut data = TraceData::new();
        let mut indices = Vec::new();
        for i in 0..1000 {
            let idx = add_event(&mut data, (i * 2) as i64, 1, "e", 0);
            indices.push(idx);
        }

        let track = Track {
            kind: TrackType::Thread,
            event_indices: indices,
            depths: vec![0; 1000],
            max_depth: 0,
            ..Default::default()
        };

        let mut state = State::default();
        let blocks = state.thread_blocks(&track, &data, 0.0, 1000000.0, 1000.0, 0.0, None);
        assert!(blocks.len() < 100);
    }

    #[test]
    fn counter_bucketing() {
        let mut data = TraceData::new();
        let e1 = add_event(&mut data, 100, 0, "c", 0);
        let e2 = add_event(&mut data, 101, 0, "c", 0);
        let e3 = add_event(&mut data, 102, 0, "c", 0);
        let e4 = add_event(&mut data, 200, 0, "c", 0);

        let track = Track {
            kind: TrackType::Counter,
            event_indices: vec![e1, e2, e3, e4],
            ..Default::default()
        };

        let mut state = State::default();
        let blocks = state.counter_blocks(&track, &data, 0.0, 1000.0, 1000.0, 0.0, None);
        assert!(!blocks.is_empty());
        assert!(blocks.len() < 50);
        assert!(blocks[0].event_index().is_some());
    }

    #[test]
    fn counter_first_event_gap() {
        let mut data = TraceData::new();
        let e1 = add_event(&mut data, 100, 0, "c", 0);
        let e2 = add_event(&mut data, 150, 0, "c", 0);

        let track = Track {
            kind: TrackType::Counter,
            event_indices: vec![e1, e2],
            ..Default::default()
        };

        let mut state = State::default();
        let blocks = state.counter_blocks(&track, &data, 0.0, 200.0, 1000.0, 0.0, None);
        assert!(!blocks.is_empty());
        assert!(blocks[0].event_index().is_some());
        assert!(blocks.last().unwrap().event_index().is_some());
    }

    #[test]
    fn counter_mid_viewport() {
        let mut data = TraceData::new();
        let e1 = add_event(&mut data, 50, 0, "c", 0);
        let e2 = add_event(&mut data, 150, 0, "c", 0);

        let track = Track {
            kind: TrackType::Counter,
            event_indices: vec![e1, e2],
            ..Default::default()
        };

        let mut state = State::default();
        let blocks = state.counter_blocks(&track, &data, 100.0, 200.0, 1000.0, 0.0, None);
        assert!(!blocks.is_empty());
        assert_eq!(blocks[0].event_index(), Some(0));
        assert!(blocks.last().unwrap().event_index().is_some());
    }

    #[test]
    fn counter_selection_and_focus_use_the_hovered_interval_bounds() {
        let mut data = TraceData::new();
        let first = add_event(&mut data, 100, 0, "c", 0);
        let second = add_event(&mut data, 200, 0, "c", 0);
        let track = Track {
            kind: TrackType::Counter,
            event_indices: vec![first, second],
            ..Default::default()
        };
        let mut state = State::default();

        let hovered = state.counter_blocks(&track, &data, 0.0, 300.0, 300.0, 0.0, None);
        let hovered = hovered
            .iter()
            .find(|block| block.event_index() == Some(first))
            .unwrap();
        let hovered_bounds = (hovered.x1, hovered.x2);
        assert!(hovered.x2 - hovered.x1 <= MIN_EVENT_WIDTH);

        state.update_selection(data.events.len(), &[first as i64]);
        let selected = state.counter_blocks(&track, &data, 0.0, 300.0, 300.0, 0.0, None);
        let selected = selected.iter().find(|block| block.selected).unwrap();
        assert_eq!((selected.x1, selected.x2), hovered_bounds);

        state.update_selection(data.events.len(), &[]);
        let focused = state.counter_blocks(&track, &data, 0.0, 300.0, 300.0, 0.0, Some(first));
        let focused = focused.iter().find(|block| block.focused).unwrap();
        assert_eq!((focused.x1, focused.x2), hovered_bounds);
    }

    #[test]
    fn counter_draw_too_far_left() {
        let mut data = TraceData::new();
        let first = add_counter_event(&mut data, 100, None);
        let second = add_counter_event(&mut data, 150, None);
        let track = counter_track(vec![first, second], vec![]);
        let mut state = State::default();

        let blocks = state.counter_blocks(&track, &data, 50.0, 150.0, 1000.0, 0.0, None);

        assert!(!blocks.is_empty());
        assert!(blocks[0].event_index().is_some());
    }

    #[test]
    fn counter_canvas_offset() {
        let mut data = TraceData::new();
        let first = add_counter_event(&mut data, 100, None);
        let second = add_counter_event(&mut data, 150, None);
        let track = counter_track(vec![first, second], vec![]);
        let mut state = State::default();

        let blocks = state.counter_blocks(&track, &data, 0.0, 200.0, 1000.0, 100.0, None);

        assert!(!blocks.is_empty());
        assert!(blocks[0].x1 >= 100.0);
        assert!(blocks[0].event_index().is_some());
    }

    #[test]
    fn counter_before_first_event() {
        let mut data = TraceData::new();
        let event = add_counter_event(&mut data, 100, None);
        let track = counter_track(vec![event], vec![]);
        let mut state = State::default();

        let blocks = state.counter_blocks(&track, &data, 0.0, 50.0, 1000.0, 0.0, None);

        assert!(blocks.is_empty());
    }

    #[test]
    fn counter_first_event_at_start() {
        let mut data = TraceData::new();
        let event = add_counter_event(&mut data, 100, None);
        let track = counter_track(vec![event], vec![]);
        let mut state = State::default();

        let blocks = state.counter_blocks(&track, &data, 100.0, 200.0, 1000.0, 0.0, None);

        assert!(blocks.is_empty());
    }

    #[test]
    fn counter_clamped_gap_bug() {
        let mut data = TraceData::new();
        let first = add_counter_event(&mut data, 0, None);
        let second = add_counter_event(&mut data, 100, None);
        let track = counter_track(vec![first, second], vec![]);
        let mut state = State::default();

        let blocks = state.counter_blocks(&track, &data, 50.0, 150.0, 1000.0, 0.0, None);

        assert!(!blocks.is_empty());
        assert_eq!(blocks[0].event_index(), Some(first));
        assert!(blocks.last().unwrap().event_index().is_some());
    }

    #[test]
    fn counter_viewport_far_left() {
        let mut data = TraceData::new();
        let event = add_counter_event(&mut data, 100, None);
        let track = counter_track(vec![event], vec![]);
        let mut state = State::default();

        let blocks = state.counter_blocks(&track, &data, -200.0, -100.0, 1000.0, 0.0, None);

        assert!(blocks.is_empty());
    }

    #[test]
    fn counter_first_event_just_before() {
        let mut data = TraceData::new();
        let event = add_counter_event(&mut data, 90, None);
        let track = counter_track(vec![event], vec![]);
        let mut state = State::default();

        let blocks = state.counter_blocks(&track, &data, 100.0, 200.0, 1000.0, 0.0, None);

        assert!(blocks.is_empty());
    }

    #[test]
    fn counter_session_start_gap() {
        let mut data = TraceData::new();
        let first = add_counter_event(&mut data, 100, None);
        let second = add_counter_event(&mut data, 150, None);
        let track = counter_track(vec![first, second], vec![]);
        let mut state = State::default();

        let blocks = state.counter_blocks(&track, &data, 0.0, 200.0, 1000.0, 0.0, None);

        assert!(!blocks.is_empty());
        assert!(blocks[0].event_index().is_some());
        assert!(blocks.last().unwrap().event_index().is_some());
    }

    #[test]
    fn counter_exact_start() {
        let mut data = TraceData::new();
        let first = add_counter_event(&mut data, 100, None);
        let second = add_counter_event(&mut data, 150, None);
        let track = counter_track(vec![first, second], vec![]);
        let mut state = State::default();

        let blocks = state.counter_blocks(&track, &data, 100.0, 200.0, 1000.0, 0.0, None);

        assert!(!blocks.is_empty());
        assert!(blocks[0].event_index().is_some());
    }

    #[test]
    fn counter_viewport_negative() {
        let mut data = TraceData::new();
        let first = add_counter_event(&mut data, 100, None);
        let second = add_counter_event(&mut data, 150, None);
        let track = counter_track(vec![first, second], vec![]);
        let mut state = State::default();

        let blocks = state.counter_blocks(&track, &data, -100.0, 200.0, 1000.0, 0.0, None);

        assert!(!blocks.is_empty());
        assert!(blocks[0].event_index().is_some());
        assert!(blocks.last().unwrap().event_index().is_some());
    }

    #[test]
    fn counter_partial_start() {
        let mut data = TraceData::new();
        let first = add_counter_event(&mut data, 50, None);
        let second = add_counter_event(&mut data, 150, None);
        let track = counter_track(vec![first, second], vec![]);
        let mut state = State::default();

        let blocks = state.counter_blocks(&track, &data, 100.0, 200.0, 1000.0, 0.0, None);

        assert!(!blocks.is_empty());
        assert_eq!(blocks[0].event_index(), Some(first));
        assert!(blocks.last().unwrap().event_index().is_some());
    }

    #[test]
    fn counter_viewport_far_right() {
        let mut data = TraceData::new();
        let event = add_counter_event(&mut data, 100, None);
        let track = counter_track(vec![event], vec![]);
        let mut state = State::default();

        let blocks = state.counter_blocks(&track, &data, 200.0, 300.0, 1000.0, 0.0, None);

        assert!(blocks.is_empty());
    }

    #[test]
    fn counter_last_event_at_end() {
        let mut data = TraceData::new();
        let first = add_counter_event(&mut data, 100, None);
        let second = add_counter_event(&mut data, 200, None);
        let track = counter_track(vec![first, second], vec![]);
        let mut state = State::default();

        let blocks = state.counter_blocks(&track, &data, 0.0, 300.0, 3000.0, 0.0, None);

        assert!(!blocks.is_empty());
        assert!(blocks.last().unwrap().event_index().is_some());
    }

    #[test]
    fn counter_peak_preservation() {
        let mut data = TraceData::new();
        let key = data.intern(b"a");
        let first = add_counter_event(&mut data, 10, Some((key, 10.0)));
        let second = add_counter_event(&mut data, 11, Some((key, 1.0)));
        let track = counter_track(vec![first, second], vec![key]);
        let mut state = State::default();

        let blocks = state.counter_blocks(&track, &data, 0.0, 100.0, 100.0, 0.0, None);
        let bucket = blocks
            .iter()
            .position(|block| block.x1 >= 9.0 - 0.001 && block.x2 <= 12.0 + 0.001)
            .unwrap();

        assert_eq!(blocks.peaks(bucket), [10.0]);
    }

    #[test]
    fn counter_bucketing_stability() {
        let mut data = TraceData::new();
        let first = add_counter_event(&mut data, 10, None);
        let second = add_counter_event(&mut data, 20, None);
        let track = counter_track(vec![first, second], vec![]);
        let mut state = State::default();

        let blocks_a = state.counter_blocks(&track, &data, 0.0, 1000.0, 1000.0, 0.0, None);
        let first_a = blocks_a
            .iter()
            .find(|block| block.event_index() == Some(first))
            .unwrap();
        assert!((first_a.x1 - 10.0).abs() < 0.01);
        assert!((first_a.x2 - 12.0).abs() < 0.01);

        let blocks_b = state.counter_blocks(&track, &data, 1.0, 1001.0, 1000.0, 0.0, None);
        let first_b = blocks_b
            .iter()
            .find(|block| block.event_index() == Some(first))
            .unwrap();
        assert!((first_b.x1 - 9.0).abs() < 0.01);
        assert!((first_b.x2 - 11.0).abs() < 0.01);
    }

    #[test]
    fn counter_gap_initial_state() {
        let mut data = TraceData::new();
        let event = add_counter_event(&mut data, 50, None);
        let track = counter_track(vec![event], vec![]);
        let mut state = State::default();

        let blocks = state.counter_blocks(&track, &data, 75.0, 100.0, 1000.0, 0.0, None);

        assert!(blocks.is_empty());
    }

    #[test]
    fn counter_start_index_bug() {
        let mut data = TraceData::new();
        let event = add_counter_event(&mut data, 50, None);
        let track = counter_track(vec![event], vec![]);
        let mut state = State::default();

        let blocks = state.counter_blocks(&track, &data, 0.0, 50.0, 1000.0, 0.0, None);

        assert!(blocks.is_empty());
    }

    #[test]
    fn counter_max_duration_bug() {
        let mut data = TraceData::new();
        let first = add_counter_event(&mut data, 50, None);
        let second = add_counter_event(&mut data, 150, None);
        let track = counter_track(vec![first, second], vec![]);
        let mut state = State::default();

        let blocks = state.counter_blocks(&track, &data, 100.0, 200.0, 1000.0, 0.0, None);

        assert!(!blocks.is_empty());
        assert_eq!(blocks[0].event_index(), Some(first));
        assert!(blocks.last().unwrap().event_index().is_some());
    }

    #[test]
    fn counter_drop_stub_fix() {
        let mut data = TraceData::new();
        let key = data.intern(b"a");
        let first = add_counter_event(&mut data, 0, Some((key, 100.0)));
        let second = add_counter_event(&mut data, 10, Some((key, 10.0)));
        let track = counter_track(vec![first, second], vec![key]);
        let mut state = State::default();

        let blocks = state.counter_blocks(&track, &data, 0.0, 100.0, 100.0, 0.0, None);
        let bucket = blocks
            .iter()
            .position(|block| block.x1 >= 9.0 - 0.001 && block.x2 <= 12.0 + 0.001)
            .unwrap();

        assert_eq!(blocks.peaks(bucket), [10.0]);
    }

    #[test]
    fn thread_bucketing_stability_panned() {
        let mut data = TraceData::new();
        let spanning = add_event(&mut data, 85, 20, "e0", 0);
        let first = add_event(&mut data, 100, 1, "e1", 0);
        let second = add_event(&mut data, 105, 1, "e2", 0);
        let third = add_event(&mut data, 110, 1, "e3", 0);
        let track = Track {
            event_indices: vec![spanning, first, second, third],
            depths: vec![0; 4],
            max_depth: 0,
            ..Default::default()
        };
        let mut state = State::default();

        let blocks_a = state
            .thread_blocks(&track, &data, 0.0, 10000.0, 1000.0, 0.0, None)
            .to_vec();
        let blocks_b = state.thread_blocks(&track, &data, 95.0, 10095.0, 1000.0, 0.0, None);

        assert_eq!(
            blocks_a
                .iter()
                .find(|block| (block.x1 - 9.0).abs() < 0.001)
                .map(|block| block.count),
            Some(3)
        );
        assert_eq!(
            blocks_b
                .iter()
                .find(|block| (block.x1 + 0.5).abs() < 0.001)
                .map(|block| block.count),
            Some(3)
        );
    }

    #[test]
    fn thread_bucketing_stability_threshold() {
        let mut data = TraceData::new();
        let event = add_event(&mut data, 100, 30, "e0", 0);
        let track = Track {
            event_indices: vec![event],
            depths: vec![0],
            max_depth: 0,
            ..Default::default()
        };
        let mut state = State::default();

        let blocks_a = state.thread_blocks(&track, &data, 0.0, 10000.0, 1000.0, 0.0, None);
        assert_eq!(blocks_a.len(), 1);
        assert!(blocks_a[0].x2 - blocks_a[0].x1 >= 3.0 - 0.001);

        let blocks_b = state.thread_blocks(&track, &data, 0.0, 10000.0001, 1000.0, 0.0, None);
        assert_eq!(blocks_b.len(), 1);
        assert!((blocks_b[0].x1 - 10.0).abs() < 0.01);
    }

    #[test]
    fn zoomed_in_spanning_event() {
        let mut data = TraceData::new();
        let spanning = add_event(&mut data, 0, 60_000_000, "monster", 0);
        for index in 0..999_999 {
            add_event(&mut data, 60_000_000 + i64::from(index) * 20, 10, "tiny", 0);
        }
        let mut track = Track {
            event_indices: (0..1_000_000).collect(),
            depths: vec![0; 1_000_000],
            max_depth: 0,
            ..Default::default()
        };
        track.update_max_duration(&data);
        let mut state = State::default();

        let started = std::time::Instant::now();
        let blocks =
            state.thread_blocks(&track, &data, 59_900_000.0, 60_000_000.0, 1000.0, 0.0, None);

        assert!(started.elapsed() < std::time::Duration::from_millis(10));
        let spanning = blocks
            .iter()
            .find(|block| block.event_index == spanning)
            .unwrap();
        assert!(spanning.x1 <= 0.0);
        assert!(spanning.x2 >= 1000.0);
    }

    #[test]
    fn correctness_spanning_and_coalesced() {
        let mut data = TraceData::new();
        let spanning = add_event(&mut data, 0, 2000, "spanning", 0);
        for _ in 1..1024 {
            add_event(&mut data, 0, 10, "tiny_invisible", 0);
        }
        for index in 0..1024 {
            add_event(&mut data, 1000 + i64::from(index) * 2, 1, "tiny_visible", 0);
        }
        let mut track = Track {
            event_indices: (0..2048).collect(),
            depths: (0..2048).map(|index| u32::from(index >= 1024)).collect(),
            max_depth: 1,
            ..Default::default()
        };
        track.update_max_duration(&data);
        let mut state = State::default();

        let blocks = state.thread_blocks(&track, &data, 1000.0, 1100.0, 1000.0, 0.0, None);

        assert!(
            blocks
                .iter()
                .any(|block| block.depth == 0 && block.event_index == spanning)
        );
        assert!(blocks.iter().any(|block| block.depth == 1));
    }

    #[test]
    fn counter_focused_highlight() {
        let mut data = TraceData::new();
        let key = data.intern(b"value");
        let first = add_counter_event(&mut data, 100, Some((key, 10.0)));
        let second = add_counter_event(&mut data, 150, Some((key, 10.0)));
        let track = counter_track(vec![first, second], vec![key]);
        let mut state = State::default();

        let blocks = state.counter_blocks(&track, &data, 0.0, 200.0, 1000.0, 0.0, Some(first));
        let focused = blocks.iter().find(|block| block.focused).unwrap();

        assert_eq!(focused.event_index(), Some(first));
        assert!(focused.x2 - focused.x1 <= MIN_EVENT_WIDTH + 0.001);
    }

    #[test]
    fn focused_event_never_skipped() {
        let mut data = TraceData::new();
        let e1 = add_event(&mut data, 100, 1, "e1", 0);
        let e2 = add_event(&mut data, 101, 1, "e2", 0);

        let track = Track {
            kind: TrackType::Thread,
            event_indices: vec![e1, e2],
            depths: vec![0, 0],
            max_depth: 0,
            ..Default::default()
        };

        let mut state = State::default();
        let blocks_unfocused = state.thread_blocks(&track, &data, 0.0, 1000.0, 1000.0, 0.0, None);
        assert_eq!(blocks_unfocused.len(), 1);
        assert_eq!(blocks_unfocused[0].count, 2);

        let blocks_focused = state.thread_blocks(&track, &data, 0.0, 1000.0, 1000.0, 0.0, Some(1));
        assert_eq!(blocks_focused.len(), 2);
        assert!(
            blocks_focused
                .iter()
                .any(|b| b.focused && b.event_index == 1)
        );
    }

    #[test]
    fn render_scratch_buffers_are_reused() {
        assert!(std::mem::size_of::<ThreadBucketRepresentative>() <= 24);
        assert!(std::mem::size_of::<CounterBlock>() <= 24);

        let mut data = TraceData::new();
        let thread_events = (0..100)
            .map(|index| add_event(&mut data, index * 10, 1, "event", 0))
            .collect::<Vec<_>>();
        let thread_track = Track {
            kind: TrackType::Thread,
            event_indices: thread_events,
            depths: vec![0; 100],
            max_depth: 0,
            ..Default::default()
        };
        let mut state = State::default();

        assert!(
            !state
                .thread_blocks(&thread_track, &data, 0.0, 1000.0, 1000.0, 0.0, None)
                .is_empty()
        );
        let thread_pointers = (
            state.thread_blocks.as_ptr(),
            state.thread_blocked_until.as_ptr(),
            state.thread_bucket_representatives.as_ptr(),
        );
        let thread_capacities = (
            state.thread_blocks.capacity(),
            state.thread_blocked_until.capacity(),
            state.thread_bucket_representatives.capacity(),
        );

        state.thread_blocks(&thread_track, &data, 0.0, 1000.0, 1000.0, 0.0, None);
        assert_eq!(
            thread_pointers,
            (
                state.thread_blocks.as_ptr(),
                state.thread_blocked_until.as_ptr(),
                state.thread_bucket_representatives.as_ptr(),
            )
        );
        assert_eq!(
            thread_capacities,
            (
                state.thread_blocks.capacity(),
                state.thread_blocked_until.capacity(),
                state.thread_bucket_representatives.capacity(),
            )
        );

        let series = data.intern(b"value");
        let counter_events = (0..100)
            .map(|index| add_counter_event(&mut data, index * 10, Some((series, index as f64))))
            .collect();
        let counter_track = counter_track(counter_events, vec![series]);

        assert!(
            !state
                .counter_blocks(&counter_track, &data, 0.0, 1000.0, 1000.0, 0.0, None)
                .is_empty()
        );
        let counter_pointers = (
            state.counter_blocks.as_ptr(),
            state.counter_peaks.as_ptr(),
            state.counter_values.as_ptr(),
            state.counter_bucket_peaks.as_ptr(),
            state.counter_updated.as_ptr(),
        );
        let counter_capacities = (
            state.counter_blocks.capacity(),
            state.counter_peaks.capacity(),
            state.counter_values.capacity(),
            state.counter_bucket_peaks.capacity(),
            state.counter_updated.capacity(),
        );

        state.counter_blocks(&counter_track, &data, 0.0, 1000.0, 1000.0, 0.0, None);
        assert_eq!(
            counter_pointers,
            (
                state.counter_blocks.as_ptr(),
                state.counter_peaks.as_ptr(),
                state.counter_values.as_ptr(),
                state.counter_bucket_peaks.as_ptr(),
                state.counter_updated.as_ptr(),
            )
        );
        assert_eq!(
            counter_capacities,
            (
                state.counter_blocks.capacity(),
                state.counter_peaks.capacity(),
                state.counter_values.capacity(),
                state.counter_bucket_peaks.capacity(),
                state.counter_updated.capacity(),
            )
        );
    }
}
