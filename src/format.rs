pub fn duration(microseconds: f64, interval_microseconds: f64) -> String {
    if microseconds == 0.0 {
        return "0".to_owned();
    }
    let interval = if interval_microseconds.abs() == 0.0 {
        microseconds.abs()
    } else {
        interval_microseconds.abs()
    };
    let (value, unit) = if interval >= 1_000_000.0 {
        (microseconds / 1_000_000.0, "s")
    } else if interval >= 1_000.0 {
        (microseconds / 1_000.0, "ms")
    } else {
        (microseconds, "us")
    };
    let mut number = format!("{value:.2}");
    while number.ends_with('0') {
        number.pop();
    }
    if number.ends_with('.') {
        number.pop();
    }
    format!("{number} {unit}")
}

pub fn tick_interval(duration: f64, width: f64, minimum_tick_width: f64) -> f64 {
    if duration <= 0.0 || width <= 0.0 {
        return 1.0;
    }
    let maximum_ticks = ((width / minimum_tick_width) as i32).max(2);
    let raw = duration / f64::from(maximum_ticks);
    let power = 10_f64.powf(raw.log10().floor());
    let mantissa = raw / power;
    let result = if mantissa < 1.5 {
        power
    } else if mantissa < 3.5 {
        2.0 * power
    } else if mantissa < 7.5 {
        5.0 * power
    } else {
        10.0 * power
    };
    result.max(1.0)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn format_duration() {
        assert_eq!(duration(0.0, 0.0), "0");
        assert_eq!(duration(500.0, 0.0), "500 us");
        assert_eq!(duration(1_000.0, 0.0), "1 ms");
        assert_eq!(duration(1_500.0, 0.0), "1.5 ms");
        assert_eq!(duration(1_000_000.0, 0.0), "1 s");
        assert_eq!(duration(2_500_000.0, 0.0), "2.5 s");
    }

    #[test]
    fn format_duration_negative() {
        assert_eq!(duration(-500.0, 0.0), "-500 us");
        assert_eq!(duration(-1_000.0, 0.0), "-1 ms");
        assert_eq!(duration(-1_000_000.0, 0.0), "-1 s");
    }

    #[test]
    fn format_duration_with_interval() {
        assert_eq!(duration(136_567_000.0, 1_000_000.0), "136.57 s");
        assert_eq!(duration(136_567_000.0, 1_000.0), "136567 ms");
        assert_eq!(duration(136_567_000.0, 10.0), "136567000 us");
        assert_eq!(duration(1_500.0, -1_000.0), "1.5 ms");
    }

    #[test]
    fn format_duration_unit_boundaries() {
        assert_eq!(duration(999.0, 999.0), "999 us");
        assert_eq!(duration(1_000.0, 1_000.0), "1 ms");
        assert_eq!(duration(999_999.0, 999_999.0), "1000 ms");
        assert_eq!(duration(1_000_000.0, 1_000_000.0), "1 s");
    }

    #[test]
    fn calculate_tick_interval() {
        for (duration, width, minimum, expected) in [
            (1_000_000.0, 1_000.0, 100.0, 100_000.0),
            (1_000_000.0, 1_000.0, 250.0, 200_000.0),
            (0.0, 1_000.0, 100.0, 1.0),
            (-100.0, 1_000.0, 100.0, 1.0),
            (1_000_000.0, 0.0, 100.0, 1.0),
            (1_000_000.0, -50.0, 100.0, 1.0),
            (1_000.0, 100.0, 200.0, 500.0),
            (0.5, 1_000.0, 100.0, 1.0),
            (5.0, 1_000.0, 100.0, 1.0),
            (140.0, 100.0, 10.0, 10.0),
            (150.0, 100.0, 10.0, 20.0),
            (160.0, 100.0, 10.0, 20.0),
            (340.0, 100.0, 10.0, 20.0),
            (350.0, 100.0, 10.0, 50.0),
            (360.0, 100.0, 10.0, 50.0),
            (740.0, 100.0, 10.0, 50.0),
            (750.0, 100.0, 10.0, 100.0),
            (760.0, 100.0, 10.0, 100.0),
            (1e15, 1_000.0, 100.0, 1e14),
        ] {
            assert_eq!(tick_interval(duration, width, minimum), expected);
        }
    }
}
