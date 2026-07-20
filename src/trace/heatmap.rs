use super::{
    TraceData,
    track::{Track, TrackType},
};

pub const BUCKET_COUNT: usize = 16;

pub fn compute(
    tracks: &[Track],
    data: &TraceData,
    minimum: i64,
    maximum: i64,
) -> Vec<[Option<usize>; BUCKET_COUNT]> {
    let mut output = vec![[None; BUCKET_COUNT]; tracks.len()];
    let total = maximum as f64 - minimum as f64;
    if total <= 0.0 {
        return output;
    }
    let width = total / BUCKET_COUNT as f64;
    for (track_index, track) in tracks.iter().enumerate() {
        let mut max_durations = [-1_i64; BUCKET_COUNT];
        for (position, &event_index) in track.event_indices.iter().enumerate() {
            if track.kind == TrackType::Thread && track.depths[position] != 0 {
                continue;
            }
            let Some(event) = data.events.get(event_index) else {
                continue;
            };
            let bucket = (((event.timestamp as f64 - minimum as f64) / width) as isize)
                .clamp(0, BUCKET_COUNT as isize - 1) as usize;
            if event.duration > max_durations[bucket] {
                max_durations[bucket] = event.duration;
                output[track_index][bucket] = Some(event_index);
            }
        }
    }
    output
}

#[cfg(test)]
mod tests {
    use super::compute;
    use crate::trace::{PersistedEvent, TraceData, Track};

    fn event(timestamp: i64, duration: i64) -> PersistedEvent {
        PersistedEvent {
            timestamp,
            duration,
            ..PersistedEvent::default()
        }
    }

    fn thread(indices: Vec<usize>, depths: Vec<u32>) -> Track {
        let mut track = Track::thread(0, 0);
        track.event_indices = indices;
        track.depths = depths;
        track
    }

    #[test]
    fn compute_heatmap_normal() {
        let mut data = TraceData::new();
        data.events = vec![
            event(500, 100),
            event(5_500, 100),
            event(5_000, 100),
            event(5_600, 50),
        ];
        let tracks = vec![
            thread(vec![0, 1, 3], vec![0, 0, 1]),
            thread(vec![2], vec![0]),
        ];
        let heatmaps = compute(&tracks, &data, 0, 16_000);
        assert_eq!(heatmaps[0][0], Some(0));
        assert_eq!(heatmaps[0][5], Some(1));
        assert_eq!(heatmaps[0][1], None);
        assert_eq!(heatmaps[1][5], Some(2));
    }

    #[test]
    fn compute_heatmap_zero_duration() {
        let heatmaps = compute(&[thread(vec![], vec![])], &TraceData::new(), 0, 0);
        assert!(heatmaps[0].iter().all(Option::is_none));
    }

    #[test]
    fn compute_heatmap_empty_inputs() {
        assert!(compute(&[], &TraceData::new(), 0, 1_000).is_empty());
    }

    #[test]
    fn compute_heatmap_counter_track() {
        let mut data = TraceData::new();
        data.events.push(event(5_000, 100));
        let mut track = Track::counter(0, Default::default(), Default::default());
        track.event_indices.push(0);
        let heatmaps = compute(&[track], &data, 0, 16_000);
        assert_eq!(heatmaps[0][5], Some(0));
    }

    #[test]
    fn compute_heatmap_out_of_bounds_index() {
        let heatmaps = compute(
            &[thread(vec![99_999], vec![0])],
            &TraceData::new(),
            0,
            1_000,
        );
        assert!(heatmaps[0].iter().all(Option::is_none));
    }

    #[test]
    fn compute_heatmap_viewport_clamping() {
        let mut data = TraceData::new();
        data.events = vec![event(-500, 100), event(20_000, 100)];
        let heatmaps = compute(&[thread(vec![0, 1], vec![0, 0])], &data, 0, 16_000);
        assert_eq!(heatmaps[0][0], Some(0));
        assert_eq!(heatmaps[0][15], Some(1));
    }

    #[test]
    fn full_i64_viewport_does_not_overflow() {
        let mut data = TraceData::new();
        data.events = vec![event(i64::MIN, 1), event(i64::MAX, 1)];
        let heatmaps = compute(&[thread(vec![0, 1], vec![0, 0])], &data, i64::MIN, i64::MAX);
        assert_eq!(heatmaps[0][0], Some(0));
        assert_eq!(heatmaps[0][15], Some(1));
    }

    #[test]
    fn equal_duration_ties_keep_the_first_event() {
        let mut data = TraceData::new();
        data.events = vec![event(100, 50), event(200, 50)];
        let heatmaps = compute(&[thread(vec![0, 1], vec![0, 0])], &data, 0, 16_000);
        assert_eq!(heatmaps[0][0], Some(0));
    }
}
