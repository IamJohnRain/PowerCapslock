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

// 处理 Hook 线程上的自定义消息/定时器消息
bool HookHandleMessage(const MSG* msg);

// 运行 CapsLock+A 自测，slowStartDelayMs 用于模拟慢速语音启动
bool HookRunCapsLockATest(DWORD slowStartDelayMs, const char* outputPath);

#endif // HOOK_H
