#include "keyboard_layout.h"
#include "logger.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

void KeyboardLayoutInit(void) {
    HKL hkl = KeyboardLayoutGetCurrent();
    char* name = KeyboardLayoutGetName(hkl);
    if (name != NULL) {
        LOG_INFO("Current keyboard layout: %s (0x%08X)", name, (UINT_PTR)hkl);
        free(name);
    }
}

void KeyboardLayoutCleanup(void) {
    // 无需清理
}

HKL KeyboardLayoutGetCurrent(void) {
    // 获取前台窗口的键盘布局
    HWND hwnd = GetForegroundWindow();
    if (hwnd != NULL) {
        DWORD threadId = GetWindowThreadProcessId(hwnd, NULL);
        return GetKeyboardLayout(threadId);
    }

    // 如果没有前台窗口，使用当前线程的布局
    return GetKeyboardLayout(0);
}

WORD KeyboardLayoutVkToScanCode(UINT vkCode) {
    HKL hkl = KeyboardLayoutGetCurrent();
    // 使用 MapVirtualKeyEx 进行转换
    // MAPVK_VK_TO_VSC: 将 VK 转换为 Scan Code
    return (WORD)MapVirtualKeyEx(vkCode, MAPVK_VK_TO_VSC, hkl);
}

UINT KeyboardLayoutScanCodeToVk(WORD scanCode) {
    HKL hkl = KeyboardLayoutGetCurrent();
    // 使用 MapVirtualKeyEx 进行转换
    // MAPVK_VSC_TO_VK: 将 Scan Code 转换为 VK
    return MapVirtualKeyEx(scanCode, MAPVK_VSC_TO_VK, hkl);
}

char* KeyboardLayoutGetName(HKL hkl) {
    // HKL 的低字包含语言标识符
    LANGID langId = LOWORD((DWORD_PTR)hkl);

    // 获取语言名称
    char langName[256];
    int len = GetLocaleInfoA(MAKELCID(langId, SORT_DEFAULT),
                             LOCALE_SLANGUAGE,
                             langName, sizeof(langName));

    if (len > 0) {
        char* result = (char*)malloc(strlen(langName) + 1);
        if (result != NULL) {
            strcpy(result, langName);
            return result;
        }
    }

    // 如果获取失败，返回十六进制表示
    char* result = (char*)malloc(16);
    if (result != NULL) {
        snprintf(result, 16, "0x%08X", (UINT_PTR)hkl);
    }
    return result;
}
