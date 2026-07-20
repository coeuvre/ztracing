use super::{
    TraceData,
    track::{Track, TrackType},
};
use crate::string_interner::StringId;
use std::cmp::Ordering;
use std::collections::HashMap;

pub const MAX_DOMINANT_EVENTS: usize = 3;

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Bucket {
    pub start: f64,
    pub end: f64,
    pub average: f64,
    pub dominant_events: Vec<StringId>,
}

pub fn compute(
    tracks: &[Track],
    data: &TraceData,
    minimum: i64,
    maximum: i64,
    count: usize,
) -> Vec<Bucket> {
    if count == 0 || tracks.is_empty() || maximum <= minimum {
        return Vec::new();
    }
    let width = (maximum as f64 - minimum as f64) / count as f64;
    (0..count)
        .map(|bucket_index| {
            let start = minimum as f64 + bucket_index as f64 * width;
            let end = start + width;
            let mut average = 0.0;
            let mut durations = HashMap::<StringId, f64>::new();
            for track in tracks
                .iter()
                .filter(|track| track.kind == TrackType::Thread)
            {
                for (position, &event_index) in track.event_indices.iter().enumerate() {
                    if track.depths[position] != 0 {
                        continue;
                    }
                    let Some(event) = data.events.get(event_index) else {
                        continue;
                    };
                    let overlap = (event.timestamp as f64 + event.duration as f64).min(end)
                        - (event.timestamp as f64).max(start);
                    if overlap > 0.0 {
                        average += overlap / width;
                        *durations.entry(event.name).or_default() += overlap;
                    }
                }
            }
            let mut durations: Vec<_> = durations.into_iter().collect();
            durations.sort_by(|a, b| {
                b.1.partial_cmp(&a.1)
                    .unwrap_or(Ordering::Equal)
                    .then_with(|| data.string(a.0).cmp(data.string(b.0)))
            });
            Bucket {
                start,
                end,
                average,
                dominant_events: durations
                    .into_iter()
                    .take(MAX_DOMINANT_EVENTS)
                    .map(|entry| entry.0)
                    .collect(),
            }
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::compute;
    use crate::trace::{PersistedEvent, TraceData, Track};

    fn add_event(data: &mut TraceData, name: &[u8], timestamp: i64, duration: i64) -> usize {
        let name = data.intern(name);
        let index = data.events.len();
        data.events.push(PersistedEvent {
            name,
            timestamp,
            duration,
            ..PersistedEvent::default()
        });
        index
    }

    fn thread(index: usize, depth: u32) -> Track {
        let mut track = Track::thread(0, 0);
        track.event_indices.push(index);
        track.depths.push(depth);
        track
    }

    #[test]
    fn basic() {
        let mut data = TraceData::new();
        let first = add_event(&mut data, b"task1", 1_000, 1_000);
        let second = add_event(&mut data, b"task2", 1_500, 1_000);
        let buckets = compute(
            &[thread(first, 0), thread(second, 0)],
            &data,
            1_000,
            2_500,
            2,
        );
        assert!((buckets[0].average - 1.333).abs() < 0.01);
        assert!((buckets[1].average - 1.333).abs() < 0.01);
    }

    #[test]
    fn exact_bucket_boundaries_do_not_overlap_adjacent_buckets() {
        let mut data = TraceData::new();
        let first = add_event(&mut data, b"first", 0, 10);
        let second = add_event(&mut data, b"second", 10, 10);
        let buckets = compute(&[thread(first, 0), thread(second, 0)], &data, 0, 20, 2);

        assert_eq!(buckets[0].average, 1.0);
        assert_eq!(buckets[1].average, 1.0);
        assert_eq!(data.string(buckets[0].dominant_events[0]), b"first");
        assert_eq!(data.string(buckets[1].dominant_events[0]), b"second");
    }

    #[test]
    fn ignores_nested_events_and_counter_tracks() {
        let mut data = TraceData::new();
        let parent = add_event(&mut data, b"parent", 0, 10);
        let nested = add_event(&mut data, b"nested", 0, 10);
        let counter_event = add_event(&mut data, b"counter", 0, 10);
        let mut thread_track = Track::thread(0, 0);
        thread_track.event_indices = vec![parent, nested];
        thread_track.depths = vec![0, 1];
        let mut counter = Track::counter(0, Default::default(), Default::default());
        counter.event_indices.push(counter_event);

        let buckets = compute(&[thread_track, counter], &data, 0, 10, 1);

        assert_eq!(buckets[0].average, 1.0);
        assert_eq!(buckets[0].dominant_events.len(), 1);
        assert_eq!(data.string(buckets[0].dominant_events[0]), b"parent");
    }

    #[test]
    fn ignores_invalid_event_indices() {
        let buckets = compute(&[thread(99_999, 0)], &TraceData::new(), 0, 10, 1);
        assert_eq!(buckets[0].average, 0.0);
        assert!(buckets[0].dominant_events.is_empty());
    }

    #[test]
    fn invalid_inputs_produce_no_buckets() {
        let track = Track::thread(0, 0);
        assert!(compute(&[track.clone()], &TraceData::new(), 0, 10, 0).is_empty());
        assert!(compute(&[], &TraceData::new(), 0, 10, 1).is_empty());
        assert!(compute(&[track.clone()], &TraceData::new(), 10, 10, 1).is_empty());
        assert!(compute(&[track], &TraceData::new(), 10, 0, 1).is_empty());
    }

    #[test]
    fn dominant_event_ties_are_lexical_and_limited_to_three() {
        let mut data = TraceData::new();
        let tracks: Vec<_> = [b"z".as_slice(), b"a", b"c", b"b"]
            .into_iter()
            .map(|name| {
                let index = add_event(&mut data, name, 0, 10);
                thread(index, 0)
            })
            .collect();

        let buckets = compute(&tracks, &data, 0, 10, 1);
        let names: Vec<_> = buckets[0]
            .dominant_events
            .iter()
            .map(|id| data.string(*id))
            .collect();

        assert_eq!(buckets[0].average, 4.0);
        assert_eq!(names, [b"a".as_slice(), b"b", b"c"]);
    }

    #[test]
    fn full_i64_range_does_not_overflow() {
        let mut data = TraceData::new();
        let index = add_event(&mut data, b"event", i64::MIN, i64::MAX);
        let buckets = compute(&[thread(index, 0)], &data, i64::MIN, i64::MAX, 2);

        assert_eq!(buckets.len(), 2);
        assert!(buckets.iter().all(|bucket| bucket.average.is_finite()));
        assert!(buckets[0].average > 0.0);
    }
}
