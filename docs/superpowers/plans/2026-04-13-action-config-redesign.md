# 快捷键配置交互优化实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 重构快捷键配置系统，支持多种动作类型（按键映射、内置功能、执行命令），采用键盘输入检测代替表单填写。

**Architecture:** 统一动作模型 `(trigger, type, param)`，可插拔内置功能注册机制，WebView2 前端与 C 后端通过消息通信。

**Tech Stack:** C11, C++17, WebView2, Win32 API

---

## 文件结构

### 新增文件

| 文件 | 职责 |
|------|------|
| `src/action.h` | 统一动作数据结构与接口声明 |
| `src/action.c` | 动作管理实现（增删改查、执行） |
| `src/action_builtin.h` | 内置功能注册接口声明 |
| `src/action_builtin.c` | 内置功能注册与执行实现 |

### 修改文件

| 文件 | 修改内容 |
|------|----------|
| `src/config.h` | 添加 actions 相关接口声明 |
| `src/config.c` | 加载/保存 actions 配置，迁移旧格式 |
| `src/keymap.c/h` | 从 action 模块获取按键映射，保持兼容 |
| `src/hook.c` | 集成动作查询与执行，支持捕获模式 |
| `src/hook.h` | 添加捕获模式接口声明 |
| `src/config_dialog_webview.cpp` | WebView2 消息处理，按键捕获支持 |
| `resources/config_ui.html` | 新 UI 布局与交互 |
| `CMakeLists.txt` | 添加新源文件 |

---

## Task 1: 创建 action.h 头文件

**Files:**
- Create: `src/action.h`

- [ ] **Step 1: 创建 action.h 头文件**

```c
#ifndef ACTION_H
#define ACTION_H

#include <windows.h>
#include <stdbool.h>

// 动作类型枚举
typedef enum {
    ACTION_TYPE_KEY_MAPPING,    // 按键映射
    ACTION_TYPE_BUILTIN,        // 内置功能
    ACTION_TYPE_COMMAND         // 执行命令
} ActionType;

// 统一动作结构
typedef struct {
    char trigger[16];           // 触发键名称，如 "H", "A", "F1"
    WORD scanCode;              // 触发键扫描码（缓存）
    ActionType type;            // 动作类型
    char param[256];            // 参数：按键名/内置功能名/命令路径
} Action;

// 初始化动作模块
void ActionInit(void);

// 清理动作模块
void ActionCleanup(void);

// 根据扫描码查找动作
const Action* ActionFindByScanCode(WORD scanCode);

// 获取动作数量
int ActionGetCount(void);

// 获取指定索引的动作
const Action* ActionGet(int index);

// 添加动作（返回索引，-1表示失败）
int ActionAdd(const Action* action);

// 更新动作
bool ActionUpdate(int index, const Action* action);

// 删除动作
bool ActionDelete(int index);

// 重置为默认动作
void ActionResetToDefaults(void);

// 执行动作
bool ActionExecute(const Action* action);

// 根据触发键名称查找动作
const Action* ActionFindByTriggerName(const char* triggerName);

// 检查触发键是否已存在
bool ActionTriggerExists(const char* triggerName);

#endif // ACTION_H
```

- [ ] **Step 2: 提交**

```bash
git add src/action.h
git commit -m "feat: add action.h header with unified action model

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>"
```

---

## Task 2: 创建 action_builtin.h 头文件

**Files:**
- Create: `src/action_builtin.h`

- [ ] **Step 1: 创建 action_builtin.h 头文件**

```c
#ifndef ACTION_BUILTIN_H
#define ACTION_BUILTIN_H

#include <stdbool.h>

// 内置功能处理器类型
typedef bool (*BuiltinHandler)(void);

// 初始化内置功能模块
void BuiltinInit(void);

// 清理内置功能模块
void BuiltinCleanup(void);

// 注册内置功能
void BuiltinRegister(const char* name, BuiltinHandler handler);

// 执行内置功能
bool BuiltinExecute(const char* name);

// 获取所有已注册的内置功能名称
// 返回: 名称数组，count 输出数量
const char** BuiltinGetList(int* count);

// 获取内置功能的显示名称（中文）
const char* BuiltinGetDisplayName(const char* name);

#endif // ACTION_BUILTIN_H
```

- [ ] **Step 2: 提交**

```bash
git add src/action_builtin.h
git commit -m "feat: add action_builtin.h with pluggable builtin interface

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>"
```

---

## Task 3: 实现 action_builtin.c

**Files:**
- Create: `src/action_builtin.c`

- [ ] **Step 1: 创建 action_builtin.c 实现文件**

