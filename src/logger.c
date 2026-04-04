#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <windows.h>

// 日志系统状态
typedef struct {
    FILE* logFile;          // 日志文件句柄
    LogLevel level;         // 当前日志级别
    CRITICAL_SECTION cs;    // 临界区，保证线程安全
    BOOL initialized;       // 是否已初始化
} LoggerState;

static LoggerState g_logger = {0};

// 日志级别名称
static const char* levelNames[] = {
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR"
};

int LoggerInit(const char* logPath) {
    if (g_logger.initialized) {
        return 0;  // 已经初始化
    }

    // 初始化临界区
    InitializeCriticalSection(&g_logger.cs);

    // 设置默认级别
    g_logger.level = LOG_LEVEL_INFO;

    // 打开日志文件
    if (logPath != NULL) {
        // 确保目录存在
        char dirPath[MAX_PATH];
        strncpy(dirPath, logPath, MAX_PATH - 1);
        char* lastSlash = strrchr(dirPath, '\\');
        if (lastSlash != NULL) {
            *lastSlash = '\0';
            CreateDirectoryA(dirPath, NULL);
        }

        g_logger.logFile = fopen(logPath, "a");
        if (g_logger.logFile == NULL) {
            // 文件打开失败，只使用控制台输出
            g_logger.logFile = NULL;
        }
    }

    g_logger.initialized = TRUE;
    return 0;
}

void LoggerCleanup(void) {
    if (!g_logger.initialized) {
        return;
    }

    EnterCriticalSection(&g_logger.cs);

    if (g_logger.logFile != NULL) {
        fflush(g_logger.logFile);
        fclose(g_logger.logFile);
        g_logger.logFile = NULL;
    }

    LeaveCriticalSection(&g_logger.cs);
    DeleteCriticalSection(&g_logger.cs);
    g_logger.initialized = FALSE;
}

void LoggerSetLevel(LogLevel level) {
    if (level >= LOG_LEVEL_DEBUG && level <= LOG_LEVEL_ERROR) {
        g_logger.level = level;
    }
}

LogLevel LoggerGetLevel(void) {
    return g_logger.level;
}

void LogMessage(LogLevel level, const char* format, ...) {
    if (!g_logger.initialized) {
        return;
    }

    // 检查日志级别
    if (level < g_logger.level) {
        return;
    }

    EnterCriticalSection(&g_logger.cs);

    // 获取当前时间
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    char timeStr[32];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", t);

    // 格式化用户消息
    char message[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    // 构建完整日志行
    char logLine[2048];
    snprintf(logLine, sizeof(logLine), "[%s] [%s] %s\n",
             timeStr, levelNames[level], message);

    // 输出到控制台
    printf("%s", logLine);

    // 输出到文件
    if (g_logger.logFile != NULL) {
        fprintf(g_logger.logFile, "%s", logLine);
        fflush(g_logger.logFile);
    }

    LeaveCriticalSection(&g_logger.cs);
}
