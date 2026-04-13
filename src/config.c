#include "config.h"
#include "keymap.h"
#include "logger.h"
#include "action.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <shlobj.h>

static Config g_config = {0};

// 按键名称到扫描码的映射表
typedef struct {
    const char* name;
    WORD scanCode;
    UINT vkCode;
} KeyNameEntry;

static const KeyNameEntry keyNameTable[] = {
    // 字母键 (基于美式键盘物理布局)
    {"A", 0x1E, 0x41}, {"B", 0x30, 0x42}, {"C", 0x2E, 0x43}, {"D", 0x20, 0x44},
    {"E", 0x12, 0x45}, {"F", 0x21, 0x46}, {"G", 0x22, 0x47}, {"H", 0x23, 0x48},
    {"I", 0x17, 0x49}, {"J", 0x24, 0x4A}, {"K", 0x25, 0x4B}, {"L", 0x26, 0x4C},
    {"M", 0x32, 0x4D}, {"N", 0x31, 0x4E}, {"O", 0x18, 0x4F}, {"P", 0x19, 0x50},
    {"Q", 0x10, 0x51}, {"R", 0x13, 0x52}, {"S", 0x1F, 0x53}, {"T", 0x14, 0x54},
    {"U", 0x16, 0x55}, {"V", 0x2F, 0x56}, {"W", 0x11, 0x57}, {"X", 0x2D, 0x58},
    {"Y", 0x15, 0x59}, {"Z", 0x2C, 0x5A},

    // 数字键
    {"1", 0x02, 0x31}, {"2", 0x03, 0x32}, {"3", 0x04, 0x33}, {"4", 0x05, 0x34},
    {"5", 0x06, 0x35}, {"6", 0x07, 0x36}, {"7", 0x08, 0x37}, {"8", 0x09, 0x38},
    {"9", 0x0A, 0x39}, {"0", 0x0B, 0x30},

    // 符号键
    {"MINUS", 0x0C, 0xBD}, {"EQUAL", 0x0D, 0xBB},
    {"LBRACKET", 0x1A, 0xDB}, {"RBRACKET", 0x1B, 0xDD},
    {"BACKSLASH", 0x2B, 0xDC}, {"SEMICOLON", 0x27, 0xBA},
    {"QUOTE", 0x28, 0xDE}, {"COMMA", 0x33, 0xBC},
    {"PERIOD", 0x34, 0xBE}, {"SLASH", 0x35, 0xBF},
    {"BACKQUOTE", 0x29, 0xC0},

    // 功能键
    {"F1", 0x3B, VK_F1}, {"F2", 0x3C, VK_F2}, {"F3", 0x3D, VK_F3}, {"F4", 0x3E, VK_F4},
    {"F5", 0x3F, VK_F5}, {"F6", 0x40, VK_F6}, {"F7", 0x41, VK_F7}, {"F8", 0x42, VK_F8},
    {"F9", 0x43, VK_F9}, {"F10", 0x44, VK_F10}, {"F11", 0x57, VK_F11}, {"F12", 0x58, VK_F12},

    // 导航键
    {"HOME", 0xE047, VK_HOME}, {"END", 0xE04F, VK_END},
    {"PAGEUP", 0xE049, VK_PRIOR}, {"PAGEDOWN", 0xE051, VK_NEXT},
    {"INSERT", 0xE052, VK_INSERT}, {"DELETE", 0xE053, VK_DELETE},
    {"UP", 0xE048, VK_UP}, {"DOWN", 0xE050, VK_DOWN},
    {"LEFT", 0xE04B, VK_LEFT}, {"RIGHT", 0xE04D, VK_RIGHT},

    // 控制键
    {"ESCAPE", 0x01, VK_ESCAPE}, {"TAB", 0x0F, VK_TAB},
    {"CAPSLOCK", 0x3A, VK_CAPITAL}, {"SPACE", 0x39, VK_SPACE},
    {"ENTER", 0x1C, VK_RETURN}, {"BACKSPACE", 0x0E, VK_BACK},

    {NULL, 0, 0}  // 结束标记
};

// 根据名称查找扫描码
static WORD NameToScanCode(const char* name) {
    for (int i = 0; keyNameTable[i].name != NULL; i++) {
        if (_stricmp(keyNameTable[i].name, name) == 0) {
            return keyNameTable[i].scanCode;
        }
    }
    return 0;
}