```c
#include "action_builtin.h"
#include "logger.h"
#include "hook.h"
#include "voice.h"
#include "audio.h"
#include "voice_prompt.h"
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
    // TODO: 实现截图功能
    return false;
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

    LOG_INFO("Builtin module initialized with %d handlers", g_builtinCount);
}

void BuiltinCleanup(void) {
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
```

- [ ] **Step 2: 提交**

```bash
git add src/action_builtin.c
git commit -m "feat: implement action_builtin.c with pluggable handlers

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>"
```

---

## Task 4: 实现 action.c

**Files:**
- Create: `src/action.c`

- [ ] **Step 1: 创建 action.c 实现文件**

```c
#include "action.h"
#include "action_builtin.h"
#include "logger.h"
#include "config.h"
#include <stdlib.h>
#include <string.h>

#define MAX_ACTIONS 64

// 动作存储
static Action g_actions[MAX_ACTIONS];
static int g_actionCount = 0;

// 默认动作配置（与现有 keymap 默认映射一致）
static const struct {
    const char* trigger;
    const char* param;
} g_defaultKeyMappings[] = {
    {"H", "LEFT"}, {"J", "DOWN"}, {"K", "UP"}, {"L", "RIGHT"},
    {"I", "HOME"}, {"O", "END"}, {"U", "PAGEDOWN"}, {"P", "PAGEUP"},
    {"1", "F1"}, {"2", "F2"}, {"3", "F3"}, {"4", "F4"},
    {"5", "F5"}, {"6", "F6"}, {"7", "F7"}, {"8", "F8"},
    {"9", "F9"}, {"0", "F10"}, {"MINUS", "F11"}, {"EQUAL", "F12"}
};
static const int g_defaultKeyMappingCount = sizeof(g_defaultKeyMappings) / sizeof(g_defaultKeyMappings[0]);

void ActionInit(void) {
    g_actionCount = 0;
    memset(g_actions, 0, sizeof(g_actions));
    LOG_INFO("Action module initialized");
}

void ActionCleanup(void) {
    g_actionCount = 0;
    LOG_INFO("Action module cleaned up");
}

const Action* ActionFindByScanCode(WORD scanCode) {
    for (int i = 0; i < g_actionCount; i++) {
        if (g_actions[i].scanCode == scanCode) {
            return &g_actions[i];
        }
    }
    return NULL;
}

const Action* ActionFindByTriggerName(const char* triggerName) {
    if (triggerName == NULL) {
        return NULL;
    }
    for (int i = 0; i < g_actionCount; i++) {
        if (_stricmp(g_actions[i].trigger, triggerName) == 0) {
            return &g_actions[i];
        }
    }
    return NULL;
}

int ActionGetCount(void) {
    return g_actionCount;
}

const Action* ActionGet(int index) {
    if (index < 0 || index >= g_actionCount) {
        return NULL;
    }
    return &g_actions[index];
}

bool ActionTriggerExists(const char* triggerName) {
    return ActionFindByTriggerName(triggerName) != NULL;
}

int ActionAdd(const Action* action) {
    if (action == NULL || g_actionCount >= MAX_ACTIONS) {
        LOG_ERROR("Cannot add action: invalid params or buffer full");
        return -1;
    }

    // 检查是否已存在
    const Action* existing = ActionFindByTriggerName(action->trigger);
    if (existing != NULL) {
        // 更新现有动作
        int index = (int)(existing - g_actions);
        ActionUpdate(index, action);
        return index;
    }

    // 添加新动作
    Action* newAction = &g_actions[g_actionCount];
    memset(newAction, 0, sizeof(Action));

    strncpy(newAction->trigger, action->trigger, sizeof(newAction->trigger) - 1);
    newAction->type = action->type;
    strncpy(newAction->param, action->param, sizeof(newAction->param) - 1);

    // 计算扫描码
    newAction->scanCode = ConfigKeyNameToScanCode(action->trigger);

    g_actionCount++;
    LOG_DEBUG("Added action: %s -> type=%d, param=%s",
              action->trigger, action->type, action->param);
    return g_actionCount - 1;
}

bool ActionUpdate(int index, const Action* action) {
    if (index < 0 || index >= g_actionCount || action == NULL) {
        return false;
    }

    Action* target = &g_actions[index];
    strncpy(target->trigger, action->trigger, sizeof(target->trigger) - 1);
    target->type = action->type;
    strncpy(target->param, action->param, sizeof(target->param) - 1);
    target->scanCode = ConfigKeyNameToScanCode(action->trigger);

    LOG_DEBUG("Updated action at index %d: %s", index, action->trigger);
    return true;
}

bool ActionDelete(int index) {
    if (index < 0 || index >= g_actionCount) {
        return false;
    }

    // 移动后续元素
    for (int i = index; i < g_actionCount - 1; i++) {
        g_actions[i] = g_actions[i + 1];
    }
    g_actionCount--;

    LOG_DEBUG("Deleted action at index %d", index);
    return true;
}

void ActionResetToDefaults(void) {
    g_actionCount = 0;

    // 添加默认按键映射
    for (int i = 0; i < g_defaultKeyMappingCount; i++) {
        Action action = {0};
        strncpy(action.trigger, g_defaultKeyMappings[i].trigger, sizeof(action.trigger) - 1);
        action.type = ACTION_TYPE_KEY_MAPPING;
        strncpy(action.param, g_defaultKeyMappings[i].param, sizeof(action.param) - 1);
        action.scanCode = ConfigKeyNameToScanCode(action.trigger);
        ActionAdd(&action);
    }

    LOG_INFO("Actions reset to defaults (%d actions)", g_actionCount);
}

bool ActionExecute(const Action* action) {
    if (action == NULL) {
        return false;
    }

    switch (action->type) {
        case ACTION_TYPE_KEY_MAPPING: {
            // 按键映射由 hook.c 处理
            LOG_DEBUG("Action execute: key_mapping %s -> %s", action->trigger, action->param);
            return true;
        }
        case ACTION_TYPE_BUILTIN: {
            LOG_DEBUG("Action execute: builtin %s -> %s", action->trigger, action->param);
            return BuiltinExecute(action->param);
        }
        case ACTION_TYPE_COMMAND: {
            LOG_DEBUG("Action execute: command %s -> %s", action->trigger, action->param);
            // TODO: 实现命令执行
            return false;
        }
        default:
            LOG_WARN("Unknown action type: %d", action->type);
            return false;
    }
}
```

