use super::{TraceData, aggregate};
use std::cmp::Ordering;
use std::collections::BTreeMap;

#[derive(Clone, Debug, PartialEq)]
pub struct Entry {
    pub key: Vec<u8>,
    pub baseline_duration: f64,
    pub baseline_count: usize,
    pub target_duration: f64,
    pub target_count: usize,
    pub delta_duration: f64,
    pub delta_count: i64,
}

pub fn compute(
    baseline: &TraceData,
    target: &TraceData,
    group_by: &[u8],
    sort_by: &[u8],
) -> Vec<Entry> {
    let mut map = BTreeMap::<Vec<u8>, Entry>::new();
    for entry in aggregate::compute(baseline, group_by, b"") {
        let key = baseline.string(entry.key).to_vec();
        map.insert(
            key.clone(),
            Entry {
                key,
                baseline_duration: entry.total_duration,
                baseline_count: entry.count,
                target_duration: 0.0,
                target_count: 0,
                delta_duration: 0.0,
                delta_count: 0,
            },
        );
    }
    for entry in aggregate::compute(target, group_by, b"") {
        let key = target.string(entry.key).to_vec();
        let value = map.entry(key.clone()).or_insert(Entry {
            key,
            baseline_duration: 0.0,
            baseline_count: 0,
            target_duration: 0.0,
            target_count: 0,
            delta_duration: 0.0,
            delta_count: 0,
        });
        value.target_duration = entry.total_duration;
        value.target_count = entry.count;
    }
    let mut output: Vec<_> = map
        .into_values()
        .map(|mut entry| {
            entry.delta_duration = entry.target_duration - entry.baseline_duration;
            entry.delta_count = entry.target_count as i64 - entry.baseline_count as i64;
            entry
        })
        .collect();
    output.sort_by(|a, b| {
        let ordering = if sort_by == b"count-delta" {
            b.delta_count.cmp(&a.delta_count)
        } else {
            b.delta_duration
                .partial_cmp(&a.delta_duration)
                .unwrap_or(Ordering::Equal)
        };
        ordering.then_with(|| a.key.cmp(&b.key))
    });
    output
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::trace::data::PersistedEvent;

    fn data(events: &[(&[u8], i64)]) -> TraceData {
        let mut data = TraceData::new();
        for &(name, duration) in events {
            let name = data.intern(name);
            data.events.push(PersistedEvent {
                name,
                duration,
                ..PersistedEvent::default()
            });
        }
        data
    }

    fn categorized_data(events: &[(&[u8], &[u8], i64)]) -> TraceData {
        let mut data = TraceData::new();
        for &(name, category, duration) in events {
            let name = data.intern(name);
            let category = data.intern(category);
            data.events.push(PersistedEvent {
                name,
                category,
                duration,
                ..PersistedEvent::default()
            });
        }
        data
    }

    #[test]
    fn basic() {
        let baseline = data(&[(b"task1", 100), (b"task2", 200)]);
        let target = data(&[(b"task1", 150), (b"task3", 300)]);
        let entries = compute(&baseline, &target, b"name", b"dur-delta");
        assert_eq!(entries.len(), 3);
        assert_eq!(
            (
                &entries[0].key[..],
                entries[0].delta_duration,
                entries[0].baseline_count,
                entries[0].target_count
            ),
            (b"task3".as_slice(), 300.0, 0, 1)
        );
        assert_eq!(
            (&entries[1].key[..], entries[1].delta_duration),
            (b"task1".as_slice(), 50.0)
        );
        assert_eq!(
            (&entries[2].key[..], entries[2].delta_duration),
            (b"task2".as_slice(), -200.0)
        );
        assert_eq!(
            entries
                .iter()
                .map(|entry| entry.delta_count)
                .collect::<Vec<_>>(),
            [1, 0, -1]
        );
    }

    #[test]
    fn groups_by_category() {
        let baseline = categorized_data(&[
            (b"one", b"cpu", 10),
            (b"two", b"cpu", 20),
            (b"three", b"gpu", 5),
        ]);
        let target = categorized_data(&[(b"one", b"cpu", 40), (b"four", b"io", 7)]);

        let entries = compute(&baseline, &target, b"category", b"duration-delta");

        assert_eq!(
            entries
                .iter()
                .map(|entry| (entry.key.as_slice(), entry.delta_duration))
                .collect::<Vec<_>>(),
            [
                (b"cpu".as_slice(), 10.0),
                (b"io".as_slice(), 7.0),
                (b"gpu".as_slice(), -5.0),
            ]
        );
    }

    #[test]
    fn sorts_positive_and_negative_count_deltas() {
        let baseline = data(&[(b"a", 1), (b"a", 1), (b"b", 1)]);
        let target = data(&[(b"a", 1), (b"c", 1), (b"c", 1), (b"c", 1)]);

        let entries = compute(&baseline, &target, b"name", b"count-delta");

        assert_eq!(
            entries
                .iter()
                .map(|entry| (entry.key.as_slice(), entry.delta_count))
                .collect::<Vec<_>>(),
            [
                (b"c".as_slice(), 3),
                (b"a".as_slice(), -1),
                (b"b".as_slice(), -1),
            ]
        );
    }

    #[test]
    fn handles_empty_inputs_and_missing_groups() {
        let empty = TraceData::new();
        let target = data(&[(b"target", 10)]);
        let target_only = compute(&empty, &target, b"name", b"duration-delta");
        assert_eq!(
            (
                target_only[0].key.as_slice(),
                target_only[0].baseline_count,
                target_only[0].target_count,
            ),
            (b"target".as_slice(), 0, 1)
        );

        let baseline_only = compute(&target, &empty, b"name", b"duration-delta");
        assert_eq!(
            (
                baseline_only[0].key.as_slice(),
                baseline_only[0].baseline_count,
                baseline_only[0].target_count,
            ),
            (b"target".as_slice(), 1, 0)
        );
        assert!(compute(&empty, &empty, b"name", b"duration-delta").is_empty());
    }

    #[test]
    fn equal_and_zero_deltas_use_lexical_ties() {
        let baseline = data(&[(b"same", 10)]);
        let target = data(&[(b"z", 10), (b"a", 10), (b"same", 10)]);

        let entries = compute(&baseline, &target, b"name", b"duration-delta");

        assert_eq!(
            entries
                .iter()
                .map(|entry| (entry.key.as_slice(), entry.delta_duration))
                .collect::<Vec<_>>(),
            [
                (b"a".as_slice(), 10.0),
                (b"z".as_slice(), 10.0),
                (b"same".as_slice(), 0.0),
            ]
        );
    }
}
