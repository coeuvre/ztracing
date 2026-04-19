#ifndef ZTRACING_SRC_FORMAT_H_
#define ZTRACING_SRC_FORMAT_H_

#include <stddef.h>

// Formats a duration in microseconds to a human-readable string (s, ms, us).
// The interval_us is used to determine the appropriate unit.
void format_duration(char* buf, size_t buf_size, double us,
                     double interval_us = 0);

// Calculates a "nice" tick interval (1, 2, 5 * 10^n) for a given duration and
// width.
double calculate_tick_interval(double duration, double width,
                               double min_tick_width);

#endif  // ZTRACING_SRC_FORMAT_H_