// 根据名称查找VK码
static UINT NameToVkCode(const char* name) {
    for (int i = 0; keyNameTable[i].name != NULL; i++) {
        if (_stricmp(keyNameTable[i].name, name) == 0) {
            return keyNameTable[i].vkCode;
        }
    }
    return 0;
}

// 根据扫描码查找键名（反向查找，用于保存配置）
static const char* ScanCodeToName(WORD scanCode) {
    for (int i = 0; keyNameTable[i].name != NULL; i++) {
        if (keyNameTable[i].scanCode == scanCode) {
            return keyNameTable[i].name;
        }
    }
    return NULL;
}

// 根据VK码查找键名（反向查找，用于保存配置）
static const char* VkCodeToName(UINT vkCode) {
    for (int i = 0; keyNameTable[i].name != NULL; i++) {
        if (keyNameTable[i].vkCode == vkCode) {
            return keyNameTable[i].name;
        }
    }
    return NULL;
}

// 简单的JSON解析辅助函数
static char* ReadFileContent(const char* path) {
    FILE* file = fopen(path, "r");
    if (file == NULL) {
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* content = (char*)malloc(size + 1);
    if (content == NULL) {
        fclose(file);
        return NULL;
    }

    size_t read = fread(content, 1, size, file);
    content[read] = '\0';
    fclose(file);

    return content;
}

static bool WriteFileContent(const char* path, const char* content) {
    FILE* file = fopen(path, "w");
    if (file == NULL) {
        return false;
    }

    fprintf(file, "%s", content);
    fclose(file);
    return true;
}

// 创建目录（支持多级目录）
static bool CreateDirectoryRecursive(const char* path) {
    char tmp[MAX_PATH];
    char* p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);

    // 移除末尾的斜杠
    if (tmp[len - 1] == '\\' || tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }

    // 逐级创建目录
    for (p = tmp + 1; *p; p++) {
        if (*p == '\\' || *p == '/') {
            *p = 0;
            // 尝试创建目录，忽略已存在的错误
            CreateDirectoryA(tmp, NULL);
            *p = '\\';
        }
    }
    // 创建最后一级目录
    CreateDirectoryA(tmp, NULL);

    // 检查目录是否存在
    DWORD attrib = GetFileAttributesA(tmp);
    return (attrib != INVALID_FILE_ATTRIBUTES && (attrib & FILE_ATTRIBUTE_DIRECTORY));
}

// 获取配置文件所在目录
static void GetConfigDir(char* dir, size_t size) {
    strncpy(dir, g_config.configPath, size);
    char* lastSlash = strrchr(dir, '\\');
    if (lastSlash != NULL) {
        *lastSlash = '\0';
    }
}

// 获取日志文件所在目录
static void GetLogDir(char* dir, size_t size) {
    strncpy(dir, g_config.logPath, size);
    char* lastSlash = strrchr(dir, '\\');
    if (lastSlash != NULL) {
        *lastSlash = '\0';
    }
}

// 确保配置和日志目录存在
static bool EnsureDirectoriesExist(void) {
    char configDir[MAX_PATH];
    char logDir[MAX_PATH];

    GetConfigDir(configDir, sizeof(configDir));
    GetLogDir(logDir, sizeof(logDir));

    bool success = true;

    // 创建配置目录
    DWORD attrib = GetFileAttributesA(configDir);
    if (attrib == INVALID_FILE_ATTRIBUTES || !(attrib & FILE_ATTRIBUTE_DIRECTORY)) {
        if (!CreateDirectoryRecursive(configDir)) {
            success = false;
        }
    }

    // 创建日志目录
    attrib = GetFileAttributesA(logDir);
    if (attrib == INVALID_FILE_ATTRIBUTES || !(attrib & FILE_ATTRIBUTE_DIRECTORY)) {
        if (!CreateDirectoryRecursive(logDir)) {
            success = false;
        }
    }

    return success;
}

