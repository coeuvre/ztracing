#ifndef ZTRACING_SRC_PLATFORM_H_
#define ZTRACING_SRC_PLATFORM_H_

#ifdef __cplusplus
extern "C" {
#endif

// Returns the current time in milliseconds.
double platform_get_now();

// Returns true if the system's preferred color scheme is dark.
bool platform_is_dark_mode();

// Returns true if the platform is macOS.
bool platform_is_mac();

typedef void (*platform_job_fn_t)(void* user_data);
void platform_submit_job(platform_job_fn_t fn, void* user_data);
void platform_teardown_workers();
void platform_open_file_dialog();

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
typedef platform_job_fn_t PlatformJobFn;
#endif

#endif  // ZTRACING_SRC_PLATFORM_H_
