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
    WORD scanCode;              // 触发键扫描码（缓存，用于快速查找）
    ActionType type;            // 动作类型
    char param[256];            // 参数：按键名/内置功能名/命令路径
} Action;

// 初始化动作模块
void ActionInit(void);

// 清理动作模块
void ActionCleanup(void);

// 根据扫描码查找动作
// 返回: 找到返回动作指针，否则返回NULL
const Action* ActionFindByScanCode(WORD scanCode);

// 获取动作数量
int ActionGetCount(void);

// 获取指定索引的动作
// 返回: 索引有效返回动作指针，否则返回NULL
const Action* ActionGet(int index);

// 添加动作
// 返回: 成功返回索引(>=0)，失败返回-1
int ActionAdd(const Action* action);

// 更新动作
// 返回: 成功返回true，索引无效或参数无效返回false
bool ActionUpdate(int index, const Action* action);

// 删除动作
// 返回: 成功返回true，索引无效返回false
bool ActionDelete(int index);

// 重置为默认动作
void ActionResetToDefaults(void);

// 执行动作
// 返回: 成功返回true，动作无效或执行失败返回false
bool ActionExecute(const Action* action);

// 根据触发键名称查找动作
// 返回: 找到返回动作指针，否则返回NULL
const Action* ActionFindByTriggerName(const char* triggerName);

// 检查触发键是否已存在
// 返回: 存在返回true，否则返回false
bool ActionTriggerExists(const char* triggerName);

#endif // ACTION_H
