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
