#include "tray.h"
#include "hook.h"
#include "config.h"
#include "logger.h"
#include "config_dialog.h"
#include <shellapi.h>
#include <stdio.h>
#include <wchar.h>
#include <windowsx.h>

// 全局提示框窗口类名
static const char* TOAST_WINDOW_CLASS = "JohnHotKeyMapToastClass";

// 全局提示框窗口句柄
static HWND g_toastWindow = NULL;
static HFONT g_toastTitleFont = NULL;
static HFONT g_toastMessageFont = NULL;
static wchar_t g_toastTitle[64] = L"PowerCapslock";
static wchar_t g_toastMessage[256] = L"";

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

                case IDM_CONFIGURE: {
                    ShowConfigDialog(hWnd);
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
    AppendMenuW(g_tray.hMenu, MF_STRING, IDM_CONFIGURE, L"配置");
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

static HFONT CreateToastFont(int pointSize, int weight) {
    HDC hdc = GetDC(NULL);
    int height = -MulDiv(pointSize, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(NULL, hdc);
    return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
}

static COLORREF ToastBlendColor(COLORREF a, COLORREF b, int percentB) {
    int percentA = 100 - percentB;
    return RGB(
        (GetRValue(a) * percentA + GetRValue(b) * percentB) / 100,
        (GetGValue(a) * percentA + GetGValue(b) * percentB) / 100,
        (GetBValue(a) * percentA + GetBValue(b) * percentB) / 100);
}

static void ToastFillGradient(HDC hdc, const RECT* rect, COLORREF topColor, COLORREF bottomColor) {
    int height = rect->bottom - rect->top;
    if (height <= 0) {
        return;
    }

    for (int y = 0; y < height; y++) {
        COLORREF color = ToastBlendColor(topColor, bottomColor, (y * 100) / height);
        HPEN pen = CreatePen(PS_SOLID, 1, color);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        MoveToEx(hdc, rect->left, rect->top + y, NULL);
        LineTo(hdc, rect->right, rect->top + y);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }
}

static COLORREF ToastAccentColor(void) {
    if (wcsstr(g_toastMessage, L"禁用") != NULL) {
        return RGB(255, 124, 118);
    }
    if (wcsstr(g_toastMessage, L"大写") != NULL) {
        return RGB(94, 218, 255);
    }
    return RGB(65, 226, 169);
}

static void DrawToast(HDC hdc, const RECT* rect) {
    COLORREF accent = ToastAccentColor();
    RECT title = {rect->left + 16, rect->top + 9, rect->right - 16, rect->top + 30};
    RECT message = {rect->left + 16, rect->top + 30, rect->right - 16, rect->top + 55};

    ToastFillGradient(hdc, rect, RGB(20, 43, 66), RGB(42, 81, 108));

    HPEN borderPen = CreatePen(PS_SOLID, 1, ToastBlendColor(RGB(140, 208, 255), accent, 25));
    HGDIOBJ oldPen = SelectObject(hdc, borderPen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
    RoundRect(hdc, rect->left, rect->top, rect->right, rect->bottom, 14, 14);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(borderPen);

    HPEN accentPen = CreatePen(PS_SOLID, 2, accent);
    oldPen = SelectObject(hdc, accentPen);
    int centerX = (rect->left + rect->right) / 2;
    MoveToEx(hdc, centerX - 34, rect->bottom - 9, NULL);
    LineTo(hdc, centerX + 34, rect->bottom - 9);
    SelectObject(hdc, oldPen);
    DeleteObject(accentPen);

    SetBkMode(hdc, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(hdc, g_toastTitleFont);
    SetTextColor(hdc, RGB(186, 225, 246));
    DrawTextW(hdc, g_toastTitle, -1, &title, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(hdc, g_toastMessageFont);
    SetTextColor(hdc, RGB(248, 253, 255));
    DrawTextW(hdc, g_toastMessage, -1, &message, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (oldFont != NULL) {
        SelectObject(hdc, oldFont);
    }
}

// 全局提示框窗口过程
static LRESULT CALLBACK ToastWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_TIMER:
            if (wParam == TIMER_TOAST_AUTO_CLOSE) {
                ToastDestroy();
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rect;
            GetClientRect(hWnd, &rect);
            DrawToast(hdc, &rect);
            EndPaint(hWnd, &ps);
            return 0;
        }
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

// 创建并显示全局提示框
void TrayShowToast(const char* title, const wchar_t* message) {
    ToastDestroy();

    int width = 260;
    int height = 66;

    if (title != NULL && title[0] != '\0') {
        MultiByteToWideChar(CP_UTF8, 0, title, -1, g_toastTitle,
                            (int)(sizeof(g_toastTitle) / sizeof(g_toastTitle[0])));
        g_toastTitle[(sizeof(g_toastTitle) / sizeof(g_toastTitle[0])) - 1] = L'\0';
    } else {
        wcscpy(g_toastTitle, L"PowerCapslock");
    }

    wcsncpy(g_toastMessage, message != NULL ? message : L"",
            (sizeof(g_toastMessage) / sizeof(g_toastMessage[0])) - 1);
    g_toastMessage[(sizeof(g_toastMessage) / sizeof(g_toastMessage[0])) - 1] = L'\0';

    g_toastTitleFont = CreateToastFont(8, FW_MEDIUM);
    g_toastMessageFont = CreateToastFont(11, FW_SEMIBOLD);

    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = ToastWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = TOAST_WINDOW_CLASS;

    if (!RegisterClassExA(&wc)) {
        LOG_DEBUG("Toast window class already registered or failed");
    }

    g_toastWindow = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        TOAST_WINDOW_CLASS, "Toast",
        WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT,
        width, height,
        NULL, NULL, GetModuleHandle(NULL), NULL);

    if (g_toastWindow == NULL) {
        LOG_ERROR("Failed to create toast window");
        ToastDestroy();
        return;
    }

    SetWindowTextW(g_toastWindow, g_toastMessage);
    SetLayeredWindowAttributes(g_toastWindow, 0, 238, LWA_ALPHA);

    HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, 16, 16);
    if (region != NULL && !SetWindowRgn(g_toastWindow, region, TRUE)) {
        DeleteObject(region);
    }

    POINT cursor;
    if (!GetCursorPos(&cursor)) {
        cursor.x = 0;
        cursor.y = 0;
    }

    RECT workArea = {0};
    MONITORINFO monitorInfo = {0};
    monitorInfo.cbSize = sizeof(monitorInfo);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    int x = 0;
    int y = 0;
    if (monitor != NULL && GetMonitorInfoW(monitor, &monitorInfo)) {
        workArea = monitorInfo.rcWork;
        x = workArea.left + (workArea.right - workArea.left - width) / 2;
        y = workArea.bottom - height - 48;
    } else {
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        x = (screenWidth - width) / 2;
        y = screenHeight - height - 72;
    }

    SetWindowPos(g_toastWindow, HWND_TOPMOST, x, y, width, height,
                 SWP_SHOWWINDOW | SWP_NOACTIVATE);

    SetTimer(g_toastWindow, TIMER_TOAST_AUTO_CLOSE, 2000, NULL);

    LOG_DEBUG("Toast shown: %S", g_toastMessage);
}

// 销毁全局提示框
static void ToastDestroy(void) {
    if (g_toastWindow != NULL) {
        KillTimer(g_toastWindow, TIMER_TOAST_AUTO_CLOSE);
        DestroyWindow(g_toastWindow);
        g_toastWindow = NULL;
        UnregisterClassA(TOAST_WINDOW_CLASS, GetModuleHandle(NULL));
    }

    if (g_toastTitleFont != NULL) {
        DeleteObject(g_toastTitleFont);
        g_toastTitleFont = NULL;
    }

    if (g_toastMessageFont != NULL) {
        DeleteObject(g_toastMessageFont);
        g_toastMessageFont = NULL;
    }
}