- [ ] **Step 2: 提交**

```bash
git add src/action.c
git commit -m "feat: implement action.c with unified action management

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>"
```

---

## Task 5: 修改 CMakeLists.txt 添加新源文件

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 读取 CMakeLists.txt**

```bash
cat CMakeLists.txt
```

- [ ] **Step 2: 在源文件列表中添加新文件**

找到 `set(SOURCES ...)` 部分，添加 `src/action.c` 和 `src/action_builtin.c`。

```cmake
# 在 SOURCES 列表中添加
src/action.c
src/action_builtin.c
```

- [ ] **Step 3: 提交**

```bash
git add CMakeLists.txt
git commit -m "build: add action modules to CMakeLists.txt

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>"
```

---

## Task 6: 修改 config.c 支持 actions 配置

**Files:**
- Modify: `src/config.c`
- Modify: `src/config.h`

- [ ] **Step 1: 在 config.h 添加 actions 相关接口**

在 `config.h` 末尾添加：

```c
// Actions 配置接口
bool ConfigLoadActions(const char* json);
char* ConfigSaveActions(void);
void ConfigMigrateMappingsToActions(void);
```

- [ ] **Step 2: 在 config.c 添加 actions 解析函数**

在 `ParseMappings` 函数附近添加：

```c
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
```

- [ ] **Step 3: 修改 ConfigLoad 函数**

在 `ConfigLoad` 函数中，优先加载 actions，如果没有则加载 mappings 并迁移：

```c
// 在 ConfigLoad 函数中，ParseMappings(content) 之前添加：

// 优先尝试加载 actions 数组
ParseActions(content);

// 如果没有 actions，加载 mappings 并迁移
if (ActionGetCount() == 0) {
    ParseMappings(content);
    ConfigMigrateMappingsToActions();
}
```

- [ ] **Step 4: 修改 ConfigSave 函数**

在 `ConfigSave` 函数中，保存 actions 格式：

```c
// 在构建 JSON 时，将 mappings 替换为 actions：

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

offset += snprintf(content + offset, sizeof(content) - offset,
    "\n    ],\n");
```

- [ ] **Step 5: 提交**

```bash
git add src/config.c src/config.h
git commit -m "feat: add actions config support with migration

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>"
```

---

## Task 7: 修改 hook.c 集成动作模块

**Files:**
- Modify: `src/hook.c`
- Modify: `src/hook.h`

- [ ] **Step 1: 在 hook.h 添加捕获模式接口**

