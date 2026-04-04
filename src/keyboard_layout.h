#ifndef KEYBOARD_LAYOUT_H
#define KEYBOARD_LAYOUT_H

#include <windows.h>

// 获取当前键盘布局
HKL KeyboardLayoutGetCurrent(void);

// Virtual Key 转 Scan Code
WORD KeyboardLayoutVkToScanCode(UINT vkCode);

// Scan Code 转 Virtual Key
UINT KeyboardLayoutScanCodeToVk(WORD scanCode);

// 获取布局名称
// 返回: 布局名称字符串，需要调用者释放
char* KeyboardLayoutGetName(HKL hkl);

// 初始化键盘布局模块
void KeyboardLayoutInit(void);

// 清理键盘布局模块
void KeyboardLayoutCleanup(void);

#endif // KEYBOARD_LAYOUT_H
