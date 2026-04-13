#ifndef CONFIG_H
#define CONFIG_H

#include <windows.h>
#include <stdbool.h>
#include "logger.h"

// 配置结构
typedef struct {
    // 修饰键配置
    char modifierKey[32];          // 修饰键名称
    bool suppressOriginal;         // 是否屏蔽原功能
    bool controlLed;               // 是否控制LED

    // 选项配置
    bool startEnabled;             // 启动时是否启用
    LogLevel logLevel;             // 日志级别
    bool logToFile;                // 是否写入文件
    char keyboardLayout[32];       // 键盘布局

    // 语音输入配置
    bool voiceInputEnabled;        // 是否启用语音输入
    bool voiceInputAsked;          // 用户是否已经选择过是否启用

    // 自定义路径配置
    char logDirPath[MAX_PATH];     // 日志存储目录
    char modelDirPath[MAX_PATH];   // 模型存储目录

    // 路径
    char configPath[MAX_PATH];     // 配置文件路径
    char logPath[MAX_PATH];        // 日志文件路径（完整路径含文件名）
} Config;

// 初始化配置模块
void ConfigInit(void);

// 清理配置模块
void ConfigCleanup(void);

// 加载配置文件
// path: 配置文件路径，如果为NULL则使用默认路径
// 返回: true成功，false失败
bool ConfigLoad(const char* path);

// 保存配置文件
// path: 配置文件路径，如果为NULL则使用当前路径
// 返回: true成功，false失败
bool ConfigSave(const char* path);

// 获取当前配置
const Config* ConfigGet(void);

// 设置配置
void ConfigSet(const Config* config);

// 加载默认配置
void ConfigLoadDefaults(void);

// 获取配置文件路径
const char* ConfigGetPath(void);

// 获取日志文件路径
const char* ConfigGetLogPath(void);

WORD ConfigKeyNameToScanCode(const char* name);
UINT ConfigKeyNameToVkCode(const char* name);

// Actions 配置接口
#include "action.h"
bool ConfigLoadActions(const char* json);
char* ConfigSaveActions(void);
void ConfigMigrateMappingsToActions(void);

#endif // CONFIG_H