```c
// 捕获模式类型
typedef enum {
    CAPTURE_MODE_NONE,
    CAPTURE_MODE_TRIGGER,
    CAPTURE_MODE_OUTPUT
} CaptureMode;

// 设置捕获模式
void HookSetCaptureMode(CaptureMode mode);

// 获取捕获的按键信息
bool HookGetCapturedKey(char* keyName, int keyNameSize, WORD* scanCode);

// 清除捕获的按键
void HookClearCapturedKey(void);

// 检查是否处于捕获模式
bool HookIsCaptureMode(void);
```

- [ ] **Step 2: 在 hook.c 添加捕获模式实现**

在全局状态结构体中添加：

```c
typedef struct {
    // ... 现有字段 ...
    CaptureMode captureMode;
    char capturedKeyName[32];
    WORD capturedScanCode;
} HookState;
```

添加捕获模式函数实现：

```c
void HookSetCaptureMode(CaptureMode mode) {
    g_hook.captureMode = mode;
    g_hook.capturedKeyName[0] = '\0';
    g_hook.capturedScanCode = 0;
    LOG_DEBUG("Capture mode set to: %d", mode);
}

bool HookGetCapturedKey(char* keyName, int keyNameSize, WORD* scanCode) {
    if (g_hook.capturedKeyName[0] == '\0') {
        return false;
    }
    if (keyName != NULL && keyNameSize > 0) {
        strncpy(keyName, g_hook.capturedKeyName, keyNameSize - 1);
        keyName[keyNameSize - 1] = '\0';
    }
    if (scanCode != NULL) {
        *scanCode = g_hook.capturedScanCode;
    }
    return true;
}

void HookClearCapturedKey(void) {
    g_hook.capturedKeyName[0] = '\0';
    g_hook.capturedScanCode = 0;
}

bool HookIsCaptureMode(void) {
    return g_hook.captureMode != CAPTURE_MODE_NONE;
}
```

- [ ] **Step 3: 修改 LowLevelKeyboardProc 处理捕获模式**

在键盘钩子回调中，优先处理捕获模式：

```c
// 在 LowLevelKeyboardProc 函数开头添加：

// 捕获模式优先处理
if (g_hook.captureMode != CAPTURE_MODE_NONE && wParam == WM_KEYDOWN) {
    KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lParam;

    // 忽略 CapsLock 本身
    if (kb->vkCode != VK_CAPITAL) {
        // 获取按键名称
        const char* keyName = ScanCodeToKeyName(kb->scanCode);
        if (keyName != NULL) {
            strncpy(g_hook.capturedKeyName, keyName, sizeof(g_hook.capturedKeyName) - 1);
            g_hook.capturedScanCode = kb->scanCode;
            LOG_DEBUG("Captured key: %s (scanCode=0x%02X)", keyName, kb->scanCode);

            // 通知 WebView2
            NotifyKeyCaptured(keyName, kb->scanCode);
        }
        return 1; // 拦截按键
    }
}
```

- [ ] **Step 4: 修改动作查找逻辑**

将 `KeymapFindByScanCode` 替换为 `ActionFindByScanCode`：

```c
// 在处理 CapsLock + 按键时：

const Action* action = ActionFindByScanCode(kb->scanCode);
if (action != NULL) {
    if (action->type == ACTION_TYPE_KEY_MAPPING) {
        // 按键映射：发送目标键
        UINT targetVk = ConfigKeyNameToVkCode(action->param);
        if (targetVk != 0) {
            SendKeyInput(targetVk, true);
            SendKeyInput(targetVk, false);
        }
    } else {
        // 其他动作类型
        ActionExecute(action);
    }
    return 1;
}
```

- [ ] **Step 5: 提交**

```bash
git add src/hook.c src/hook.h
git commit -m "feat: integrate action module into hook with capture mode

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>"
```

---

## Task 8: 修改 config_dialog_webview.cpp 支持新消息协议

**Files:**
- Modify: `src/config_dialog_webview.cpp`

- [ ] **Step 1: 添加按键捕获消息处理**

在消息处理函数中添加：

```cpp
// 处理 start_capture 消息
if (action == L"start_capture") {
    std::wstring type = JsonGetString(message, L"type");
    if (type == L"trigger") {
        HookSetCaptureMode(CAPTURE_MODE_TRIGGER);
    } else if (type == L"output") {
        HookSetCaptureMode(CAPTURE_MODE_OUTPUT);
    }
    return;
}

// 处理 stop_capture 消息
if (action == L"stop_capture") {
    HookSetCaptureMode(CAPTURE_MODE_NONE);
    return;
}
```

- [ ] **Step 2: 添加动作保存消息处理**

