#include "hook.h"
#include "keymap.h"
#include "logger.h"
#include <stdio.h>

// 全局状态
typedef struct {
    bool enabled;           // 是否启用
    bool capslockHeld;      // CapsLock 是否按下
    HHOOK hHook;            // 钩子句柄
} HookState;

static HookState g_hook = {0};

// 获取当前修饰键物理状态（使用 GetAsyncKeyState 获取实时状态）
static void GetModifierState(bool* shift, bool* ctrl, bool* alt) {
    *shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    *ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    *alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
}

// 检查是否是扩展键（导航键、数字小键盘等）
static bool IsExtendedKey(UINT vk) {
    switch (vk) {
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
        case VK_HOME:
        case VK_END:
        case VK_PRIOR:  // PageUp
        case VK_NEXT:   // PageDown
        case VK_INSERT:
        case VK_DELETE:
        case VK_NUMLOCK:
        case VK_DIVIDE:
        case VK_RCONTROL:
        case VK_RMENU:
        case VK_SNAPSHOT:
        case VK_CANCEL:
            return true;
        default:
            return false;
    }
}

// 发送按键输入（保留修饰键状态）
// 由于修饰键（Shift/Ctrl/Alt）物理按下时系统会自动识别，
// 我们只需要发送目标键即可，系统会自动组合成 Shift+目标键 等
static void SendKeyInput(UINT vk, bool keyDown) {
    INPUT input = {0};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.dwFlags = keyDown ? 0 : KEYEVENTF_KEYUP;

    // 扩展键需要设置 KEYEVENTF_EXTENDEDKEY 标志
    if (IsExtendedKey(vk)) {
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }

    UINT sent = SendInput(1, &input, sizeof(INPUT));
    if (sent != 1) {
        LOG_ERROR("SendInput failed for VK=%d, keyDown=%d", vk, keyDown);
    }
}

// 设置CapsLock LED状态
static void SetCapsLockLED(bool turnOn) {
    // 模拟按键来控制LED
    INPUT inputs[2] = {0};

    // 按下CapsLock
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CAPITAL;
    inputs[0].ki.dwFlags = 0;

    // 释放CapsLock
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = VK_CAPITAL;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

    // 获取当前CapsLock状态
    bool currentState = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;

    // 如果当前状态与目标状态不同，则切换
    if (currentState != turnOn) {
        SendInput(2, inputs, sizeof(INPUT));
        LOG_DEBUG("CapsLock LED toggled to: %s", turnOn ? "ON" : "OFF");
    }
}

// 低级别键盘钩子回调函数
static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    // 检查是否需要处理
    if (nCode < 0 || !g_hook.enabled) {
        return CallNextHookEx(g_hook.hHook, nCode, wParam, lParam);
    }

    // 获取键盘信息
    KBDLLHOOKSTRUCT* pKb = (KBDLLHOOKSTRUCT*)lParam;
    DWORD vkCode = pKb->vkCode;
    WORD scanCode = pKb->scanCode;
    bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    bool isKeyUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

    // 检查 CapsLock (VK_CAPITAL = 20)
    if (vkCode == VK_CAPITAL) {
        if (isKeyDown && !g_hook.capslockHeld) {
            g_hook.capslockHeld = true;
            LOG_DEBUG("CapsLock pressed (scanCode=0x%02X)", scanCode);
            return 1;  // 拦截
        }
        else if (isKeyUp && g_hook.capslockHeld) {
            g_hook.capslockHeld = false;
            LOG_DEBUG("CapsLock released");
            return 1;  // 拦截
        }
        return CallNextHookEx(g_hook.hHook, nCode, wParam, lParam);
    }

    // 如果 CapsLock 没按下，正常传递
    if (!g_hook.capslockHeld) {
        return CallNextHookEx(g_hook.hHook, nCode, wParam, lParam);
    }

    // 查找映射
    const KeyMapping* mapping = KeymapFindByScanCode(scanCode);
    if (mapping == NULL) {
        // 没有映射，正常传递
        return CallNextHookEx(g_hook.hHook, nCode, wParam, lParam);
    }

    // 找到映射，发送目标键（保留修饰键状态）
    if (isKeyDown) {
        bool shift = false, ctrl = false, alt = false;
        GetModifierState(&shift, &ctrl, &alt);
        LOG_DEBUG("Mapping triggered: %s (scanCode=0x%02X -> VK=%d) [Shift=%d Ctrl=%d Alt=%d]",
                  mapping->name, scanCode, mapping->targetVk, shift, ctrl, alt);
        SendKeyInput(mapping->targetVk, true);
        return 1;  // 拦截原按键
    }
    else if (isKeyUp) {
        SendKeyInput(mapping->targetVk, false);
        return 1;  // 拦截原按键
    }

    return CallNextHookEx(g_hook.hHook, nCode, wParam, lParam);
}

HHOOK HookInstall(void) {
    if (g_hook.hHook != NULL) {
        LOG_WARN("Hook already installed");
        return g_hook.hHook;
    }

    g_hook.hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                                     GetModuleHandle(NULL), 0);
    if (g_hook.hHook == NULL) {
        LOG_ERROR("Failed to install keyboard hook, error=%d", GetLastError());
        return NULL;
    }

    g_hook.enabled = true;

    // 安装钩子后，确保CapsLock LED熄灭（小写状态）
    SetCapsLockLED(false);

    LOG_INFO("Keyboard hook installed successfully");
    return g_hook.hHook;
}

void HookUninstall(HHOOK hHook) {
    if (hHook == NULL) {
        return;
    }

    if (UnhookWindowsHookEx(hHook)) {
        LOG_INFO("Keyboard hook uninstalled successfully");
    } else {
        LOG_ERROR("Failed to uninstall keyboard hook, error=%d", GetLastError());
    }

    if (hHook == g_hook.hHook) {
        g_hook.hHook = NULL;
        g_hook.enabled = false;
        g_hook.capslockHeld = false;
    }
}

void HookSetEnabled(bool enabled) {
    g_hook.enabled = enabled;
    if (!enabled) {
        g_hook.capslockHeld = false;
    } else {
        // 启用时，确保CapsLock LED熄灭（小写状态）
        SetCapsLockLED(false);
    }
    LOG_DEBUG("Hook enabled: %s", enabled ? "true" : "false");
}

bool HookIsEnabled(void) {
    return g_hook.enabled;
}

bool HookIsCapsLockHeld(void) {
    return g_hook.capslockHeld;
}
