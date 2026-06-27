#ifndef SRC_PLATFORM_H
#define SRC_PLATFORM_H

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
bool platform_is_main_thread(void);

// Settings persistence
void platform_set_setting(const char* key, const char* value);
bool platform_get_setting(const char* key, char* out_val, int max_len);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
typedef platform_job_fn_t PlatformJobFn;
#endif

#endif  // SRC_PLATFORM_H