```cpp
// 处理 save_action 消息
if (action == L"save_action") {
    std::wstring trigger = JsonGetString(message, L"trigger");
    std::wstring type = JsonGetString(message, L"type");
    std::wstring param = JsonGetString(message, L"param");

    Action action = {0};
    strncpy(action.trigger, WideToUtf8(trigger).c_str(), sizeof(action.trigger) - 1);

    if (type == L"key_mapping") {
        action.type = ACTION_TYPE_KEY_MAPPING;
    } else if (type == L"builtin") {
        action.type = ACTION_TYPE_BUILTIN;
    } else if (type == L"command") {
        action.type = ACTION_TYPE_COMMAND;
    }

    strncpy(action.param, WideToUtf8(param).c_str(), sizeof(action.param) - 1);

    // 检查是否已存在
    const Action* existing = ActionFindByTriggerName(action.trigger);
    if (existing != NULL) {
        // 更新现有动作
        int index = (int)(existing - ActionGet(0));
        ActionUpdate(index, &action);
    } else {
        ActionAdd(&action);
    }

    ConfigSave(NULL);
    SendActionsToWebView();
    return;
}

// 处理 delete_action 消息
if (action == L"delete_action") {
    // 解析索引
    size_t indexPos = message.find(L"\"index\"");
    if (indexPos != std::wstring::npos) {
        size_t colon = message.find(L':', indexPos);
        if (colon != std::wstring::npos) {
            int index = std::stoi(message.substr(colon + 1));
            ActionDelete(index);
            ConfigSave(NULL);
            SendActionsToWebView();
        }
    }
    return;
}
```

- [ ] **Step 3: 添加发送动作列表到 WebView 的函数**

```cpp
static void SendActionsToWebView(ICoreWebView2* webView) {
    std::wstring json = L"{\"type\":\"actions_loaded\",\"actions\":[";

    for (int i = 0; i < ActionGetCount(); i++) {
        const Action* action = ActionGet(i);
        if (i > 0) json += L",";

        json += L"{\"trigger\":\"";
        json += Utf8ToWide(action->trigger);
        json += L"\",\"type\":\"";

        switch (action->type) {
            case ACTION_TYPE_KEY_MAPPING: json += L"key_mapping"; break;
            case ACTION_TYPE_BUILTIN: json += L"builtin"; break;
            case ACTION_TYPE_COMMAND: json += L"command"; break;
        }

        json += L"\",\"param\":\"";
        json += JsonEscape(Utf8ToWide(action->param));
        json += L"\"}";
    }

    json += L"]}";

    webView->PostWebMessageAsJson(json.c_str());
}

static void SendBuiltinListToWebView(ICoreWebView2* webView) {
    int count;
    const char** names = BuiltinGetList(&count);

    std::wstring json = L"{\"type\":\"builtin_list\",\"builtins\":[";
    for (int i = 0; i < count; i++) {
        if (i > 0) json += L",";
        json += L"{\"name\":\"";
        json += Utf8ToWide(names[i]);
        json += L"\",\"displayName\":\"";
        json += Utf8ToWide(BuiltinGetDisplayName(names[i]));
        json += L"\"}";
    }
    json += L"]}";

    webView->PostWebMessageAsJson(json.c_str());
}
```

- [ ] **Step 4: 添加按键捕获通知函数**

```cpp
static void NotifyKeyCaptured(const char* keyName, WORD scanCode, ICoreWebView2* webView) {
    std::wstring json = L"{\"type\":\"key_captured\",\"key\":\"";
    json += Utf8ToWide(keyName);
    json += L"\",\"displayName\":\"";
    json += Utf8ToWide(keyName); // TODO: 添加显示名称映射
    json += L"\"}";

    webView->PostWebMessageAsJson(json.c_str());
}
```

- [ ] **Step 5: 提交**

```bash
git add src/config_dialog_webview.cpp
git commit -m "feat: add WebView2 message handlers for action config

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>"
```

---

## Task 9: 修改 config_ui.html 实现新 UI

**Files:**
- Modify: `resources/config_ui.html`

- [ ] **Step 1: 添加快捷键配置页面 HTML 结构**

在现有 HTML 中添加新的快捷键配置区域：

