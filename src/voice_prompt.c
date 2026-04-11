#include "voice_prompt.h"
#include "logger.h"
#include <windows.h>

static const char* WINDOW_CLASS = "PowerCapslockVoicePromptClass";

static HWND g_hwnd = NULL;
static bool g_initialized = false;
static HFONT g_titleFont = NULL;
static HFONT g_bodyFont = NULL;
static HFONT g_hintFont = NULL;

static HFONT CreatePromptFont(int pointSize, int weight) {
    HDC hdc = GetDC(NULL);
    int height = -MulDiv(pointSize, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(NULL, hdc);
    return CreateFontW(
        height,
        0,
        0,
        0,
        weight,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        L"Microsoft YaHei UI");
}

static COLORREF BlendColor(COLORREF a, COLORREF b, int percentB) {
    int percentA = 100 - percentB;
    return RGB(
        (GetRValue(a) * percentA + GetRValue(b) * percentB) / 100,
        (GetGValue(a) * percentA + GetGValue(b) * percentB) / 100,
        (GetBValue(a) * percentA + GetBValue(b) * percentB) / 100);
}

static void FillGradient(HDC hdc, const RECT* rect, COLORREF topColor, COLORREF bottomColor) {
    int height = rect->bottom - rect->top;
    if (height <= 0) {
        return;
    }

    for (int y = 0; y < height; y++) {
        COLORREF color = BlendColor(topColor, bottomColor, (y * 100) / height);
        HPEN pen = CreatePen(PS_SOLID, 1, color);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        MoveToEx(hdc, rect->left, rect->top + y, NULL);
        LineTo(hdc, rect->right, rect->top + y);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }
}

static void DrawRoundedGradient(HDC hdc, const RECT* rect, COLORREF topColor, COLORREF bottomColor, COLORREF borderColor, int radius) {
    HRGN clip = CreateRoundRectRgn(rect->left, rect->top, rect->right + 1, rect->bottom + 1, radius, radius);
    int saved = SaveDC(hdc);
    SelectClipRgn(hdc, clip);
    FillGradient(hdc, rect, topColor, bottomColor);
    RestoreDC(hdc, saved);
    DeleteObject(clip);

    HPEN pen = CreatePen(PS_SOLID, 1, borderColor);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
    RoundRect(hdc, rect->left, rect->top, rect->right, rect->bottom, radius, radius);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

static void DrawPrompt(HDC hdc, const RECT* rect) {
    RECT panel = *rect;
    RECT title = {panel.left + 16, panel.top + 9, panel.right - 16, panel.top + 32};
    RECT body = {panel.left + 16, panel.top + 32, panel.right - 16, panel.top + 56};
    RECT hint = {0, 0, 0, 0};

    DrawRoundedGradient(hdc, &panel, RGB(20, 43, 66), RGB(42, 81, 108), RGB(140, 208, 255), 14);

    HPEN accentPen = CreatePen(PS_SOLID, 2, RGB(94, 218, 255));
    HGDIOBJ oldPen = SelectObject(hdc, accentPen);
    int centerX = (panel.left + panel.right) / 2;
    MoveToEx(hdc, centerX - 34, panel.bottom - 9, NULL);
    LineTo(hdc, centerX + 34, panel.bottom - 9);
    SelectObject(hdc, oldPen);
    DeleteObject(accentPen);

    SetBkMode(hdc, TRANSPARENT);

    HGDIOBJ oldFont = SelectObject(hdc, g_titleFont);
    SetTextColor(hdc, RGB(248, 253, 255));
    DrawTextW(hdc, L"正在听写", -1, &title, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    SelectObject(hdc, g_bodyFont);
    SetTextColor(hdc, RGB(214, 238, 255));
    DrawTextW(hdc, L"松开 CapsLock + A 自动输入", -1, &body, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    SelectObject(hdc, g_hintFont);
    SetTextColor(hdc, RGB(157, 210, 236));
    DrawTextW(hdc, L"", -1, &hint, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    if (oldFont != NULL) {
        SelectObject(hdc, oldFont);
    }
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rect;
            GetClientRect(hwnd, &rect);
            DrawPrompt(hdc, &rect);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_DESTROY:
            g_hwnd = NULL;
            LOG_DEBUG("[语音提示] 窗口已销毁");
            return 0;

        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

bool VoicePromptInit(void) {
    if (g_initialized) {
        return true;
    }

    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = WINDOW_CLASS;

    if (!RegisterClassExA(&wc)) {
        DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            LOG_ERROR("[语音提示] 注册窗口类失败: %d", error);
            return false;
        }
    }

    g_titleFont = CreatePromptFont(12, FW_SEMIBOLD);
    g_bodyFont = CreatePromptFont(9, FW_NORMAL);
    g_hintFont = CreatePromptFont(8, FW_NORMAL);

    g_initialized = true;
    LOG_INFO("[语音提示] 模块初始化成功");
    return true;
}

void VoicePromptCleanup(void) {
    if (g_hwnd != NULL) {
        DestroyWindow(g_hwnd);
        g_hwnd = NULL;
    }

    if (g_titleFont != NULL) {
        DeleteObject(g_titleFont);
        g_titleFont = NULL;
    }
    if (g_bodyFont != NULL) {
        DeleteObject(g_bodyFont);
        g_bodyFont = NULL;
    }
    if (g_hintFont != NULL) {
        DeleteObject(g_hintFont);
        g_hintFont = NULL;
    }

    g_initialized = false;
    LOG_DEBUG("[语音提示] 模块已清理");
}

void VoicePromptShow(void) {
    if (!g_initialized) {
        LOG_ERROR("[语音提示] 模块未初始化");
        return;
    }

    if (g_hwnd != NULL && IsWindowVisible(g_hwnd)) {
        LOG_DEBUG("[语音提示] 窗口已显示，跳过");
        return;
    }

    int width = 260;
    int height = 66;
    int x = 0;
    int y = 0;

    POINT cursor;
    if (!GetCursorPos(&cursor)) {
        cursor.x = 0;
        cursor.y = 0;
    }

    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi;
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (monitor != NULL && GetMonitorInfoW(monitor, &mi)) {
        RECT workArea = mi.rcWork;
        x = workArea.left + (workArea.right - workArea.left - width) / 2;
        y = workArea.bottom - height - 48;
    } else {
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        x = (screenWidth - width) / 2;
        y = screenHeight - height - 72;
    }

    LOG_INFO("[语音提示] 创建窗口位置: (%d, %d), 大小: %dx%d", x, y, width, height);

    g_hwnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        WINDOW_CLASS,
        "VoicePrompt",
        WS_POPUP,
        x,
        y,
        width,
        height,
        NULL,
        NULL,
        GetModuleHandle(NULL),
        NULL);

    if (g_hwnd == NULL) {
        LOG_ERROR("[语音提示] 创建窗口失败: %d", GetLastError());
        return;
    }

    HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, 16, 16);
    if (region != NULL && !SetWindowRgn(g_hwnd, region, TRUE)) {
        DeleteObject(region);
    }

    if (!SetLayeredWindowAttributes(g_hwnd, 0, 238, LWA_ALPHA)) {
        LOG_WARN("[语音提示] 设置透明度失败: %d", GetLastError());
    }

    ShowWindow(g_hwnd, SW_SHOWNA);
    UpdateWindow(g_hwnd);

    LOG_INFO("[语音提示] 窗口已显示");
}

void VoicePromptHide(void) {
    if (g_hwnd != NULL) {
        LOG_INFO("[语音提示] 隐藏并销毁窗口");
        DestroyWindow(g_hwnd);
        g_hwnd = NULL;
    }
}

bool VoicePromptIsVisible(void) {
    return g_hwnd != NULL && IsWindowVisible(g_hwnd);
}
