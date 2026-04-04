#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>

// 日志级别枚举
typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_WARN = 2,
    LOG_LEVEL_ERROR = 3
} LogLevel;

// 初始化日志系统
// logPath: 日志文件路径，如果为NULL则只输出到控制台
// 返回: 0成功，-1失败
int LoggerInit(const char* logPath);

// 清理日志系统
void LoggerCleanup(void);

// 设置日志级别
void LoggerSetLevel(LogLevel level);

// 获取当前日志级别
LogLevel LoggerGetLevel(void);

// 记录日志消息
void LogMessage(LogLevel level, const char* format, ...);

// 便捷宏定义
#define LOG_DEBUG(format, ...) LogMessage(LOG_LEVEL_DEBUG, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...)  LogMessage(LOG_LEVEL_INFO, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...)  LogMessage(LOG_LEVEL_WARN, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) LogMessage(LOG_LEVEL_ERROR, format, ##__VA_ARGS__)

#endif // LOGGER_H
