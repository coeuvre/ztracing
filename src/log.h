#ifndef ZTRACING_SRC_LOG_H_
#define ZTRACING_SRC_LOG_H_

typedef enum LogLevel {
  LogLevel_Debug,
  LogLevel_Info,
  LogLevel_Warn,
  LogLevel_Error,

  LogLevel_Count,
} LogLevel;

void LogMessage(LogLevel level, const char *fmt, ...);

#define LOG(level, fmt, ...) LogMessage(level, fmt, ##__VA_ARGS__)

#define DEBUG(fmt, ...) LOG(LogLevel_Debug, "debug: " fmt, ##__VA_ARGS__)
#define INFO(fmt, ...) LOG(LogLevel_Info, "info: " fmt, ##__VA_ARGS__)
#define WARN(fmt, ...) LOG(LogLevel_Warn, "warn: " fmt, ##__VA_ARGS__)
#define ERROR(fmt, ...) LOG(LogLevel_Error, "error: " fmt, ##__VA_ARGS__)

#endif  // ZTRACING_SRC_LOG_H_
