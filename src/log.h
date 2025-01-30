#ifndef ZTRACING_SRC_LOG_H_
#define ZTRACING_SRC_LOG_H_

typedef enum LogLevel {
  kLogLevelDebug,
  kLogLevelInfo,
  kLogLevelWarn,
  kLogLevelError,
  kLogLevelCount,
} LogLevel;

void log_message(LogLevel level, const char *fmt, ...);

#define LOG(level, fmt, ...) \
  log_message(level, "%s:%d: " fmt, __FILE__, __LINE__, ##__VA_ARGS__)

#define DEBUG(fmt, ...) LOG(kLogLevelDebug, "debug: " fmt, ##__VA_ARGS__)
#define INFO(fmt, ...) LOG(kLogLevelInfo, "info: " fmt, ##__VA_ARGS__)
#define WARN(fmt, ...) LOG(kLogLevelWarn, "warn: " fmt, ##__VA_ARGS__)
#define ERROR(fmt, ...) LOG(kLogLevelError, "error: " fmt, ##__VA_ARGS__)

#endif  // ZTRACING_SRC_LOG_H_
