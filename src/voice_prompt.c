#include "voice_prompt.h"
#include "logger.h"
#include <windows.h>

// 窗口类名和标题
static const wchar_t* WINDOW_CLASS = L"PowerCapslockVoicePrompt";
static const wchar_t* WINDOW_TITLE = L"语音输入";

// 全局状态
static HWND g_hwnd = NULL;
static bool g_initialized = false;
static HFONT g_hFont = NULL;

// 窗口过程
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            // 获取窗口大小
            RECT rect;
            GetClientRect(hwnd, &rect);

            // 设置背景
            HBRUSH hBrush = CreateSolidBrush(RGB(45, 45, 45));
            FillRect(hdc, &rect, hBrush);
            DeleteObject(hBrush);

            // 绘制边框
            HPEN hPen = CreatePen(PS_SOLID, 2, RGB(0, 120, 215));
            SelectObject(hdc, hPen);
            Rectangle(hdc, 0, 0, rect.right, rect.bottom);
            DeleteObject(hPen);

            // 绘制文字
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkMode(hdc, TRANSPARENT);
            SelectObject(hdc, g_hFont);

            // 计算文字位置（居中）
            const wchar_t* text = L"🎤 正在语音输入，请说话...";
            SIZE textSize;
            GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &textSize);
            int x = (rect.right - textSize.cx) / 2;
            int y = (rect.bottom - textSize.cy) / 2;
            TextOutW(hdc, x, y, text, (int)wcslen(text));

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            g_hwnd = NULL;
            return 0;

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

bool VoicePromptInit(void) {
    if (g_initialized) {
        return true;
    }

    // 注册窗口类
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = WINDOW_CLASS;

    if (!RegisterClassExW(&wc)) {
        DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            LOG_ERROR("注册窗口类失败: %d", error);
            return false;
        }
    }

    // 创建字体
    g_hFont = CreateFontW(
        24,                        // 高度
        0,                         // 宽度
        0, 0,                      // 角度
        FW_NORMAL,                 // 粗细
        FALSE, FALSE, FALSE,       // 斜体、下划线、删除线
        DEFAULT_CHARSET,           // 字符集
        OUT_DEFAULT_PRECIS,        // 输出精度
        CLIP_DEFAULT_PRECIS,       // 裁剪精度
        DEFAULT_QUALITY,           // 质量
        DEFAULT_PITCH | FF_DONTCARE, // 间距和字体族
        L"Microsoft YaHei UI"      // 字体名
    );

    if (g_hFont == NULL) {
        LOG_WARN("创建字体失败，使用默认字体");
    }

    g_initialized = true;
    LOG_DEBUG("语音提示窗口模块初始化成功");
    return true;
}

void VoicePromptCleanup(void) {
    if (g_hwnd != NULL) {
        DestroyWindow(g_hwnd);
        g_hwnd = NULL;
    }

    if (g_hFont != NULL) {
        DeleteObject(g_hFont);
        g_hFont = NULL;
    }

    g_initialized = false;
    LOG_DEBUG("语音提示窗口资源已清理");
}

void VoicePromptShow(void) {
    if (!g_initialized) {
        LOG_WARN("语音提示窗口未初始化");
        return;
    }

    if (g_hwnd != NULL && IsWindowVisible(g_hwnd)) {
        return;  // 已显示
    }

    // 窗口大小
    int width = 320;
    int height = 60;

    // 获取屏幕大小
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);

    // 计算位置（屏幕顶部居中）
    int x = (screenWidth - width) / 2;
    int y = 80;  // 距离顶部一定距离

    // 创建窗口（无边框、置顶、工具窗口）
    g_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        WINDOW_CLASS,
        WINDOW_TITLE,
        WS_POPUP,
        x, y, width, height,
        NULL, NULL,
        GetModuleHandle(NULL),
        NULL
    );

    if (g_hwnd == NULL) {
        LOG_ERROR("创建提示窗口失败: %d", GetLastError());
        return;
    }

    // 设置窗口透明度
    SetLayeredWindowAttributes(g_hwnd, 0, 230, LWA_ALPHA);

    // 显示窗口
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(g_hwnd);

    LOG_DEBUG("语音提示窗口已显示");
}

void VoicePromptHide(void) {
    if (g_hwnd != NULL) {
        ShowWindow(g_hwnd, SW_HIDE);
        DestroyWindow(g_hwnd);
        g_hwnd = NULL;
        LOG_DEBUG("语音提示窗口已隐藏");
    }
}

bool VoicePromptIsVisible(void) {
    return g_hwnd != NULL && IsWindowVisible(g_hwnd);
}
