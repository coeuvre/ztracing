use super::TraceData;

pub const MAX_BINS: usize = 32;

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct Bucket {
    pub min_duration: i64,
    pub max_duration: i64,
    pub count: u32,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct Histogram {
    pub buckets: Vec<Bucket>,
    pub max_bucket_count: u32,
    pub total_count: u32,
    pub has_non_zero_durations: bool,
}

pub fn compute(results: &[i64], data: &TraceData) -> Histogram {
    let mut output = Histogram {
        total_count: results.len() as u32,
        ..Histogram::default()
    };
    if results.is_empty() {
        return output;
    }
    let mut minimum = None::<i64>;
    let mut maximum = None::<i64>;
    let mut zero_count = 0_u32;
    for &result in results {
        let Ok(index) = usize::try_from(result) else {
            continue;
        };
        let Some(event) = data.events.get(index) else {
            continue;
        };
        if event.duration <= 0 {
            zero_count += 1;
        } else {
            minimum = Some(minimum.map_or(event.duration, |value| value.min(event.duration)));
            maximum = Some(maximum.map_or(event.duration, |value| value.max(event.duration)));
        }
    }
    if zero_count > 0 {
        output.buckets.push(Bucket {
            min_duration: 0,
            max_duration: 0,
            count: zero_count,
        });
        output.max_bucket_count = zero_count;
    }
    let (Some(minimum), Some(maximum)) = (minimum, maximum) else {
        return output;
    };
    output.has_non_zero_durations = true;
    let range = maximum - minimum;
    let mut bins = 20_i64;
    let mut logarithmic = minimum > 0 && maximum > 0 && maximum as f64 / minimum as f64 > 100.0;
    if range < bins {
        bins = range + 1;
        logarithmic = false;
    }
    bins = bins.min((MAX_BINS - output.buckets.len()) as i64);
    let log_min = (minimum as f64).log10();
    let log_width = ((maximum as f64).log10() - log_min) / bins as f64;
    let mut limits = Vec::<i128>::with_capacity(bins as usize + 1);
    for index in 0..=bins {
        let mut value = if index == bins {
            i128::from(maximum) + 1
        } else if logarithmic {
            i128::from(10_f64.powf(log_min + index as f64 * log_width).round() as i64)
        } else {
            i128::from(minimum) + i128::from(range) * i128::from(index) / i128::from(bins)
        };
        if let Some(previous) = limits.last() {
            if value <= *previous {
                value = *previous + 1;
            }
        }
        limits.push(value);
    }
    let start = output.buckets.len();
    for pair in limits.windows(2) {
        output.buckets.push(Bucket {
            min_duration: i64::try_from(pair[0]).expect("histogram minimum exceeds i64"),
            max_duration: i64::try_from(pair[1] - 1).expect("histogram maximum exceeds i64"),
            count: 0,
        });
    }
    for &result in results {
        let Ok(index) = usize::try_from(result) else {
            continue;
        };
        let Some(event) = data.events.get(index) else {
            continue;
        };
        if event.duration <= 0 {
            continue;
        }
        if let Some(bucket) = output.buckets[start..].iter_mut().find(|bucket| {
            event.duration >= bucket.min_duration && event.duration <= bucket.max_duration
        }) {
            bucket.count += 1;
            output.max_bucket_count = output.max_bucket_count.max(bucket.count);
        }
    }
    output
}

#[cfg(test)]
mod tests {
    use super::{Bucket, MAX_BINS, compute};
    use crate::trace::{PersistedEvent, TraceData};

    fn data(durations: &[i64]) -> TraceData {
        let mut data = TraceData::new();
        data.events = durations
            .iter()
            .map(|&duration| PersistedEvent {
                duration,
                ..PersistedEvent::default()
            })
            .collect();
        data
    }

    #[test]
    fn compute_histogram() {
        let data = data(&[0, 50, 5_000]);
        let histogram = compute(&[0, 1, 2], &data);
        assert!(histogram.buckets.len() >= 2);
        assert!(histogram.has_non_zero_durations);
        assert_eq!(
            histogram.buckets[0],
            Bucket {
                min_duration: 0,
                max_duration: 0,
                count: 1,
            }
        );
    }

    #[test]
    fn compute_histogram_empty_results() {
        let histogram = compute(&[], &TraceData::new());
        assert!(histogram.buckets.is_empty());
        assert_eq!(histogram.total_count, 0);
        assert!(!histogram.has_non_zero_durations);
    }

    #[test]
    fn compute_histogram_linear_vs_logarithmic() {
        let data = data(&[10, 100, 200, 2, 1_000, 100_000]);
        let linear = compute(&[0, 1, 2], &data);
        let first = linear.buckets[0].max_duration - linear.buckets[0].min_duration;
        let second = linear.buckets[1].max_duration - linear.buckets[1].min_duration;
        assert!((first - second).abs() <= 5);

        let logarithmic = compute(&[3, 4, 5], &data);
        let first = logarithmic.buckets[0].max_duration - logarithmic.buckets[0].min_duration;
        let last = logarithmic.buckets.last().unwrap().max_duration
            - logarithmic.buckets.last().unwrap().min_duration;
        assert!(last > first * 100);
    }

    #[test]
    fn compute_histogram_narrow_range_bin_clamping() {
        let data = data(&[10, 13]);
        let histogram = compute(&[0, 1], &data);
        assert_eq!(histogram.buckets.len(), 4);
        assert_eq!(histogram.buckets[0].min_duration, 10);
        assert_eq!(histogram.buckets[3].max_duration, 13);
    }

    #[test]
    fn compute_histogram_invalid_index_ignored() {
        let histogram = compute(&[99_999, -1], &TraceData::new());
        assert!(histogram.buckets.is_empty());
        assert_eq!(histogram.total_count, 2);
        assert!(!histogram.has_non_zero_durations);
    }

    #[test]
    fn non_positive_durations_share_the_zero_bucket() {
        let data = data(&[0, -10]);
        let histogram = compute(&[0, 1], &data);
        assert_eq!(
            histogram.buckets,
            [Bucket {
                min_duration: 0,
                max_duration: 0,
                count: 2,
            }]
        );
        assert!(!histogram.has_non_zero_durations);
    }

    #[test]
    fn repeated_duration_uses_one_bucket() {
        let data = data(&[42, 42]);
        let histogram = compute(&[0, 1], &data);
        assert_eq!(
            histogram.buckets,
            [Bucket {
                min_duration: 42,
                max_duration: 42,
                count: 2,
            }]
        );
    }

    #[test]
    fn maximum_duration_does_not_overflow_the_final_boundary() {
        let data = data(&[i64::MAX]);
        let histogram = compute(&[0], &data);
        assert_eq!(
            histogram.buckets,
            [Bucket {
                min_duration: i64::MAX,
                max_duration: i64::MAX,
                count: 1,
            }]
        );
    }

    #[test]
    fn wide_linear_range_uses_non_overflowing_intermediates() {
        let minimum = i64::MAX / 2;
        let data = data(&[minimum, i64::MAX]);
        let histogram = compute(&[0, 1], &data);
        assert_eq!(histogram.buckets.len(), 20.min(MAX_BINS));
        assert_eq!(histogram.buckets[0].min_duration, minimum);
        assert_eq!(histogram.buckets.last().unwrap().max_duration, i64::MAX);
        assert_eq!(
            histogram
                .buckets
                .iter()
                .map(|bucket| bucket.count)
                .sum::<u32>(),
            2
        );
    }
}
