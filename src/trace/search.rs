use super::{
    TraceData,
    histogram::{self, Histogram},
};
use base::debug;
use std::sync::Arc;

pub struct Results {
    pub event_indices: Arc<Vec<i64>>,
    pub display_event_indices: Vec<i64>,
    pub histogram: Histogram,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SortColumn {
    Name,
    Category,
    Start,
    Duration,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Sort {
    pub column: SortColumn,
    pub ascending: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct DurationFilter {
    pub minimum: i64,
    pub maximum: i64,
}

#[derive(Clone, Copy, Debug)]
pub struct Options {
    pub include_threads: bool,
    pub include_counters: bool,
    pub sort: Option<Sort>,
    pub duration_filter: Option<DurationFilter>,
}

pub fn contains_case_insensitive(text: &[u8], query: &[u8]) -> bool {
    if query.is_empty() {
        return true;
    }
    text.windows(query.len()).any(|window| {
        window
            .iter()
            .zip(query)
            .all(|(a, b)| a.eq_ignore_ascii_case(b))
    })
}
pub fn compute(
    data: &TraceData,
    query: &[u8],
    options: Options,
    mut cancelled: impl FnMut() -> bool,
) -> Option<Results> {
    debug!(
        "trace_search_task_run background task started (query: '{}')",
        String::from_utf8_lossy(query),
    );
    let mut indices = Vec::new();
    if !query.is_empty() {
        for (index, event) in data.events.iter().enumerate() {
            if index & 2047 == 0 && cancelled() {
                debug!("trace_search_task_run background task aborted");
                debug!("trace_search_task_run background task exiting");
                return None;
            }
            let phase = data.string(event.phase);
            let counter = phase == b"C";
            let metadata = phase == b"M";
            if counter && !options.include_counters {
                continue;
            }
            if !counter && !metadata && !options.include_threads {
                continue;
            }
            let matched = contains_case_insensitive(data.string(event.name), query)
                || contains_case_insensitive(data.string(event.category), query)
                || data
                    .event_args(event)
                    .iter()
                    .any(|arg| contains_case_insensitive(data.string(arg.value), query));
            if matched {
                indices.push(index as i64)
            }
        }
    }
    debug!("trace_search_task_run background task completed, generating histogram");
    let histogram = histogram::compute(&indices, data);
    let Some(display_indices) = derive_display(
        data,
        &indices,
        options.sort,
        options.duration_filter,
        &mut cancelled,
    ) else {
        debug!("trace_search_task_run background task aborted");
        debug!("trace_search_task_run background task exiting");
        return None;
    };
    debug!("trace_search_task_run background task exiting");
    Some(Results {
        event_indices: Arc::new(indices),
        display_event_indices: display_indices,
        histogram,
    })
}

pub fn derive_display(
    data: &TraceData,
    indices: &[i64],
    sort: Option<Sort>,
    duration_filter: Option<DurationFilter>,
    mut cancelled: impl FnMut() -> bool,
) -> Option<Vec<i64>> {
    let mut display_indices = Vec::with_capacity(if duration_filter.is_none() {
        indices.len()
    } else {
        0
    });
    for (offset, &index) in indices.iter().enumerate() {
        if offset & 2047 == 0 && cancelled() {
            return None;
        }
        let included = duration_filter.is_none_or(|filter| {
            usize::try_from(index)
                .ok()
                .and_then(|index| data.events.get(index))
                .is_some_and(|event| {
                    event.duration >= filter.minimum && event.duration <= filter.maximum
                })
        });
        if included {
            display_indices.push(index);
        }
    }
    if !display_indices.is_empty() && cancelled() {
        return None;
    }
    sort_event_indices(&mut display_indices, data, sort);
    if display_indices.len() > 1 && cancelled() {
        return None;
    }
    Some(display_indices)
}

fn sort_event_indices(indices: &mut [i64], data: &TraceData, sort: Option<Sort>) {
    indices.sort_unstable_by(|left, right| {
        let Some(sort) = sort else {
            return left.cmp(right);
        };
        let events = usize::try_from(*left)
            .ok()
            .and_then(|index| data.events.get(index))
            .zip(
                usize::try_from(*right)
                    .ok()
                    .and_then(|index| data.events.get(index)),
            );
        let ordering = events.map_or_else(
            || left.cmp(right),
            |(left_event, right_event)| {
                let ordering = match sort.column {
                    SortColumn::Name => data
                        .string(left_event.name)
                        .cmp(data.string(right_event.name)),
                    SortColumn::Category => data
                        .string(left_event.category)
                        .cmp(data.string(right_event.category)),
                    SortColumn::Start => left_event.timestamp.cmp(&right_event.timestamp),
                    SortColumn::Duration => left_event.duration.cmp(&right_event.duration),
                };
                ordering.then_with(|| left.cmp(right))
            },
        );
        if sort.ascending {
            ordering
        } else {
            ordering.reverse()
        }
    });
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::trace::data::{PersistedArg, PersistedEvent};

    fn add_event(
        data: &mut TraceData,
        name: &[u8],
        category: &[u8],
        phase: &[u8],
        argument: &[u8],
        timestamp: i64,
        duration: i64,
    ) {
        let args_offset = data.args.len() as u32;
        let args_count = u32::from(!argument.is_empty());
        if !argument.is_empty() {
            let key = data.intern(b"detail");
            let value = data.intern(argument);
            data.args.push(PersistedArg {
                key,
                value,
                number: 0.0,
            });
        }
        let name = data.intern(name);
        let category = data.intern(category);
        let phase = data.intern(phase);
        data.events.push(PersistedEvent {
            name,
            category,
            phase,
            args_offset,
            args_count,
            timestamp,
            duration,
            ..PersistedEvent::default()
        });
    }

    fn options() -> Options {
        Options {
            include_threads: true,
            include_counters: true,
            sort: None,
            duration_filter: None,
        }
    }

    #[test]
    fn matches_name_category_and_argument_in_event_order() {
        let mut data = TraceData::new();
        add_event(&mut data, b"Needle Name", b"cpu", b"X", b"", 30, 3);
        add_event(&mut data, b"event", b"Needle Category", b"X", b"", 20, 2);
        add_event(&mut data, b"event", b"cpu", b"X", b"Needle Argument", 10, 1);
        let result = compute(&data, b"needle", options(), || false).unwrap();
        assert_eq!(result.event_indices.as_slice(), [0, 1, 2]);
        assert_eq!(result.display_event_indices, [0, 1, 2]);
        assert_eq!(result.histogram.total_count, 3)
    }

    #[test]
    fn case_insensitive_substring() {
        assert!(contains_case_insensitive(b"Hello World", b"WORLD"));
        assert!(!contains_case_insensitive(b"short", b"long query"));
    }

    #[test]
    fn filters_threads_and_counters_but_always_includes_metadata() {
        let mut data = TraceData::new();
        add_event(&mut data, b"match", b"", b"X", b"", 0, 1);
        add_event(&mut data, b"match", b"", b"C", b"", 0, 1);
        add_event(&mut data, b"match", b"", b"M", b"", 0, 1);

        let matches = |threads, counters| {
            compute(
                &data,
                b"match",
                Options {
                    include_threads: threads,
                    include_counters: counters,
                    ..options()
                },
                || false,
            )
            .unwrap()
            .event_indices
            .as_ref()
            .clone()
        };
        assert_eq!(matches(true, true), [0, 1, 2]);
        assert_eq!(matches(true, false), [0, 2]);
        assert_eq!(matches(false, true), [1, 2]);
        assert_eq!(matches(false, false), [2]);
    }

    #[test]
    fn empty_query_returns_empty_results_without_polling_cancellation() {
        let mut polls = 0;
        let result = compute(&TraceData::new(), b"", options(), || {
            polls += 1;
            true
        })
        .unwrap();
        assert!(result.event_indices.is_empty());
        assert_eq!(result.histogram.total_count, 0);
        assert_eq!(polls, 0);
    }

    #[test]
    fn cancellation_is_polled_every_2048_events() {
        let mut data = TraceData::new();
        for _ in 0..2049 {
            add_event(&mut data, b"match", b"", b"X", b"", 0, 1);
        }
        let mut polls = 0;
        let result = compute(&data, b"match", options(), || {
            polls += 1;
            polls == 2
        });
        assert!(result.is_none());
        assert_eq!(polls, 2);
    }

    #[test]
    fn duration_filter_only_changes_background_display_results() {
        let mut data = TraceData::new();
        for duration in 1..=3 {
            add_event(&mut data, b"match", b"", b"X", b"", 0, duration);
        }
        let result = compute(
            &data,
            b"match",
            Options {
                duration_filter: Some(DurationFilter {
                    minimum: 2,
                    maximum: 2,
                }),
                ..options()
            },
            || false,
        )
        .unwrap();
        assert_eq!(result.event_indices.as_slice(), [0, 1, 2]);
        assert_eq!(result.display_event_indices, [1]);
        assert_eq!(result.histogram.total_count, 3);
    }

    #[test]
    fn cancellation_is_polled_while_deriving_display_results() {
        let mut data = TraceData::new();
        add_event(&mut data, b"match", b"", b"X", b"", 0, 1);
        let mut polls = 0;
        let result = compute(&data, b"match", options(), || {
            polls += 1;
            polls == 2
        });
        assert!(result.is_none());
        assert_eq!(polls, 2);
    }

    #[test]
    fn result_sorting_matches_all_table_columns_and_directions() {
        let mut data = TraceData::new();
        add_event(&mut data, b"beta", b"a", b"X", b"match", 30, 2);
        add_event(&mut data, b"alpha", b"c", b"X", b"match", 10, 3);
        add_event(&mut data, b"alpha", b"b", b"X", b"match", 20, 1);

        let sorted = |sort| {
            compute(&data, b"match", Options { sort, ..options() }, || false)
                .unwrap()
                .display_event_indices
        };
        assert_eq!(
            sorted(Some(Sort {
                column: SortColumn::Name,
                ascending: true,
            })),
            [1, 2, 0]
        );
        assert_eq!(
            sorted(Some(Sort {
                column: SortColumn::Name,
                ascending: false,
            })),
            [0, 2, 1]
        );
        assert_eq!(
            sorted(Some(Sort {
                column: SortColumn::Category,
                ascending: true,
            })),
            [0, 2, 1]
        );
        assert_eq!(
            sorted(Some(Sort {
                column: SortColumn::Start,
                ascending: true,
            })),
            [1, 2, 0]
        );
        assert_eq!(
            sorted(Some(Sort {
                column: SortColumn::Duration,
                ascending: false,
            })),
            [1, 0, 2]
        );
    }
}