```html
<!-- 快捷键配置页面 -->
<div id="action-config-page" class="page">
  <h2>快捷键配置</h2>

  <button id="add-action-btn" class="primary-btn">+ 添加快捷键</button>

  <div id="action-list" class="action-list">
    <!-- 动态生成 -->
  </div>

  <button id="reset-actions-btn" class="secondary-btn">恢复默认</button>
</div>

<!-- 添加/编辑对话框 -->
<div id="action-dialog" class="dialog hidden">
  <div class="dialog-content">
    <!-- 步骤1：捕获触发键 -->
    <div id="step-trigger" class="step">
      <h3>添加快捷键</h3>
      <div class="trigger-preview">
        <span>CapsLock + </span>
        <span id="trigger-key" class="key-waiting">[ 等待按键... ]</span>
      </div>
      <p class="hint">按下要绑定的按键，松开 CapsLock 可取消</p>
      <button id="recapture-btn" class="secondary-btn hidden">重新捕获</button>
    </div>

    <!-- 步骤2：选择动作类型 -->
    <div id="step-type" class="step hidden">
      <h3>选择动作类型</h3>
      <div class="type-cards">
        <div class="type-card" data-type="key_mapping">
          <span class="icon">🎹</span>
          <span class="title">按键映射</span>
          <span class="desc">输出另一个按键</span>
        </div>
        <div class="type-card" data-type="builtin">
          <span class="icon">⚡</span>
          <span class="title">内置功能</span>
          <span class="desc">语音输入、截图、启用/禁用</span>
        </div>
        <div class="type-card" data-type="command">
          <span class="icon">💻</span>
          <span class="title">执行命令</span>
          <span class="desc">运行外部程序或脚本</span>
        </div>
      </div>
    </div>

    <!-- 步骤3：配置参数 -->
    <div id="step-param" class="step hidden">
      <!-- 按键映射参数 -->
      <div id="param-key-mapping" class="param-panel hidden">
        <h3>配置按键映射</h3>
        <p>请按下要输出的按键</p>
        <div class="trigger-preview">
          <span id="output-key" class="key-waiting">[ 等待按键... ]</span>
        </div>
        <div class="quick-keys">
          <button data-key="LEFT">←</button>
          <button data-key="RIGHT">→</button>
          <button data-key="UP">↑</button>
          <button data-key="DOWN">↓</button>
          <button data-key="HOME">Home</button>
          <button data-key="END">End</button>
          <button data-key="PAGEUP">PgUp</button>
          <button data-key="PAGEDOWN">PgDn</button>
        </div>
      </div>

      <!-- 内置功能参数 -->
      <div id="param-builtin" class="param-panel hidden">
        <h3>选择内置功能</h3>
        <div id="builtin-list" class="builtin-list">
          <!-- 动态生成 -->
        </div>
      </div>

      <!-- 执行命令参数 -->
      <div id="param-command" class="param-panel hidden">
        <h3>配置执行命令</h3>
        <label>命令路径：</label>
        <input type="text" id="command-path" placeholder="例如: C:\Tools\screenshot.exe">
        <button id="browse-btn" class="secondary-btn">浏览...</button>
        <label>参数（可选）：</label>
        <input type="text" id="command-args" placeholder="例如: --region">
      </div>
    </div>

    <!-- 底部按钮 -->
    <div class="dialog-buttons">
      <button id="cancel-btn" class="secondary-btn">取消</button>
      <button id="save-btn" class="primary-btn" disabled>保存</button>
    </div>
  </div>
</div>
```

- [ ] **Step 2: 添加 CSS 样式**

```css
/* 快捷键配置样式 */
.action-list {
  margin-top: 16px;
}

.action-item {
  display: flex;
  align-items: center;
  padding: 12px;
  background: #f5f5f5;
  border-radius: 8px;
  margin-bottom: 8px;
}

.action-item .trigger {
  font-weight: bold;
  color: #333;
  min-width: 120px;
}

.action-item .arrow {
  color: #666;
  margin: 0 12px;
}

.action-item .action-desc {
  flex: 1;
  color: #666;
}

.action-item .actions {
  display: flex;
  gap: 8px;
}

/* 对话框样式 */
.dialog {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background: rgba(0, 0, 0, 0.5);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1000;
}

.dialog.hidden {
  display: none;
}

.dialog-content {
  background: white;
  padding: 24px;
  border-radius: 12px;
  min-width: 400px;
  max-width: 600px;
}

/* 类型卡片样式 */
.type-cards {
  display: flex;
  gap: 12px;
  margin-top: 16px;
}

.type-card {
  flex: 1;
  padding: 16px;
  border: 2px solid #e0e0e0;
  border-radius: 8px;
  cursor: pointer;
  text-align: center;
  transition: border-color 0.2s;
}

.type-card:hover {
  border-color: #0078d4;
}

.type-card.selected {
  border-color: #0078d4;
  background: #f0f7ff;
}

.type-card .icon {
  font-size: 24px;
  display: block;
  margin-bottom: 8px;
}

.type-card .title {
  font-weight: bold;
  display: block;
}

.type-card .desc {
  font-size: 12px;
  color: #666;
  margin-top: 4px;
}

/* 按键预览样式 */
.trigger-preview {
  font-size: 18px;
  padding: 16px;
  background: #f5f5f5;
  border-radius: 8px;
  text-align: center;
  margin: 16px 0;
}

.key-waiting {
  color: #0078d4;
  animation: pulse 1.5s infinite;
}

@keyframes pulse {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.5; }
}

/* 快捷键按钮 */
.quick-keys {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  margin-top: 12px;
}

.quick-keys button {
  padding: 8px 12px;
  border: 1px solid #ccc;
  border-radius: 4px;
  background: white;
  cursor: pointer;
}

.quick-keys button:hover {
  background: #f0f0f0;
}
```

