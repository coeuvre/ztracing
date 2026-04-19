#ifndef ZTRACING_SRC_PLATFORM_H_
#define ZTRACING_SRC_PLATFORM_H_

#ifdef __cplusplus
extern "C" {
#endif

// Returns the current time in milliseconds.
double platform_get_now();

// Returns true if the system's preferred color scheme is dark.
bool platform_is_dark_mode();

#ifdef __cplusplus
}
#endif

#endif  // ZTRACING_SRC_PLATFORM_H_
