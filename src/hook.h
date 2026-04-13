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

bool HookRunKeyMappingTest(WORD scanCode, UINT expectedVk, const char* outputPath);

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

#endif // HOOK_H