// 默认配置文件内容
static const char* DEFAULT_CONFIG_JSON =
    "{\n"
    "    \"version\": \"1.0\",\n"
    "    \"modifier\": {\n"
    "        \"key\": \"CAPSLOCK\",\n"
    "        \"suppress_original\": true,\n"
    "        \"control_led\": false\n"
    "    },\n"
    "    \"mappings\": [\n"
    "        {\"from\": \"H\", \"to\": \"LEFT\"},\n"
    "        {\"from\": \"J\", \"to\": \"DOWN\"},\n"
    "        {\"from\": \"K\", \"to\": \"UP\"},\n"
    "        {\"from\": \"L\", \"to\": \"RIGHT\"},\n"
    "        {\"from\": \"I\", \"to\": \"HOME\"},\n"
    "        {\"from\": \"O\", \"to\": \"END\"},\n"
    "        {\"from\": \"U\", \"to\": \"PAGEDOWN\"},\n"
    "        {\"from\": \"P\", \"to\": \"PAGEUP\"},\n"
    "        {\"from\": \"1\", \"to\": \"F1\"},\n"
    "        {\"from\": \"2\", \"to\": \"F2\"},\n"
    "        {\"from\": \"3\", \"to\": \"F3\"},\n"
    "        {\"from\": \"4\", \"to\": \"F4\"},\n"
    "        {\"from\": \"5\", \"to\": \"F5\"},\n"
    "        {\"from\": \"6\", \"to\": \"F6\"},\n"
    "        {\"from\": \"7\", \"to\": \"F7\"},\n"
    "        {\"from\": \"8\", \"to\": \"F8\"},\n"
    "        {\"from\": \"9\", \"to\": \"F9\"},\n"
    "        {\"from\": \"0\", \"to\": \"F10\"},\n"
    "        {\"from\": \"MINUS\", \"to\": \"F11\"},\n"
    "        {\"from\": \"EQUAL\", \"to\": \"F12\"}\n"
    "    ],\n"
    "    \"options\": {\n"
    "        \"start_enabled\": true,\n"
    "        \"log_level\": \"INFO\",\n"
    "        \"log_to_file\": true,\n"
    "        \"keyboard_layout\": \"auto\"\n"
    "    }\n"
    "}\n";

// 确保配置文件存在，不存在则创建默认配置
static bool EnsureConfigFileExists(void) {
    // 检查配置文件是否存在
    DWORD attrib = GetFileAttributesA(g_config.configPath);
    if (attrib != INVALID_FILE_ATTRIBUTES) {
        return true;  // 文件已存在
    }

    // 创建默认配置文件
    if (WriteFileContent(g_config.configPath, DEFAULT_CONFIG_JSON)) {
        return true;
    }

    return false;
}

// 简单的JSON字符串提取
static bool ExtractJsonString(const char* json, const char* key, char* value, int maxSize) {
    char searchKey[128];
    snprintf(searchKey, sizeof(searchKey), "\"%s\"", key);

    const char* pos = strstr(json, searchKey);
    if (pos == NULL) return false;

    pos = strchr(pos + strlen(searchKey), ':');
    if (pos == NULL) return false;

    pos = strchr(pos, '"');
    if (pos == NULL) return false;
    pos++;

    const char* end = strchr(pos, '"');
    if (end == NULL) return false;

    int len = end - pos;
    if (len >= maxSize) len = maxSize - 1;

    strncpy(value, pos, len);
    value[len] = '\0';
    return true;
}

// 简单的JSON布尔值提取
static bool ExtractJsonBool(const char* json, const char* key, bool* value) {
    char searchKey[128];
    snprintf(searchKey, sizeof(searchKey), "\"%s\"", key);

    const char* pos = strstr(json, searchKey);
    if (pos == NULL) return false;

    pos = strchr(pos + strlen(searchKey), ':');
    if (pos == NULL) return false;

    while (*pos == ' ' || *pos == ':' || *pos == '\t') pos++;

    if (strncmp(pos, "true", 4) == 0) {
        *value = true;
        return true;
    } else if (strncmp(pos, "false", 5) == 0) {
        *value = false;
        return true;
    }

    return false;
}

// 向前声明
static bool CreateDirectoryRecursive(const char* path);

