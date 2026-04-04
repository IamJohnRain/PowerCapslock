#include "tray.h"
#include "hook.h"
#include "config.h"
#include "logger.h"
#include <shellapi.h>
#include <stdio.h>

// 托盘状态
typedef struct {
    HWND hWnd;              // 托盘窗口句柄
    HMENU hMenu;            // 托盘菜单句柄
    HICON hIconNormal;      // 正常图标
    HICON hIconDisabled;    // 禁用图标
    NOTIFYICONDATAW nid;    // 托盘图标数据（使用Unicode版本）
    bool enabled;           // 当前状态
} TrayState;

static TrayState g_tray = {0};

// 托盘窗口类名
static const char* TRAY_WINDOW_CLASS = "JohnHotKeyMapTrayClass";

// 托盘窗口过程
static LRESULT CALLBACK TrayWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_DESTROY:
            // 移除托盘图标
            Shell_NotifyIconW(NIM_DELETE, &g_tray.nid);
            return 0;

        case WM_TRAYICON:
            // 托盘图标消息
            if (lParam == WM_LBUTTONUP) {
                // 左键单击：切换状态
                bool enabled = !HookIsEnabled();
                HookSetEnabled(enabled);
                TraySetState(enabled);
            }
            else if (lParam == WM_RBUTTONUP) {
                // 右键单击：显示菜单
                POINT pt;
                GetCursorPos(&pt);

                // 设置菜单项状态
                bool enabled = HookIsEnabled();
                EnableMenuItem(g_tray.hMenu, IDM_ENABLE, enabled ? MF_GRAYED : MF_ENABLED);
                EnableMenuItem(g_tray.hMenu, IDM_DISABLE, enabled ? MF_ENABLED : MF_GRAYED);

                // 显示菜单
                SetForegroundWindow(hWnd);
                TrackPopupMenu(g_tray.hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                              pt.x, pt.y, 0, hWnd, NULL);
            }
            return 0;

        case WM_COMMAND:
            // 菜单命令
            switch (LOWORD(wParam)) {
                case IDM_ENABLE:
                    HookSetEnabled(true);
                    TraySetState(true);
                    break;

                case IDM_DISABLE:
                    HookSetEnabled(false);
                    TraySetState(false);
                    break;

                case IDM_SHOW_LOG: {
                    const char* logPath = ConfigGetLogPath();
                    ShellExecuteA(NULL, "open", logPath, NULL, NULL, SW_SHOW);
                    break;
                }

                case IDM_ABOUT:
                    MessageBoxW(hWnd,
                        L"JohnHotKeyMap v1.0\n\n"
                        L"Windows 键盘热键映射工具\n"
                        L"将 CapsLock 改造为强大的修饰键\n\n"
                        L"作者: John\n"
                        L"许可: MIT License",
                        L"关于 JohnHotKeyMap",
                        MB_OK | MB_ICONINFORMATION);
                    break;

                case IDM_EXIT:
                    PostQuitMessage(0);
                    break;
            }
            return 0;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

