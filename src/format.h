#ifndef ZTRACING_SRC_FORMAT_H_
#define ZTRACING_SRC_FORMAT_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Formats a duration in microseconds to a human-readable string (s, ms, us).
// The interval_us is used to determine the appropriate unit. Passing 0.0 is
// allowed and will cause the duration itself to be used as the reference unit.
void format_duration(char* buf, size_t buf_size, double us, double interval_us);

// Calculates a "nice" tick interval (1, 2, 5 * 10^n) for a given duration and
// width.
double calculate_tick_interval(double duration, double width,
                               double min_tick_width);

#ifdef __cplusplus
}
#endif

#endif  // ZTRACING_SRC_FORMAT_H_
