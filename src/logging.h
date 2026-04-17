#ifndef ZTRACING_LOGGING_H_
#define ZTRACING_LOGGING_H_

enum LogLevel {
  DEBUG,
  INFO,
  WARN,
  ERROR,
};

void Log(LogLevel level, const char* format, ...)
    __attribute__((format(printf, 2, 3)));

#ifndef LOG_LEVEL
#ifdef NDEBUG
#define LOG_LEVEL INFO
#else
#define LOG_LEVEL DEBUG
#endif
#endif

#if LOG_LEVEL <= DEBUG
#define LOG_DEBUG(format, ...) Log(DEBUG, format, ##__VA_ARGS__)
#else
#define LOG_DEBUG(format, ...) ((void)0)
#endif

#if LOG_LEVEL <= INFO
#define LOG_INFO(format, ...) Log(INFO, format, ##__VA_ARGS__)
#else
#define LOG_INFO(format, ...) ((void)0)
#endif

#if LOG_LEVEL <= WARN
#define LOG_WARN(format, ...) Log(WARN, format, ##__VA_ARGS__)
#else
#define LOG_WARN(format, ...) ((void)0)
#endif

#if LOG_LEVEL <= ERROR
#define LOG_ERROR(format, ...) Log(ERROR, format, ##__VA_ARGS__)
#else
#define LOG_ERROR(format, ...) ((void)0)
#endif

#endif  // ZTRACING_LOGGING_H_