void ConfigInit(void) {
    memset(&g_config, 0, sizeof(Config));

    // 设置默认路径 - 新位置: %USERPROFILE%\.PowerCapslock\config\config.json
    char userProfilePath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_PROFILE, NULL, 0, userProfilePath))) {
        // 新配置文件路径
        snprintf(g_config.configPath, MAX_PATH, "%s\\.PowerCapslock\\config\\config.json", userProfilePath);
        // 默认日志目录和模型目录
        snprintf(g_config.logDirPath, MAX_PATH, "%s\\.PowerCapslock\\logs", userProfilePath);
        strncpy(g_config.modelDirPath, "models", sizeof(g_config.modelDirPath) - 1);  // 相对路径，相对于exe
    }

    // 检查新配置文件是否存在
    bool newConfigExists = (GetFileAttributesA(g_config.configPath) != INVALID_FILE_ATTRIBUTES);

    // 如果新配置不存在，尝试从旧位置迁移
    if (!newConfigExists) {
        char oldConfigPath[MAX_PATH];
        char appDataPath[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, 0, appDataPath))) {
            snprintf(oldConfigPath, MAX_PATH, "%s\\PowerCapslock\\config.json", appDataPath);
            bool oldConfigExists = (GetFileAttributesA(oldConfigPath) != INVALID_FILE_ATTRIBUTES);

            if (oldConfigExists) {
                // 旧配置存在，创建新目录
                char* lastSlash = strrchr(g_config.configPath, '\\');
                if (lastSlash != NULL) {
                    *lastSlash = '\0';
                    CreateDirectoryRecursive(g_config.configPath);
                    *lastSlash = '\\';

                    // 复制旧配置到新位置
                    if (CopyFileA(oldConfigPath, g_config.configPath, FALSE)) {
                        LOG_INFO("Config migrated from %s to %s", oldConfigPath, g_config.configPath);
                    } else {
                        LOG_WARN("Failed to migrate config from old location to new location");
                    }
                }
            }
        }
    }

    // 生成日志文件完整路径（按日期命名）
    if (g_config.logDirPath[0] != '\0') {
        time_t now = time(NULL);
        struct tm* t = localtime(&now);
        char dateStr[32];
        strftime(dateStr, sizeof(dateStr), "%Y%m%d", t);
        snprintf(g_config.logPath, MAX_PATH, "%s\\powercapslock_%s.log", g_config.logDirPath, dateStr);
    }

    ConfigLoadDefaults();

    // 确保目录和配置文件存在
    EnsureDirectoriesExist();
    EnsureConfigFileExists();
}

void ConfigCleanup(void) {
    // 无需清理
}

// 解析 mappings 数组
static void ParseMappings(const char* json) {
    // 找到 mappings 数组的开始
    const char* mappingsStart = strstr(json, "\"mappings\"");
    if (mappingsStart == NULL) {
        LOG_WARN("No mappings array found in config");
        return;
    }

    // 找到数组开始的 [
    const char* arrayStart = strchr(mappingsStart, '[');
    if (arrayStart == NULL) {
        LOG_WARN("Invalid mappings array format");
        return;
    }

    // 清除现有映射
    KeymapClear();

    // 解析每个映射对象
    const char* pos = arrayStart;
    int mappingCount = 0;

    while ((pos = strstr(pos, "\"from\"")) != NULL) {
        // 找到 from 值
        const char* fromStart = strchr(pos + 6, '"');
        if (fromStart == NULL) break;
        fromStart++;
        const char* fromEnd = strchr(fromStart, '"');
        if (fromEnd == NULL) break;

        char fromKey[64] = {0};
        int fromLen = fromEnd - fromStart;
        if (fromLen >= 64) fromLen = 63;
        strncpy(fromKey, fromStart, fromLen);

        // 找到 to 值
        const char* toPos = strstr(fromEnd, "\"to\"");
        if (toPos == NULL) break;
        const char* toStart = strchr(toPos + 4, '"');
        if (toStart == NULL) break;
        toStart++;
        const char* toEnd = strchr(toStart, '"');
        if (toEnd == NULL) break;

        char toKey[64] = {0};
        int toLen = toEnd - toStart;
        if (toLen >= 64) toLen = 63;
        strncpy(toKey, toStart, toLen);

        // 转换为扫描码和VK码
        WORD scanCode = NameToScanCode(fromKey);
        UINT targetVk = NameToVkCode(toKey);

        if (scanCode != 0 && targetVk != 0) {
            char mappingName[128];
            snprintf(mappingName, sizeof(mappingName), "%s->%s", fromKey, toKey);
            KeymapAddMapping(scanCode, targetVk, strdup(mappingName));
            mappingCount++;
            LOG_DEBUG("Loaded mapping: %s -> %s (scanCode=0x%02X, vk=0x%02X)",
                     fromKey, toKey, scanCode, targetVk);
        } else {
            LOG_WARN("Invalid mapping: %s -> %s (scanCode=0x%02X, vk=0x%02X)",
                    fromKey, toKey, scanCode, targetVk);
        }

        pos = toEnd;
    }

    LOG_INFO("Loaded %d key mappings from config", mappingCount);
}

