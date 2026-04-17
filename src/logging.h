#ifndef ZTRACING_SRC_LOGGING_H_
#define ZTRACING_SRC_LOGGING_H_

enum LogLevel {
  LOG_LEVEL_DEBUG,
  LOG_LEVEL_INFO,
  LOG_LEVEL_WARN,
  LOG_LEVEL_ERROR,
};

void log_message(LogLevel level, const char* format, ...)
    __attribute__((format(printf, 2, 3)));

#ifndef LOG_LEVEL
#ifdef NDEBUG
#define LOG_LEVEL LOG_LEVEL_INFO
#else
#define LOG_LEVEL LOG_LEVEL_DEBUG
#endif
#endif

#if LOG_LEVEL <= LOG_LEVEL_DEBUG
#define LOG_DEBUG(format, ...) \
  log_message(LOG_LEVEL_DEBUG, format __VA_OPT__(, ) __VA_ARGS__)
#else
#define LOG_DEBUG(format, ...) ((void)0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_INFO
#define LOG_INFO(format, ...) \
  log_message(LOG_LEVEL_INFO, format __VA_OPT__(, ) __VA_ARGS__)
#else
#define LOG_INFO(format, ...) ((void)0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_WARN
#define LOG_WARN(format, ...) \
  log_message(LOG_LEVEL_WARN, format __VA_OPT__(, ) __VA_ARGS__)
#else
#define LOG_WARN(format, ...) ((void)0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_ERROR
#define LOG_ERROR(format, ...) \
  log_message(LOG_LEVEL_ERROR, format __VA_OPT__(, ) __VA_ARGS__)
#else
#define LOG_ERROR(format, ...) ((void)0)
#endif

#endif  // ZTRACING_SRC_LOGGING_H_
