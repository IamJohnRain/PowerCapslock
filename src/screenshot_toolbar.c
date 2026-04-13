#include "screenshot_toolbar.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windowsx.h>

static const char* WINDOW_CLASS = "PowerCapslockScreenshotToolbar";

static ToolbarContext g_toolbar = {0};
static bool g_initialized = false;
static ToolbarCallback g_callback = NULL;
static void* g_callbackData = NULL;

// 按钮默认定义
static ToolbarButton g_defaultButtons[] = {
    { TOOLBAR_BTN_SAVE,   L"保存 (Ctrl+S)",   "💾", true },
    { TOOLBAR_BTN_COPY,   L"复制 (Ctrl+C)",   "📋", true },
    { TOOLBAR_BTN_PIN,    L"置顶",            "📌", true },
    { TOOLBAR_BTN_RECT,   L"矩形",            "▢", true },
    { TOOLBAR_BTN_ARROW,  L"箭头",            "→", true },
    { TOOLBAR_BTN_PENCIL, L"画笔",            "✎", true },
    { TOOLBAR_BTN_CIRCLE, L"圆形",            "○", true },
    { TOOLBAR_BTN_TEXT,   L"文字",            "T", true },
    { TOOLBAR_BTN_OCR,    L"OCR识别",         "OCR", true },
    { TOOLBAR_BTN_CLOSE,  L"关闭 (Esc)",      "✕", true },
};

// 按钮尺寸
#define BUTTON_WIDTH  36
#define BUTTON_HEIGHT 28
#define BUTTON_MARGIN 2
#define BUTTON_PADDING 4

// 前向声明
static LRESULT CALLBACK ToolbarWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static void DrawToolbar(HDC hdc, ToolbarContext* ctx);
static int GetButtonAtPoint(ToolbarContext* ctx, int x, int y);
static void ExecuteButton(ToolbarButtonType type);

bool ScreenshotToolbarInit(void) {
    if (g_initialized) {
        LOG_DEBUG("[工具栏] 模块已初始化");
        return true;
    }

    LOG_DEBUG("[工具栏] 开始初始化...");

    // 注册窗口类
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = ToolbarWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = WINDOW_CLASS;

    if (!RegisterClassExA(&wc)) {
        DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            LOG_ERROR("[工具栏] 注册窗口类失败: %d", error);
            return false;
        }
    }

    // 初始化按钮
    g_toolbar.buttonCount = sizeof(g_defaultButtons) / sizeof(g_defaultButtons[0]);
    for (int i = 0; i < g_toolbar.buttonCount && i < TOOLBAR_BTN_COUNT; i++) {
        g_toolbar.buttons[i] = g_defaultButtons[i];
    }
    g_toolbar.hoveredButton = -1;

    g_initialized = true;
    LOG_INFO("[工具栏] 模块初始化成功");
    return true;
}

void ScreenshotToolbarCleanup(void) {
    if (!g_initialized) {
        return;
    }

    LOG_DEBUG("[工具栏] 开始清理...");

    if (g_toolbar.hwnd != NULL) {
        DestroyWindow(g_toolbar.hwnd);
        g_toolbar.hwnd = NULL;
    }

    g_initialized = false;
    LOG_INFO("[工具栏] 模块已清理");
}

