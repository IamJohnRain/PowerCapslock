#ifndef HOOK_H
#define HOOK_H

#include <windows.h>
#include <stdbool.h>

// 安装键盘钩子
// 返回: 钩子句柄，失败返回NULL
HHOOK HookInstall(void);

// 卸载键盘钩子
void HookUninstall(HHOOK hHook);

// 设置启用状态
void HookSetEnabled(bool enabled);

// 获取启用状态
bool HookIsEnabled(void);

// 检查 CapsLock 是否按下
bool HookIsCapsLockHeld(void);

#endif // HOOK_H