// 解析 actions 数组（新格式）
static void ParseActions(const char* json) {
    const char* actionsStart = strstr(json, "\"actions\"");
    if (actionsStart == NULL) {
        LOG_DEBUG("No actions array found in config");
        return;
    }

    const char* arrayStart = strchr(actionsStart, '[');
    if (arrayStart == NULL) {
        LOG_WARN("Invalid actions array format");
        return;
    }

    const char* pos = arrayStart;
    int actionCount = 0;

    while ((pos = strstr(pos, "\"trigger\"")) != NULL) {
        // 解析 trigger
        const char* triggerStart = strchr(pos + 9, '"');
        if (triggerStart == NULL) break;
        triggerStart++;
        const char* triggerEnd = strchr(triggerStart, '"');
        if (triggerEnd == NULL) break;

        char trigger[32] = {0};
        int triggerLen = triggerEnd - triggerStart;
        if (triggerLen >= 32) triggerLen = 31;
        strncpy(trigger, triggerStart, triggerLen);

        // 解析 type
        const char* typePos = strstr(triggerEnd, "\"type\"");
        if (typePos == NULL) break;
        const char* typeStart = strchr(typePos + 6, '"');
        if (typeStart == NULL) break;
        typeStart++;
        const char* typeEnd = strchr(typeStart, '"');
        if (typeEnd == NULL) break;

        char typeStr[32] = {0};
        int typeLen = typeEnd - typeStart;
        if (typeLen >= 32) typeLen = 31;
        strncpy(typeStr, typeStart, typeLen);

        ActionType type = ACTION_TYPE_KEY_MAPPING;
        if (strcmp(typeStr, "builtin") == 0) {
            type = ACTION_TYPE_BUILTIN;
        } else if (strcmp(typeStr, "command") == 0) {
            type = ACTION_TYPE_COMMAND;
        }

        // 解析 param
        const char* paramPos = strstr(typeEnd, "\"param\"");
        if (paramPos == NULL) break;
        const char* paramStart = strchr(paramPos + 7, '"');
        if (paramStart == NULL) break;
        paramStart++;
        const char* paramEnd = strchr(paramStart, '"');
        if (paramEnd == NULL) break;

        char param[256] = {0};
        int paramLen = paramEnd - paramStart;
        if (paramLen >= 256) paramLen = 255;
        strncpy(param, paramStart, paramLen);

        // 创建动作
        Action action = {0};
        strncpy(action.trigger, trigger, sizeof(action.trigger) - 1);
        action.type = type;
        strncpy(action.param, param, sizeof(action.param) - 1);
        ActionAdd(&action);
        actionCount++;

        pos = paramEnd;
    }

    LOG_INFO("Loaded %d actions from config", actionCount);
}

// 迁移旧 mappings 到 actions
void ConfigMigrateMappingsToActions(void) {
    int mappingCount;
    const KeyMapping* mappings = KeymapGetAll(&mappingCount);

    for (int i = 0; i < mappingCount; i++) {
        const char* triggerName = ScanCodeToName(mappings[i].scanCode);
        const char* paramName = VkCodeToName(mappings[i].targetVk);

        if (triggerName != NULL && paramName != NULL) {
            Action action = {0};
            strncpy(action.trigger, triggerName, sizeof(action.trigger) - 1);
            action.type = ACTION_TYPE_KEY_MAPPING;
            strncpy(action.param, paramName, sizeof(action.param) - 1);
            ActionAdd(&action);
        }
    }

    LOG_INFO("Migrated %d mappings to actions", mappingCount);
}

