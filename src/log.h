#ifndef ZTRACING_SRC_LOG_H_
#define ZTRACING_SRC_LOG_H_

typedef enum LogLevel {
  LOG_LEVEL_DEBUG,
  LOG_LEVEL_INFO,
  LOG_LEVEL_WARN,
  LOG_LEVEL_ERROR,

  LOG_LEVEL_COUNT,
} LogLevel;

void log_message(LogLevel level, const char *fmt, ...);

#define LOG(level, fmt, ...) log_message(level, fmt, ##__VA_ARGS__)

#define DEBUG(fmt, ...) LOG(LOG_LEVEL_DEBUG, "debug: " fmt, ##__VA_ARGS__)
#define INFO(fmt, ...) LOG(LOG_LEVEL_INFO, "info: " fmt, ##__VA_ARGS__)
#define WARN(fmt, ...) LOG(LOG_LEVEL_WARN, "warn: " fmt, ##__VA_ARGS__)
#define ERROR(fmt, ...) LOG(LOG_LEVEL_ERROR, "error: " fmt, ##__VA_ARGS__)

#endif  // ZTRACING_SRC_LOG_H_
