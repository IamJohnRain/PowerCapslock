#include "config.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlobj.h>

static Config g_config = {0};

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

void ConfigInit(void) {
    memset(&g_config, 0, sizeof(Config));

    // 设置默认路径
    char appDataPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, 0, appDataPath))) {
        snprintf(g_config.configPath, MAX_PATH, "%s\\JohnHotKeyMap\\keymap.json", appDataPath);
        snprintf(g_config.logPath, MAX_PATH, "%s\\JohnHotKeyMap\\logs\\hotkeymap.log", appDataPath);
    }

    ConfigLoadDefaults();
}

void ConfigCleanup(void) {
    // 无需清理
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

    free(content);
    LOG_INFO("Config loaded from: %s", configPath);
    return true;
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

    snprintf(content, sizeof(content),
        "{\n"
        "    \"version\": \"1.0\",\n"
        "    \"modifier\": {\n"
        "        \"key\": \"%s\",\n"
        "        \"suppress_original\": %s,\n"
        "        \"control_led\": %s\n"
        "    },\n"
        "    \"mappings\": [\n"
        "        {\"from\": \"H\", \"to\": \"LEFT\"},\n"
        "        {\"from\": \"J\", \"to\": \"DOWN\"},\n"
        "        {\"from\": \"K\", \"to\": \"UP\"},\n"
        "        {\"from\": \"L\", \"to\": \"RIGHT\"},\n"
        "        {\"from\": \"I\", \"to\": \"END\"},\n"
        "        {\"from\": \"O\", \"to\": \"HOME\"},\n"
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
        "        \"start_enabled\": %s,\n"
        "        \"log_level\": \"%s\",\n"
        "        \"log_to_file\": %s,\n"
        "        \"keyboard_layout\": \"%s\"\n"
        "    }\n"
        "}\n",
        g_config.modifierKey,
        g_config.suppressOriginal ? "true" : "false",
        g_config.controlLed ? "true" : "false",
        g_config.startEnabled ? "true" : "false",
        levelStr,
        g_config.logToFile ? "true" : "false",
        g_config.keyboardLayout
    );

    if (WriteFileContent(configPath, content)) {
        LOG_INFO("Config saved to: %s", configPath);
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
}

const char* ConfigGetPath(void) {
    return g_config.configPath;
}

const char* ConfigGetLogPath(void) {
    return g_config.logPath;
}
