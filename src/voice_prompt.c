#include "voice_prompt.h"
#include "logger.h"
#include <windows.h>

// 窗口类名
static const char* WINDOW_CLASS = "PowerCapslockVoicePromptClass";

// 全局状态
static HWND g_hwnd = NULL;
static bool g_initialized = false;

// 窗口过程
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            // 获取窗口大小
            RECT rect;
            GetClientRect(hwnd, &rect);

            // 设置背景色（与 Toast 一致）
            HBRUSH hBrush = CreateSolidBrush(RGB(50, 50, 50));
            FillRect(hdc, &rect, hBrush);
            DeleteObject(hBrush);

            // 设置文本颜色
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkMode(hdc, TRANSPARENT);

            // 绘制文本（居中）
            const wchar_t* text = L"🎤 正在语音输入，请说话...";
            DrawTextW(hdc, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

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
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = WINDOW_CLASS;

    if (!RegisterClassExA(&wc)) {
        DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            LOG_ERROR("注册窗口类失败: %d", error);
            return false;
        }
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

    // 窗口大小（与 Toast 一致）
    int width = 350;
    int height = 80;

    // 获取屏幕工作区域
    RECT workArea;
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0)) {
        // 计算位置（屏幕底部居中，与 Toast 一致）
        int x = (workArea.right - workArea.left - width) / 2;
        int y = workArea.bottom - 100;  // 距离底部 100 像素

        // 创建窗口（样式与 Toast 一致）
        g_hwnd = CreateWindowExA(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
            WINDOW_CLASS,
            "VoicePrompt",
            WS_POPUP | WS_BORDER,
            x, y, width, height,
            NULL, NULL,
            GetModuleHandle(NULL),
            NULL
        );
    } else {
        // 备用：屏幕中央
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int x = (screenWidth - width) / 2;
        int y = (screenHeight - height) / 2;

        g_hwnd = CreateWindowExA(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
            WINDOW_CLASS,
            "VoicePrompt",
            WS_POPUP | WS_BORDER,
            x, y, width, height,
            NULL, NULL,
            GetModuleHandle(NULL),
            NULL
        );
    }

    if (g_hwnd == NULL) {
        LOG_ERROR("创建提示窗口失败: %d", GetLastError());
        return;
    }

    // 设置窗口透明效果（与 Toast 一致）
    SetLayeredWindowAttributes(g_hwnd, 0, 230, LWA_ALPHA);

    // 显示窗口
    ShowWindow(g_hwnd, SW_SHOWNA);

    LOG_DEBUG("语音提示窗口已显示");
}

void VoicePromptHide(void) {
    if (g_hwnd != NULL) {
        DestroyWindow(g_hwnd);
        g_hwnd = NULL;
        LOG_DEBUG("语音提示窗口已隐藏");
    }
}

bool VoicePromptIsVisible(void) {
    return g_hwnd != NULL && IsWindowVisible(g_hwnd);
}
