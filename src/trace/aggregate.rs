use super::TraceData;
use crate::string_interner::StringId;
use std::cmp::Ordering;
use std::collections::HashMap;

#[derive(Clone, Debug, PartialEq)]
pub struct Entry {
    pub key: StringId,
    pub total_duration: f64,
    pub count: usize,
}

pub fn compute(data: &TraceData, group_by: &[u8], sort_by: &[u8]) -> Vec<Entry> {
    let by_category = group_by == b"category";
    let mut values = HashMap::<StringId, (f64, usize)>::new();
    for event in &data.events {
        let key = if by_category {
            event.category
        } else {
            event.name
        };
        let value = values.entry(key).or_default();
        value.0 += event.duration as f64;
        value.1 += 1;
    }
    let mut output: Vec<_> = values
        .into_iter()
        .map(|(key, (total_duration, count))| Entry {
            key,
            total_duration,
            count,
        })
        .collect();
    output.sort_by(|a, b| {
        let primary = if sort_by == b"count" {
            b.count.cmp(&a.count)
        } else {
            b.total_duration
                .partial_cmp(&a.total_duration)
                .unwrap_or(Ordering::Equal)
        };
        primary.then_with(|| data.string(a.key).cmp(data.string(b.key)))
    });
    output
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::trace::data::PersistedEvent;

    fn add(data: &mut TraceData, name: &[u8], category: &[u8], duration: i64) {
        let name = data.intern(name);
        let category = data.intern(category);
        data.events.push(PersistedEvent {
            name,
            category,
            duration,
            ..PersistedEvent::default()
        });
    }

    #[test]
    fn basic() {
        let mut data = TraceData::new();
        for (name, category, duration) in [
            (b"task1".as_slice(), b"cpu".as_slice(), 100),
            (b"task2", b"gpu", 200),
            (b"task1", b"cpu", 150),
        ] {
            add(&mut data, name, category, duration);
        }
        let entries = compute(&data, b"name", b"duration");
        assert_eq!(entries.len(), 2);
        assert_eq!(data.string(entries[0].key), b"task1");
        assert_eq!((entries[0].total_duration, entries[0].count), (250.0, 2));
        assert_eq!(data.string(entries[1].key), b"task2");
        let entries = compute(&data, b"category", b"count");
        assert_eq!(data.string(entries[0].key), b"cpu");
        assert_eq!(entries[0].count, 2);
        assert_eq!(entries[0].total_duration, 250.0);
        assert_eq!(data.string(entries[1].key), b"gpu");
        assert_eq!((entries[1].count, entries[1].total_duration), (1, 200.0));
    }

    #[test]
    fn empty_input_produces_no_entries() {
        assert!(compute(&TraceData::new(), b"name", b"duration").is_empty());
    }

    #[test]
    fn empty_keys_form_one_group() {
        let mut data = TraceData::new();
        data.events = vec![
            PersistedEvent {
                duration: 10,
                ..PersistedEvent::default()
            },
            PersistedEvent {
                duration: 20,
                ..PersistedEvent::default()
            },
        ];

        let entries = compute(&data, b"name", b"duration");

        assert_eq!(entries.len(), 1);
        assert_eq!(data.string(entries[0].key), b"");
        assert_eq!((entries[0].total_duration, entries[0].count), (30.0, 2));
    }

    #[test]
    fn equal_duration_and_count_ties_are_lexical() {
        let mut data = TraceData::new();
        add(&mut data, b"z", b"", 10);
        add(&mut data, b"a", b"", 10);

        for sort_by in [b"duration".as_slice(), b"count"] {
            let entries = compute(&data, b"name", sort_by);
            let names: Vec<_> = entries.iter().map(|entry| data.string(entry.key)).collect();
            assert_eq!(names, [b"a".as_slice(), b"z"]);
        }
    }

    #[test]
    fn unknown_options_default_to_name_and_duration() {
        let mut data = TraceData::new();
        add(&mut data, b"slow", b"same", 20);
        add(&mut data, b"fast", b"same", 10);

        let entries = compute(&data, b"unknown", b"unknown");

        assert_eq!(entries.len(), 2);
        assert_eq!(data.string(entries[0].key), b"slow");
        assert_eq!(data.string(entries[1].key), b"fast");
    }

    #[test]
    fn negative_durations_sort_after_larger_values() {
        let mut data = TraceData::new();
        add(&mut data, b"negative", b"", -5);
        add(&mut data, b"zero", b"", 0);

        let entries = compute(&data, b"name", b"duration");

        assert_eq!(data.string(entries[0].key), b"zero");
        assert_eq!(entries[0].total_duration, 0.0);
        assert_eq!(data.string(entries[1].key), b"negative");
        assert_eq!(entries[1].total_duration, -5.0);
    }
}