bool ScreenshotToolbarShow(const ScreenshotRect* selection, ToolbarCallback callback, void* userData) {
    if (!g_initialized) {
        LOG_ERROR("[工具栏] 模块未初始化");
        return false;
    }

    if (g_toolbar.isVisible) {
        LOG_DEBUG("[工具栏] 工具栏已显示，更新位置");
        ScreenshotToolbarUpdatePosition(selection);
        return true;
    }

    LOG_DEBUG("[工具栏] 显示工具栏...");

    g_callback = callback;
    g_callbackData = userData;

    // 计算窗口大小
    int totalWidth = g_toolbar.buttonCount * (BUTTON_WIDTH + BUTTON_MARGIN) + BUTTON_PADDING * 2 - BUTTON_MARGIN;
    int windowHeight = BUTTON_HEIGHT + BUTTON_PADDING * 2;

    // 计算窗口位置（选区上方或下方）
    int x, y;
    if (selection != NULL) {
        x = selection->x + (selection->width - totalWidth) / 2;
        // 优先显示在选区上方
        y = selection->y - windowHeight - 5;
        if (y < 0) {
            // 上方空间不足，显示在选区下方
            y = selection->y + selection->height + 5;
        }
    } else {
        // 默认位置：屏幕中央
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        x = (screenWidth - totalWidth) / 2;
        y = screenHeight / 2;
    }

    // 创建窗口
    g_toolbar.hwnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        WINDOW_CLASS,
        "ScreenshotToolbar",
        WS_POPUP,
        x, y, totalWidth, windowHeight,
        NULL, NULL, GetModuleHandle(NULL), NULL);

    if (g_toolbar.hwnd == NULL) {
        LOG_ERROR("[工具栏] 创建窗口失败: %d", GetLastError());
        return false;
    }

    // 设置窗口透明度
    SetLayeredWindowAttributes(g_toolbar.hwnd, 0, 240, LWA_ALPHA);

    // 设置窗口区域（圆角）
    HRGN region = CreateRoundRectRgn(0, 0, totalWidth + 1, windowHeight + 1, 6, 6);
    SetWindowRgn(g_toolbar.hwnd, region, TRUE);

    g_toolbar.isVisible = true;

    // 显示窗口
    ShowWindow(g_toolbar.hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(g_toolbar.hwnd);

    LOG_INFO("[工具栏] 工具栏已显示: (%d, %d) %dx%d", x, y, totalWidth, windowHeight);
    return true;
}

void ScreenshotToolbarHide(void) {
    if (!g_toolbar.isVisible) {
        return;
    }

    LOG_DEBUG("[工具栏] 隐藏工具栏");

    if (g_toolbar.hwnd != NULL) {
        DestroyWindow(g_toolbar.hwnd);
        g_toolbar.hwnd = NULL;
    }

    g_toolbar.isVisible = false;
    g_toolbar.hoveredButton = -1;

    LOG_INFO("[工具栏] 工具栏已隐藏");
}

bool ScreenshotToolbarIsVisible(void) {
    return g_toolbar.isVisible;
}

void ScreenshotToolbarUpdatePosition(const ScreenshotRect* selection) {
    if (!g_toolbar.isVisible || g_toolbar.hwnd == NULL || selection == NULL) {
        return;
    }

    // 计算窗口大小
    int totalWidth = g_toolbar.buttonCount * (BUTTON_WIDTH + BUTTON_MARGIN) + BUTTON_PADDING * 2 - BUTTON_MARGIN;
    int windowHeight = BUTTON_HEIGHT + BUTTON_PADDING * 2;

    // 计算新位置
    int x = selection->x + (selection->width - totalWidth) / 2;
    int y = selection->y - windowHeight - 5;
    if (y < 0) {
        y = selection->y + selection->height + 5;
    }

    SetWindowPos(g_toolbar.hwnd, NULL, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
}

static void DrawToolbar(HDC hdc, ToolbarContext* ctx) {
    RECT clientRect;
    GetClientRect(ctx->hwnd, &clientRect);

    // 创建内存 DC
    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);

    // 绘制背景
    HBRUSH hBgBrush = CreateSolidBrush(RGB(45, 45, 45));
    FillRect(hdcMem, &clientRect, hBgBrush);
    DeleteObject(hBgBrush);

    // 绘制按钮
    int x = BUTTON_PADDING;
    int y = BUTTON_PADDING;

    for (int i = 0; i < ctx->buttonCount; i++) {
        ToolbarButton* btn = &ctx->buttons[i];
        RECT btnRect = { x, y, x + BUTTON_WIDTH, y + BUTTON_HEIGHT };

        // 绘制按钮背景
        if (i == ctx->hoveredButton) {
            // 悬停状态
            HBRUSH hHoverBrush = CreateSolidBrush(RGB(80, 80, 80));
            FillRect(hdcMem, &btnRect, hHoverBrush);
            DeleteObject(hHoverBrush);
        }

        // 绘制按钮图标（使用文字代替）
        SetBkMode(hdcMem, TRANSPARENT);
        SetTextColor(hdcMem, RGB(255, 255, 255));

        HFONT hFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        HFONT hOldFont = (HFONT)SelectObject(hdcMem, hFont);

        // 简化图标显示
        const wchar_t* iconText = L"?";
        switch (btn->type) {
            case TOOLBAR_BTN_SAVE:   iconText = L"S"; break;
            case TOOLBAR_BTN_COPY:   iconText = L"C"; break;
            case TOOLBAR_BTN_PIN:    iconText = L"P"; break;
            case TOOLBAR_BTN_RECT:   iconText = L"R"; break;
            case TOOLBAR_BTN_ARROW:  iconText = L"A"; break;
            case TOOLBAR_BTN_PENCIL: iconText = L"D"; break;
            case TOOLBAR_BTN_CIRCLE: iconText = L"O"; break;
            case TOOLBAR_BTN_TEXT:   iconText = L"T"; break;
            case TOOLBAR_BTN_OCR:    iconText = L"R"; break;
            case TOOLBAR_BTN_CLOSE:  iconText = L"X"; break;
            default: break;
        }

        SIZE textSize;
        GetTextExtentPoint32W(hdcMem, iconText, 1, &textSize);
        int textX = btnRect.left + (BUTTON_WIDTH - textSize.cx) / 2;
        int textY = btnRect.top + (BUTTON_HEIGHT - textSize.cy) / 2;
        TextOutW(hdcMem, textX, textY, iconText, 1);

        SelectObject(hdcMem, hOldFont);
        DeleteObject(hFont);

        x += BUTTON_WIDTH + BUTTON_MARGIN;
    }

    // 复制到屏幕
    BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, hdcMem, 0, 0, SRCCOPY);

    // 清理
    SelectObject(hdcMem, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
}

