#include "tray.h"
#include "hook.h"
#include "config.h"
#include "logger.h"
#include <shellapi.h>
#include <stdio.h>
#include <windowsx.h>

// 全局提示框窗口类名
static const char* TOAST_WINDOW_CLASS = "JohnHotKeyMapToastClass";

// 全局提示框窗口句柄
static HWND g_toastWindow = NULL;

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

// 定时器ID
#define TIMER_CAPSLOCK_CHECK 100
#define TIMER_TOAST_AUTO_CLOSE 101

// CapsLock 检测定时器间隔（毫秒）
#define CAPSLOCK_CHECK_INTERVAL 5000

// 上一次 CapsLock 状态
static bool g_lastCapsLockState = false;

// 前置声明
static void ToastDestroy(void);

// 托盘窗口过程
static LRESULT CALLBACK TrayWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_DESTROY:
            // 移除托盘图标
            Shell_NotifyIconW(NIM_DELETE, &g_tray.nid);
            // 停止定时器
            KillTimer(hWnd, TIMER_CAPSLOCK_CHECK);
            return 0;

        case WM_TIMER:
            if (wParam == TIMER_CAPSLOCK_CHECK) {
                // 检测 CapsLock 状态
                bool capsLockOn = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
                if (capsLockOn && g_tray.enabled) {
                    // CapsLock 开启且程序启用，自动关闭
                    INPUT inputs[2] = {0};
                    inputs[0].type = INPUT_KEYBOARD;
                    inputs[0].ki.wVk = VK_CAPITAL;
                    inputs[1].type = INPUT_KEYBOARD;
                    inputs[1].ki.wVk = VK_CAPITAL;
                    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
                    SendInput(2, inputs, sizeof(INPUT));
                    LOG_DEBUG("CapsLock auto-disabled");
                    TrayShowToast("PowerCapslock", L"已自动关闭大写锁定");
                }
                g_lastCapsLockState = capsLockOn;
            } else if (wParam == TIMER_TOAST_AUTO_CLOSE) {
                ToastDestroy();
            }
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

    // 启动 CapsLock 检测定时器
    SetTimer(g_tray.hWnd, TIMER_CAPSLOCK_CHECK, CAPSLOCK_CHECK_INTERVAL, NULL);
    LOG_DEBUG("CapsLock monitor started");

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
        TrayShowToast("PowerCapslock", L"已启用");
        // 启动 CapsLock 检测定时器
        SetTimer(g_tray.hWnd, TIMER_CAPSLOCK_CHECK, CAPSLOCK_CHECK_INTERVAL, NULL);
        LOG_DEBUG("CapsLock monitor started");
    } else {
        g_tray.nid.hIcon = g_tray.hIconDisabled;
        wcscpy(g_tray.nid.szTip, L"JohnHotKeyMap - 禁用");
        TrayShowToast("PowerCapslock", L"已禁用");
        // 停止 CapsLock 检测定时器
        KillTimer(g_tray.hWnd, TIMER_CAPSLOCK_CHECK);
        LOG_DEBUG("CapsLock monitor stopped");
    }

    Shell_NotifyIconW(NIM_MODIFY, &g_tray.nid);
    LOG_DEBUG("Tray state changed: %s", enabled ? "enabled" : "disabled");
}

HWND TrayGetWindow(void) {
    return g_tray.hWnd;
}

// 全局提示框窗口过程
static LRESULT CALLBACK ToastWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_TIMER:
            if (wParam == TIMER_TOAST_AUTO_CLOSE) {
                ToastDestroy();
            }
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            // 获取窗口大小
            RECT rect;
            GetClientRect(hWnd, &rect);

            // 设置背景色
            HBRUSH hBrush = CreateSolidBrush(RGB(50, 50, 50));
            FillRect(hdc, &rect, hBrush);
            DeleteObject(hBrush);

            // 设置文本颜色
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkMode(hdc, TRANSPARENT);

            // 获取窗口文本
            wchar_t text[256];
            GetWindowTextW(hWnd, text, 256);

            // 绘制文本（居中）
            DrawTextW(hdc, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            EndPaint(hWnd, &ps);
            return 0;
        }
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

// 创建并显示全局提示框
void TrayShowToast(const char* title, const wchar_t* message) {
    // 先销毁之前的提示框
    ToastDestroy();

    // 创建隐藏窗口
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = ToastWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = TOAST_WINDOW_CLASS;

    if (!RegisterClassExA(&wc)) {
        LOG_DEBUG("Toast window class already registered or failed");
    }

    g_toastWindow = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,  // 置顶、工具窗口、不激活
        TOAST_WINDOW_CLASS, "Toast",
        WS_POPUP | WS_BORDER,  // 弹出窗口、边框
        CW_USEDEFAULT, CW_USEDEFAULT,
        350, 80,                         // 宽度 350, 高度 80
        NULL, NULL, GetModuleHandle(NULL), NULL);

    if (g_toastWindow == NULL) {
        LOG_ERROR("Failed to create toast window");
        return;
    }

    // 设置窗口透明效果
    SetWindowLongPtrW(g_toastWindow, GWL_EXSTYLE,
                      GetWindowLongPtrW(g_toastWindow, GWL_EXSTYLE) | WS_EX_LAYERED);
    SetLayeredWindowAttributes(g_toastWindow, 0, 230, LWA_ALPHA);

    // 获取屏幕尺寸，计算位置（屏幕底部居中）
    RECT workArea;
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0)) {
        int x = (workArea.right - workArea.left - 350) / 2;
        int y = workArea.bottom - 100;  // 距离底部 100 像素
        SetWindowPos(g_toastWindow, HWND_TOPMOST, x, y, 350, 80,
                     SWP_SHOWWINDOW | SWP_NOACTIVATE);
    } else {
        SetWindowPos(g_toastWindow, HWND_TOPMOST, 0, 0, 350, 80,
                     SWP_SHOWWINDOW | SWP_NOACTIVATE);
    }

    // 显示窗口
    ShowWindow(g_toastWindow, SW_SHOWNA);

    // 自动关闭定时器（2秒）
    SetTimer(g_toastWindow, TIMER_TOAST_AUTO_CLOSE, 2000, NULL);

    LOG_DEBUG("Toast shown: %S", message);
}

// 销毁全局提示框
static void ToastDestroy(void) {
    if (g_toastWindow != NULL) {
        KillTimer(g_toastWindow, TIMER_TOAST_AUTO_CLOSE);
        DestroyWindow(g_toastWindow);
        g_toastWindow = NULL;
        UnregisterClassA(TOAST_WINDOW_CLASS, GetModuleHandle(NULL));
    }
}
