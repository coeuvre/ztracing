#include "src/format.h"

#include <math.h>
#include <stdio.h>

void format_duration(char* buf, size_t buf_size, double us,
                     double interval_us) {
  if (us == 0.0) {
    snprintf(buf, buf_size, "0");
    return;
  }

  double abs_interval = (interval_us < 0) ? -interval_us : interval_us;
  if (abs_interval == 0) {
    abs_interval = (us < 0) ? -us : us;
  }

  if (abs_interval >= 1000000.0) {
    snprintf(buf, buf_size, "%.10g s", us / 1000000.0);
  } else if (abs_interval >= 1000.0) {
    snprintf(buf, buf_size, "%.10g ms", us / 1000.0);
  } else {
    snprintf(buf, buf_size, "%.10g us", us);
  }
}

double calculate_tick_interval(double duration, double width,
                               double min_tick_width) {
  if (duration <= 0 || width <= 0) return 1.0;

  int max_ticks = (int)(width / min_tick_width);
  if (max_ticks < 2) max_ticks = 2;

  double tick_interval = duration / max_ticks;

  // Round to nice interval: 1, 2, 5 * 10^n
  double p = pow(10.0, floor(log10(tick_interval)));
  double m = tick_interval / p;
  if (m < 1.5)
    tick_interval = 1.0 * p;
  else if (m < 3.5)
    tick_interval = 2.0 * p;
  else if (m < 7.5)
    tick_interval = 5.0 * p;
  else
    tick_interval = 10.0 * p;

  if (tick_interval < 1.0) tick_interval = 1.0;
  return tick_interval;
}