HWND TrayInit(HINSTANCE hInstance) {
    // 注册窗口类
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = TrayWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = TRAY_WINDOW_CLASS;

    if (!RegisterClassExA(&wc)) {
        LOG_ERROR("Failed to register tray window class");
        return NULL;
    }

    // 创建隐藏窗口
    g_tray.hWnd = CreateWindowExA(
        0, TRAY_WINDOW_CLASS, "JohnHotKeyMap",
        0, 0, 0, 0, 0,
        HWND_MESSAGE, NULL, hInstance, NULL);

    if (g_tray.hWnd == NULL) {
        LOG_ERROR("Failed to create tray window");
        return NULL;
    }

    // 加载图标 - 使用MAKEINTRESOURCE宏
    g_tray.hIconNormal = LoadIconA(hInstance, MAKEINTRESOURCEA(101));
    g_tray.hIconDisabled = LoadIconA(hInstance, MAKEINTRESOURCEA(102));

    // 如果加载失败，使用默认图标
    if (g_tray.hIconNormal == NULL) {
        LOG_WARN("Failed to load normal icon, using default");
        g_tray.hIconNormal = LoadIcon(NULL, IDI_APPLICATION);
    } else {
        LOG_INFO("Normal icon loaded successfully");
    }
    
    if (g_tray.hIconDisabled == NULL) {
        LOG_WARN("Failed to load disabled icon, using default");
        g_tray.hIconDisabled = LoadIcon(NULL, IDI_WARNING);
    } else {
        LOG_INFO("Disabled icon loaded successfully");
    }

    // 初始化托盘图标数据 - 使用正确的结构大小
    memset(&g_tray.nid, 0, sizeof(NOTIFYICONDATA));
    g_tray.nid.cbSize = NOTIFYICONDATA_V2_SIZE;  // 使用V2版本大小
    g_tray.nid.hWnd = g_tray.hWnd;
    g_tray.nid.uID = 1;
    g_tray.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_tray.nid.uCallbackMessage = WM_TRAYICON;
    g_tray.nid.hIcon = g_tray.hIconNormal;
    // 使用宽字符版本设置提示文本
    wcscpy(g_tray.nid.szTip, L"JohnHotKeyMap - 启用");

    // 立即添加托盘图标（不等待WM_CREATE）
    if (!Shell_NotifyIconW(NIM_ADD, &g_tray.nid)) {
        LOG_ERROR("Failed to add tray icon, error: %d", GetLastError());
    } else {
        LOG_INFO("Tray icon added successfully");
    }

    // 创建菜单 - 使用Unicode版本
    g_tray.hMenu = CreatePopupMenu();
    AppendMenuW(g_tray.hMenu, MF_STRING, IDM_ENABLE, L"启用");
    AppendMenuW(g_tray.hMenu, MF_STRING, IDM_DISABLE, L"禁用");
    AppendMenuW(g_tray.hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(g_tray.hMenu, MF_STRING, IDM_SHOW_LOG, L"查看日志");
    AppendMenuW(g_tray.hMenu, MF_STRING, IDM_ABOUT, L"关于");
    AppendMenuW(g_tray.hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(g_tray.hMenu, MF_STRING, IDM_EXIT, L"退出");

    g_tray.enabled = true;

    LOG_INFO("System tray initialized");
    return g_tray.hWnd;
}

void TrayCleanup(void) {
    if (g_tray.hMenu != NULL) {
        DestroyMenu(g_tray.hMenu);
        g_tray.hMenu = NULL;
    }

    if (g_tray.hWnd != NULL) {
        DestroyWindow(g_tray.hWnd);
        g_tray.hWnd = NULL;
    }

    if (g_tray.hIconNormal != NULL) {
        DestroyIcon(g_tray.hIconNormal);
        g_tray.hIconNormal = NULL;
    }

    if (g_tray.hIconDisabled != NULL) {
        DestroyIcon(g_tray.hIconDisabled);
        g_tray.hIconDisabled = NULL;
    }

    LOG_INFO("System tray cleanup completed");
}

void TraySetState(bool enabled) {
    g_tray.enabled = enabled;

    if (enabled) {
        g_tray.nid.hIcon = g_tray.hIconNormal;
        wcscpy(g_tray.nid.szTip, L"JohnHotKeyMap - 启用");
    } else {
        g_tray.nid.hIcon = g_tray.hIconDisabled;
        wcscpy(g_tray.nid.szTip, L"JohnHotKeyMap - 禁用");
    }

    Shell_NotifyIconW(NIM_MODIFY, &g_tray.nid);
    LOG_DEBUG("Tray state changed: %s", enabled ? "enabled" : "disabled");
}

HWND TrayGetWindow(void) {
    return g_tray.hWnd;
}