bool ConfigLoad(const char* path) {
    const char* configPath = (path != NULL) ? path : g_config.configPath;

    char* content = ReadFileContent(configPath);
    if (content == NULL) {
        LOG_WARN("Failed to read config file: %s, using defaults", configPath);
        ConfigLoadDefaults();
        return false;
    }

    // 解析配置
    char temp[256];

    // modifier.key
    if (ExtractJsonString(content, "key", temp, sizeof(temp))) {
        strncpy(g_config.modifierKey, temp, sizeof(g_config.modifierKey) - 1);
    }

    // modifier.suppress_original
    ExtractJsonBool(content, "suppress_original", &g_config.suppressOriginal);

    // modifier.control_led
    ExtractJsonBool(content, "control_led", &g_config.controlLed);

    // options.start_enabled
    ExtractJsonBool(content, "start_enabled", &g_config.startEnabled);

    // options.log_level
    if (ExtractJsonString(content, "log_level", temp, sizeof(temp))) {
        if (strcmp(temp, "DEBUG") == 0) g_config.logLevel = LOG_LEVEL_DEBUG;
        else if (strcmp(temp, "INFO") == 0) g_config.logLevel = LOG_LEVEL_INFO;
        else if (strcmp(temp, "WARN") == 0) g_config.logLevel = LOG_LEVEL_WARN;
        else if (strcmp(temp, "ERROR") == 0) g_config.logLevel = LOG_LEVEL_ERROR;
    }

    // options.log_to_file
    ExtractJsonBool(content, "log_to_file", &g_config.logToFile);

    // options.keyboard_layout
    if (ExtractJsonString(content, "keyboard_layout", temp, sizeof(temp))) {
        strncpy(g_config.keyboardLayout, temp, sizeof(g_config.keyboardLayout) - 1);
    }

    // voice_input.enabled
    ExtractJsonBool(content, "enabled", &g_config.voiceInputEnabled);

    // voice_input.asked
    ExtractJsonBool(content, "asked", &g_config.voiceInputAsked);

    // paths.log_directory
    if (ExtractJsonString(content, "log_directory", temp, sizeof(temp))) {
        strncpy(g_config.logDirPath, temp, sizeof(g_config.logDirPath) - 1);
        // 重新生成日志文件完整路径（按当前日期）
        if (g_config.logDirPath[0] != '\0') {
            time_t now = time(NULL);
            struct tm* t = localtime(&now);
            char dateStr[32];
            strftime(dateStr, sizeof(dateStr), "%Y%m%d", t);
            snprintf(g_config.logPath, MAX_PATH, "%s\\powercapslock_%s.log", g_config.logDirPath, dateStr);
        }
    }

    // paths.model_directory
    if (ExtractJsonString(content, "model_directory", temp, sizeof(temp))) {
        strncpy(g_config.modelDirPath, temp, sizeof(g_config.modelDirPath) - 1);
    }

    // 优先尝试加载 actions 数组
    ParseActions(content);

    // 如果没有 actions，加载 mappings 并迁移
    if (ActionGetCount() == 0) {
        ParseMappings(content);
        ConfigMigrateMappingsToActions();
    }

    free(content);
    LOG_INFO("Config loaded from: %s", configPath);
    return true;
}

// Helper: Append a string to JSON buffer with proper escaping for backslashes
static int JsonAppendEscaped(char* buffer, int bufferSize, int offset, const char* str) {
    while (*str != '\0' && offset < bufferSize - 2) {
        if (*str == '\\') {
            buffer[offset++] = '\\';
            buffer[offset++] = '\\';
        } else {
            buffer[offset++] = *str;
        }
        str++;
    }
    return offset;
}