static int GetButtonAtPoint(ToolbarContext* ctx, int x, int y) {
    int btnX = BUTTON_PADDING;
    int btnY = BUTTON_PADDING;

    for (int i = 0; i < ctx->buttonCount; i++) {
        if (x >= btnX && x < btnX + BUTTON_WIDTH &&
            y >= btnY && y < btnY + BUTTON_HEIGHT) {
            return i;
        }
        btnX += BUTTON_WIDTH + BUTTON_MARGIN;
    }

    return -1;
}

static void ExecuteButton(ToolbarButtonType type) {
    LOG_DEBUG("[工具栏] 执行按钮: %d", type);

    if (g_callback != NULL) {
        g_callback(type, g_callbackData);
    }
}

static LRESULT CALLBACK ToolbarWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            DrawToolbar(hdc, &g_toolbar);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            int hovered = GetButtonAtPoint(&g_toolbar, x, y);
            if (hovered != g_toolbar.hoveredButton) {
                g_toolbar.hoveredButton = hovered;
                InvalidateRect(hwnd, NULL, FALSE);

                // 设置工具提示
                if (hovered >= 0 && hovered < g_toolbar.buttonCount) {
                    // 可以在这里显示工具提示
                }
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            int clicked = GetButtonAtPoint(&g_toolbar, x, y);
            if (clicked >= 0 && clicked < g_toolbar.buttonCount) {
                ExecuteButton(g_toolbar.buttons[clicked].type);
            }
            return 0;
        }

        case WM_DESTROY:
            g_toolbar.hwnd = NULL;
            g_toolbar.isVisible = false;
            LOG_DEBUG("[工具栏] 窗口已销毁");
            return 0;

        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

int ScreenshotToolbarTest(void) {
    LOG_INFO("[工具栏测试] 开始测试...");

    if (!ScreenshotToolbarInit()) {
        LOG_ERROR("[工具栏测试] 初始化失败");
        return 1;
    }

    printf("Showing toolbar for 3 seconds...\n");

    ScreenshotRect selection = { 100, 100, 300, 200 };
    if (!ScreenshotToolbarShow(&selection, NULL, NULL)) {
        LOG_ERROR("[工具栏测试] 显示工具栏失败");
        ScreenshotToolbarCleanup();
        return 1;
    }

    // 消息循环
    MSG msg;
    DWORD startTime = GetTickCount();
    while (GetTickCount() - startTime < 3000 && g_toolbar.isVisible) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Sleep(10);
    }

    ScreenshotToolbarHide();
    ScreenshotToolbarCleanup();

    LOG_INFO("[工具栏测试] 测试完成");
    printf("Test completed.\n");
    return 0;
}
