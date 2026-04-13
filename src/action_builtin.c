#include "action_builtin.h"
#include "logger.h"
#include "hook.h"
#include "voice.h"
#include "audio.h"
#include "voice_prompt.h"
#include "screenshot_manager.h"
#include <stdlib.h>
#include <string.h>

#define MAX_BUILTINS 16

typedef struct {
    char name[32];
    char displayName[64];
    BuiltinHandler handler;
} BuiltinEntry;

static BuiltinEntry g_builtins[MAX_BUILTINS];
static int g_builtinCount = 0;

// 内置功能处理器实现
static bool HandleVoiceInput(void) {
    LOG_INFO("[Builtin] Voice input triggered");
    // 语音输入由 hook.c 处理，这里返回 true 表示已识别
    return true;
}

static bool HandleScreenshot(void) {
    LOG_INFO("[Builtin] Screenshot triggered");
    return ScreenshotManagerStart();
}

static bool HandleToggleEnabled(void) {
    bool enabled = HookIsEnabled();
    HookSetEnabled(!enabled);
    LOG_INFO("[Builtin] Toggle enabled: %s", !enabled ? "true" : "false");
    return true;
}

void BuiltinInit(void) {
    g_builtinCount = 0;

    // 注册默认内置功能
    BuiltinRegister("voice_input", HandleVoiceInput);
    BuiltinRegister("screenshot", HandleScreenshot);
    BuiltinRegister("toggle_enabled", HandleToggleEnabled);

    // 初始化截图管理器
    ScreenshotManagerInit();

    LOG_INFO("Builtin module initialized with %d handlers", g_builtinCount);
}

void BuiltinCleanup(void) {
    ScreenshotManagerCleanup();
    g_builtinCount = 0;
}

void BuiltinRegister(const char* name, BuiltinHandler handler) {
    if (g_builtinCount >= MAX_BUILTINS) {
        LOG_ERROR("Builtin registry full, cannot register: %s", name);
        return;
    }

    if (name == NULL || handler == NULL) {
        LOG_ERROR("Invalid builtin registration parameters");
        return;
    }

    // 检查是否已注册
    for (int i = 0; i < g_builtinCount; i++) {
        if (strcmp(g_builtins[i].name, name) == 0) {
            g_builtins[i].handler = handler;
            LOG_DEBUG("Updated builtin handler: %s", name);
            return;
        }
    }

    // 添加新注册
    strncpy(g_builtins[g_builtinCount].name, name, sizeof(g_builtins[0].name) - 1);
    g_builtins[g_builtinCount].handler = handler;

    // 设置显示名称
    if (strcmp(name, "voice_input") == 0) {
        strncpy(g_builtins[g_builtinCount].displayName, "语音输入",
                sizeof(g_builtins[0].displayName) - 1);
    } else if (strcmp(name, "screenshot") == 0) {
        strncpy(g_builtins[g_builtinCount].displayName, "截图",
                sizeof(g_builtins[0].displayName) - 1);
    } else if (strcmp(name, "toggle_enabled") == 0) {
        strncpy(g_builtins[g_builtinCount].displayName, "启用/禁用切换",
                sizeof(g_builtins[0].displayName) - 1);
    } else {
        strncpy(g_builtins[g_builtinCount].displayName, name,
                sizeof(g_builtins[0].displayName) - 1);
    }

    g_builtinCount++;
    LOG_DEBUG("Registered builtin: %s", name);
}

bool BuiltinExecute(const char* name) {
    if (name == NULL) {
        return false;
    }

    for (int i = 0; i < g_builtinCount; i++) {
        if (strcmp(g_builtins[i].name, name) == 0) {
            LOG_INFO("Executing builtin: %s", name);
            return g_builtins[i].handler();
        }
    }

    LOG_WARN("Builtin not found: %s", name);
    return false;
}

const char** BuiltinGetList(int* count) {
    static const char* names[MAX_BUILTINS];

    if (count != NULL) {
        *count = g_builtinCount;
    }

    for (int i = 0; i < g_builtinCount; i++) {
        names[i] = g_builtins[i].name;
    }

    return names;
}

const char* BuiltinGetDisplayName(const char* name) {
    if (name == NULL) {
        return NULL;
    }

    for (int i = 0; i < g_builtinCount; i++) {
        if (strcmp(g_builtins[i].name, name) == 0) {
            return g_builtins[i].displayName;
        }
    }

    return name;
}