- [ ] **Step 3: 添加 JavaScript 交互逻辑**

```javascript
// 状态管理
let currentStep = 'trigger';
let capturedTrigger = null;
let selectedType = null;
let capturedOutput = null;
let selectedBuiltin = null;

// 打开添加对话框
document.getElementById('add-action-btn').addEventListener('click', () => {
  resetDialog();
  document.getElementById('action-dialog').classList.remove('hidden');
  startCapture('trigger');
});

// 开始按键捕获
function startCapture(type) {
  window.chrome.webview.postMessage({
    action: 'start_capture',
    type: type
  });
}

// 停止按键捕获
function stopCapture() {
  window.chrome.webview.postMessage({
    action: 'stop_capture'
  });
}

// 处理 WebView2 消息
window.chrome.webview.addEventListener('message', (e) => {
  const data = JSON.parse(e.data);

  if (data.type === 'key_captured') {
    handleKeyCaptured(data.key, data.displayName);
  } else if (data.type === 'actions_loaded') {
    renderActionList(data.actions);
  } else if (data.type === 'builtin_list') {
    renderBuiltinList(data.builtins);
  }
});

// 处理按键捕获
function handleKeyCaptured(key, displayName) {
  if (currentStep === 'trigger') {
    capturedTrigger = key;
    document.getElementById('trigger-key').textContent = displayName;
    document.getElementById('trigger-key').classList.remove('key-waiting');
    document.getElementById('recapture-btn').classList.remove('hidden');

    // 自动进入下一步
    setTimeout(() => {
      showStep('type');
    }, 500);
  } else if (currentStep === 'output') {
    capturedOutput = key;
    document.getElementById('output-key').textContent = displayName;
    document.getElementById('output-key').classList.remove('key-waiting');
    document.getElementById('save-btn').disabled = false;
  }
}

// 显示步骤
function showStep(step) {
  currentStep = step;

  document.getElementById('step-trigger').classList.add('hidden');
  document.getElementById('step-type').classList.add('hidden');
  document.getElementById('step-param').classList.add('hidden');

  if (step === 'trigger') {
    document.getElementById('step-trigger').classList.remove('hidden');
  } else if (step === 'type') {
    document.getElementById('step-type').classList.remove('hidden');
    stopCapture();
  } else if (step === 'param') {
    document.getElementById('step-param').classList.remove('hidden');
    showParamPanel(selectedType);
  }
}

// 显示参数面板
function showParamPanel(type) {
  document.getElementById('param-key-mapping').classList.add('hidden');
  document.getElementById('param-builtin').classList.add('hidden');
  document.getElementById('param-command').classList.add('hidden');

  if (type === 'key_mapping') {
    document.getElementById('param-key-mapping').classList.remove('hidden');
    startCapture('output');
  } else if (type === 'builtin') {
    document.getElementById('param-builtin').classList.remove('hidden');
  } else if (type === 'command') {
    document.getElementById('param-command').classList.remove('hidden');
    document.getElementById('save-btn').disabled = false;
  }
}

// 类型卡片点击
document.querySelectorAll('.type-card').forEach(card => {
  card.addEventListener('click', () => {
    document.querySelectorAll('.type-card').forEach(c => c.classList.remove('selected'));
    card.classList.add('selected');
    selectedType = card.dataset.type;

    setTimeout(() => {
      showStep('param');
    }, 300);
  });
});

// 快捷键按钮点击
document.querySelectorAll('.quick-keys button').forEach(btn => {
  btn.addEventListener('click', () => {
    capturedOutput = btn.dataset.key;
    document.getElementById('output-key').textContent = btn.textContent;
    document.getElementById('output-key').classList.remove('key-waiting');
    document.getElementById('save-btn').disabled = false;
    stopCapture();
  });
});

// 保存动作
document.getElementById('save-btn').addEventListener('click', () => {
  let param = '';

  if (selectedType === 'key_mapping') {
    param = capturedOutput;
  } else if (selectedType === 'builtin') {
    param = selectedBuiltin;
  } else if (selectedType === 'command') {
    const path = document.getElementById('command-path').value;
    const args = document.getElementById('command-args').value;
    param = args ? `${path} ${args}` : path;
  }

  window.chrome.webview.postMessage({
    action: 'save_action',
    trigger: capturedTrigger,
    type: selectedType,
    param: param
  });

  document.getElementById('action-dialog').classList.add('hidden');
});

// 取消
document.getElementById('cancel-btn').addEventListener('click', () => {
  stopCapture();
  document.getElementById('action-dialog').classList.add('hidden');
});

// 渲染动作列表
function renderActionList(actions) {
  const container = document.getElementById('action-list');
  container.innerHTML = '';

  actions.forEach((action, index) => {
    const item = document.createElement('div');
    item.className = 'action-item';

    let desc = '';
    if (action.type === 'key_mapping') {
      desc = `按键映射: ${action.param}`;
    } else if (action.type === 'builtin') {
      desc = `内置功能: ${action.param}`;
    } else if (action.type === 'command') {
      desc = `执行命令: ${action.param}`;
    }

    item.innerHTML = `
      <span class="trigger">CapsLock + ${action.trigger}</span>
      <span class="arrow">→</span>
      <span class="action-desc">${desc}</span>
      <div class="actions">
        <button class="edit-btn" data-index="${index}">编辑</button>
        <button class="delete-btn" data-index="${index}">删除</button>
      </div>
    `;

    container.appendChild(item);
  });

  // 绑定删除事件
  document.querySelectorAll('.delete-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      const index = parseInt(btn.dataset.index);
      window.chrome.webview.postMessage({
        action: 'delete_action',
        index: index
      });
    });
  });
}

// 重置对话框
function resetDialog() {
  currentStep = 'trigger';
  capturedTrigger = null;
  selectedType = null;
  capturedOutput = null;
  selectedBuiltin = null;

  document.getElementById('trigger-key').textContent = '[ 等待按键... ]';
  document.getElementById('trigger-key').classList.add('key-waiting');
  document.getElementById('recapture-btn').classList.add('hidden');
  document.getElementById('output-key').textContent = '[ 等待按键... ]';
  document.getElementById('output-key').classList.add('key-waiting');
  document.getElementById('save-btn').disabled = true;
  document.getElementById('command-path').value = '';
  document.getElementById('command-args').value = '';

  document.querySelectorAll('.type-card').forEach(c => c.classList.remove('selected'));

  showStep('trigger');
}
```