bool ConfigSave(const char* path) {
    const char* configPath = (path != NULL) ? path : g_config.configPath;

    // 构建JSON内容
    char content[4096];
    const char* levelStr = "INFO";
    switch (g_config.logLevel) {
        case LOG_LEVEL_DEBUG: levelStr = "DEBUG"; break;
        case LOG_LEVEL_INFO: levelStr = "INFO"; break;
        case LOG_LEVEL_WARN: levelStr = "WARN"; break;
        case LOG_LEVEL_ERROR: levelStr = "ERROR"; break;
    }

    // Write header
    int offset = 0;
    offset += snprintf(content + offset, sizeof(content) - offset,
        "{\n"
        "    \"version\": \"1.0\",\n"
        "    \"modifier\": {\n"
        "        \"key\": \"%s\",\n"
        "        \"suppress_original\": %s,\n"
        "        \"control_led\": %s\n"
        "    },\n",
        g_config.modifierKey,
        g_config.suppressOriginal ? "true" : "false",
        g_config.controlLed ? "true" : "false"
    );

    // 写入 actions 数组
    offset += snprintf(content + offset, sizeof(content) - offset,
        "    \"actions\": [\n");

    for (int i = 0; i < ActionGetCount(); i++) {
        const Action* action = ActionGet(i);
        const char* typeStr = "key_mapping";
        if (action->type == ACTION_TYPE_BUILTIN) {
            typeStr = "builtin";
        } else if (action->type == ACTION_TYPE_COMMAND) {
            typeStr = "command";
        }

        if (i == 0) {
            offset += snprintf(content + offset, sizeof(content) - offset,
                "        {\"trigger\": \"%s\", \"type\": \"%s\", \"param\": \"%s\"}",
                action->trigger, typeStr, action->param);
        } else {
            offset += snprintf(content + offset, sizeof(content) - offset,
                ",\n        {\"trigger\": \"%s\", \"type\": \"%s\", \"param\": \"%s\"}",
                action->trigger, typeStr, action->param);
        }
    }

    // Write closing bracket for actions and continue with rest
    offset += snprintf(content + offset, sizeof(content) - offset,
        "\n"
        "    ],\n"
        "    \"options\": {\n"
        "        \"start_enabled\": %s,\n"
        "        \"log_level\": \"%s\",\n"
        "        \"log_to_file\": %s,\n"
        "        \"keyboard_layout\": \"%s\"\n"
        "    },\n"
        "    \"paths\": {\n"
        "        \"log_directory\": \"",
        g_config.startEnabled ? "true" : "false",
        levelStr,
        g_config.logToFile ? "true" : "false",
        g_config.keyboardLayout
    );

    // Escape backslashes in log directory path for valid JSON
    offset = JsonAppendEscaped(content, sizeof(content), offset, g_config.logDirPath);

    // Add separator for model directory
    offset += snprintf(content + offset, sizeof(content) - offset,
        "\",\n"
        "        \"model_directory\": \"");

    // Escape backslashes in model directory path for valid JSON
    offset = JsonAppendEscaped(content, sizeof(content), offset, g_config.modelDirPath);

    // Add the closing sections
    offset += snprintf(content + offset, sizeof(content) - offset,
        "\"\n"
        "    },\n"
        "    \"voice_input\": {\n"
        "        \"enabled\": %s,\n"
        "        \"asked\": %s\n"
        "    }\n"
        "}\n",
        g_config.voiceInputEnabled ? "true" : "false",
        g_config.voiceInputAsked ? "true" : "false"
    );

    if (offset >= sizeof(content)) {
        LOG_ERROR("Config buffer overflow: too many mappings");
        return false;
    }

    if (WriteFileContent(configPath, content)) {
        LOG_INFO("Config saved to: %s (%d actions)", configPath, ActionGetCount());
        return true;
    } else {
        LOG_ERROR("Failed to save config to: %s", configPath);
        return false;
    }
}

const Config* ConfigGet(void) {
    return &g_config;
}

void ConfigSet(const Config* config) {
    if (config != NULL) {
        memcpy(&g_config, config, sizeof(Config));
    }
}

void ConfigLoadDefaults(void) {
    strncpy(g_config.modifierKey, "CAPSLOCK", sizeof(g_config.modifierKey) - 1);
    g_config.suppressOriginal = true;
    g_config.controlLed = false;
    g_config.startEnabled = true;
    g_config.logLevel = LOG_LEVEL_INFO;
    g_config.logToFile = true;
    strncpy(g_config.keyboardLayout, "auto", sizeof(g_config.keyboardLayout) - 1);
    g_config.voiceInputEnabled = false;
    g_config.voiceInputAsked = false;
}

const char* ConfigGetPath(void) {
    return g_config.configPath;
}

const char* ConfigGetLogPath(void) {
    return g_config.logPath;
}

WORD ConfigKeyNameToScanCode(const char* name) {
    if (name == NULL) {
        return 0;
    }
    return NameToScanCode(name);
}

UINT ConfigKeyNameToVkCode(const char* name) {
    if (name == NULL) {
        return 0;
    }
    return NameToVkCode(name);
}
