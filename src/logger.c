#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <windows.h>
#include <locale.h>

// 日志系统状态
typedef struct {
    FILE* logFile;          // 日志文件句柄
    LogLevel level;         // 当前日志级别
    CRITICAL_SECTION cs;    // 临界区，保证线程安全
    BOOL initialized;       // 是否已初始化
    char logDir[MAX_PATH];  // 日志目录路径
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

    // 设置 locale 为 UTF-8
    setlocale(LC_ALL, ".UTF-8");

    // 初始化临界区
    InitializeCriticalSection(&g_logger.cs);

    // 设置默认级别
    g_logger.level = LOG_LEVEL_INFO;
    g_logger.logDir[0] = '\0';

    // 打开日志文件
    if (logPath != NULL) {
        // 提取日志目录并存储它
        char dirPath[MAX_PATH];
        strncpy(dirPath, logPath, MAX_PATH - 1);
        char* lastSlash = strrchr(dirPath, '\\');
        if (lastSlash != NULL) {
            *lastSlash = '\0';
            strncpy(g_logger.logDir, dirPath, sizeof(g_logger.logDir) - 1);
            CreateDirectoryA(dirPath, NULL);
        }

        // 检查文件是否存在，用于决定是否写入 BOM
        BOOL fileExists = (GetFileAttributesA(logPath) != INVALID_FILE_ATTRIBUTES);

        // 以二进制模式打开，写入 UTF-8
        g_logger.logFile = fopen(logPath, "ab");
        if (g_logger.logFile == NULL) {
            // 文件打开失败，只使用控制台输出
            g_logger.logFile = NULL;
        } else {
            // 如果是新文件，写入 UTF-8 BOM
            if (!fileExists) {
                unsigned char bom[] = {0xEF, 0xBB, 0xBF};
                fwrite(bom, 1, 3, g_logger.logFile);
            }
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

// 清理旧日志，只保留最近 keepDays 天
void LoggerCleanupOldLogs(int keepDays) {
    if (!g_logger.initialized || g_logger.logDir[0] == '\0') {
        LOG_DEBUG("Logger not initialized or log directory not available, skipping cleanup");
        return;
    }

    // Calculate cutoff time
    time_t now = time(NULL);
    time_t cutoff = now - (time_t)(keepDays * 24 * 60 * 60);

    // Search pattern: all *.log files in log directory
    char searchPath[MAX_PATH];
    snprintf(searchPath, sizeof(searchPath), "%s\\*.log", g_logger.logDir);

    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath, &findData);

    if (hFind == INVALID_HANDLE_VALUE) {
        LOG_DEBUG("No log files found for cleanup");
        return;
    }

    int deletedCount = 0;
    int totalFiles = 0;

    do {
        // Skip directories
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }

        totalFiles++;

        // Get last write time
        FILETIME ft = findData.ftLastWriteTime;
        LARGE_INTEGER li;
        li.LowPart = ft.dwLowDateTime;
        li.HighPart = ft.dwHighDateTime;

        // Convert FILETIME to time_t
        // FILETIME is 100-nanosecond intervals since January 1, 1601 (UTC)
        // time_t is seconds since January 1, 1970 (UTC)
        const ULONGLONG TIME_DIFF = 116444736000000000LL;
        ULONGLONG fileTime = li.QuadPart;
        fileTime -= TIME_DIFF;
        fileTime /= 10000000;  // convert 100ns to seconds
        time_t fileAge = (time_t)fileTime;

        // Check if file is older than cutoff
        if (fileAge < cutoff) {
            // Delete old file
            char filePath[MAX_PATH];
            snprintf(filePath, sizeof(filePath), "%s\\%s", g_logger.logDir, findData.cFileName);
            if (DeleteFileA(filePath)) {
                deletedCount++;
                LOG_DEBUG("Deleted old log file: %s", findData.cFileName);
            } else {
                LOG_WARN("Failed to delete old log file: %s", filePath);
            }
        }

    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);

    if (deletedCount > 0) {
        LOG_INFO("Log cleanup completed: deleted %d old log files (kept last %d days)", deletedCount, keepDays);
    } else {
        LOG_DEBUG("Log cleanup completed: no old files deleted, %d total files", totalFiles);
    }
}