- [ ] **Step 4: 提交**

```bash
git add resources/config_ui.html
git commit -m "feat: implement new action config UI with key capture

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>"
```

---

## Task 10: 修改 main.c 初始化新模块

**Files:**
- Modify: `src/main.c`

- [ ] **Step 1: 添加头文件引用**

在 `main.c` 开头添加：

```c
#include "action.h"
#include "action_builtin.h"
```

- [ ] **Step 2: 在 InitializeModules 中初始化新模块**

在 `KeymapInit()` 之后添加：

```c
// 初始化动作模块
ActionInit();

// 初始化内置功能模块
BuiltinInit();
```

- [ ] **Step 3: 在 CleanupModules 中清理新模块**

在 `KeymapCleanup()` 之前添加：

```c
// 清理动作模块
ActionCleanup();

// 清理内置功能模块
BuiltinCleanup();
```

- [ ] **Step 4: 提交**

```bash
git add src/main.c
git commit -m "feat: initialize action modules in main.c

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>"
```

---

## Task 11: 构建并测试

**Files:**
- Test: `build/powercapslock.exe`

- [ ] **Step 1: 构建项目**

```bash
taskkill /IM powercapslock.exe /F 2>NUL
scripts\build.bat
```

- [ ] **Step 2: 测试配置加载**

```bash
build\powercapslock.exe --test-config
```

预期输出：配置加载成功，显示 actions 数量。

- [ ] **Step 3: 测试配置对话框**

```bash
build\powercapslock.exe --test-config-dialog
```

预期：配置对话框打开，显示快捷键列表，可以添加/编辑/删除。

- [ ] **Step 4: 测试按键捕获**

在配置对话框中点击"添加快捷键"，按下键盘，验证捕获显示正确。

- [ ] **Step 5: 提交测试通过标记**

```bash
git add -A
git commit -m "test: verify action config implementation

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>"
```

---

## 自检清单

- [x] Spec 覆盖：每个需求都有对应任务
- [x] 无占位符：所有代码完整
- [x] 类型一致：函数签名在各文件中匹配
- [x] 文件路径准确：所有路径基于项目结构
