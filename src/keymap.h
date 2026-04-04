#ifndef KEYMAP_H
#define KEYMAP_H

#include <windows.h>
#include <stdbool.h>

// 键位映射结构
typedef struct {
    WORD scanCode;          // 物理按键扫描码（多语言支持）
    UINT targetVk;          // 目标 Virtual Key
    const char* name;       // 映射名称（用于日志）
} KeyMapping;

// 初始化键位映射模块
void KeymapInit(void);

// 清理键位映射模块
void KeymapCleanup(void);

// 根据扫描码查找映射
// 返回: 找到返回映射指针，否则返回NULL
const KeyMapping* KeymapFindByScanCode(WORD scanCode);

// 添加映射
void KeymapAddMapping(WORD scanCode, UINT targetVk, const char* name);

// 清除所有映射
void KeymapClear(void);

// 重置为默认映射
void KeymapResetToDefaults(void);

// 获取映射数量
int KeymapGetCount(void);

// 获取所有映射（用于配置保存）
const KeyMapping* KeymapGetAll(int* count);

#endif // KEYMAP_H
