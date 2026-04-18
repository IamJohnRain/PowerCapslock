#include "screenshot_toolbar.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windowsx.h>

static const char* WINDOW_CLASS = "PowerCapslockScreenshotToolbar";
static const wchar_t* TOOLTIP_WINDOW_CLASS = L"PowerCapslockScreenshotTooltip";

static ToolbarContext g_toolbar = {0};
static bool g_initialized = false;
static ToolbarCallback g_callback = NULL;
static void* g_callbackData = NULL;
static WCHAR g_tooltipText[128] = L"";

static ToolbarButton g_defaultButtons[] = {
    { TOOLBAR_BTN_SAVE,   L"\x4FDD\x5B58 (Ctrl+S)",          "save", true },
    { TOOLBAR_BTN_COPY,   L"\x590D\x5236 (Ctrl+C)",          "copy", true },
    { TOOLBAR_BTN_PIN,    L"\x6302\x8D77\x5230\x5C4F\x5E55", "pin", true },
    { TOOLBAR_BTN_RECT,   L"\x77E9\x5F62\x6807\x6CE8",       "rect", true },
    { TOOLBAR_BTN_ARROW,  L"\x7BAD\x5934\x6807\x6CE8",       "arrow", true },
    { TOOLBAR_BTN_PENCIL, L"\x753B\x7B14\x6807\x6CE8",       "pencil", true },
    { TOOLBAR_BTN_CIRCLE, L"\x5706\x5F62\x6807\x6CE8",       "circle", true },
    { TOOLBAR_BTN_TEXT,   L"\x6587\x5B57\x6807\x6CE8",       "text", true },
    { TOOLBAR_BTN_OCR,    L"OCR \x8BC6\x522B",               "ocr", true },
    { TOOLBAR_BTN_CLOSE,  L"\x5173\x95ED (Esc)",             "close", true },
};

#define BUTTON_WIDTH  36
#define BUTTON_HEIGHT 28
#define BUTTON_MARGIN 2
#define BUTTON_PADDING 4

static LRESULT CALLBACK ToolbarWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK TooltipWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static RECT GetButtonRect(int index);
static int GetButtonAtPoint(ToolbarContext* ctx, int x, int y);
static void DrawToolbar(HDC hdc, ToolbarContext* ctx);
static void DrawRoundedButtonBackground(HDC hdc, const RECT* rect, bool hovered);
static void DrawButtonIcon(HDC hdc, ToolbarButtonType type, const RECT* rect, COLORREF color);
static void DrawCenteredTextIcon(HDC hdc, const wchar_t* text, const wchar_t* fontName, int fontSize, int fontWeight, const RECT* rect, COLORREF color);
static void CreateToolbarTooltip(ToolbarContext* ctx);
static void DestroyToolbarTooltip(ToolbarContext* ctx);
static void ShowToolbarTooltip(ToolbarContext* ctx, int buttonIndex);
static void HideToolbarTooltip(ToolbarContext* ctx);
static SIZE MeasureTooltipText(const wchar_t* text);
static void DrawTooltip(HDC hdc);
static void ExecuteButton(ToolbarButtonType type);
static void EnsureToolbarAboveOwner(ToolbarContext* ctx);

bool ScreenshotToolbarInit(void) {
    WNDCLASSEXA wc;
    WNDCLASSEXW tooltipWc;

    if (g_initialized) {
        return true;
    }

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
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

    ZeroMemory(&tooltipWc, sizeof(tooltipWc));
    tooltipWc.cbSize = sizeof(tooltipWc);
    tooltipWc.style = CS_HREDRAW | CS_VREDRAW;
    tooltipWc.lpfnWndProc = TooltipWndProc;
    tooltipWc.hInstance = GetModuleHandle(NULL);
    tooltipWc.hCursor = LoadCursor(NULL, IDC_ARROW);
    tooltipWc.hbrBackground = NULL;
    tooltipWc.lpszClassName = TOOLTIP_WINDOW_CLASS;

    if (!RegisterClassExW(&tooltipWc)) {
        DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            LOG_ERROR("[工具栏] 注册提示窗口类失败: %d", error);
            return false;
        }
    }

    g_toolbar.buttonCount = sizeof(g_defaultButtons) / sizeof(g_defaultButtons[0]);
    for (int i = 0; i < g_toolbar.buttonCount && i < TOOLBAR_BTN_COUNT; i++) {
        g_toolbar.buttons[i] = g_defaultButtons[i];
    }
    g_toolbar.hoveredButton = -1;
    g_toolbar.tooltipButton = -1;
    g_toolbar.pressedButton = -1;

    g_initialized = true;
    LOG_INFO("[工具栏] 模块初始化成功");
    return true;
}

void ScreenshotToolbarCleanup(void) {
    if (!g_initialized) {
        return;
    }

    DestroyToolbarTooltip(&g_toolbar);
    if (g_toolbar.hwnd != NULL) {
        DestroyWindow(g_toolbar.hwnd);
        g_toolbar.hwnd = NULL;
    }

    g_initialized = false;
    LOG_INFO("[工具栏] 模块已清理");
}

bool ScreenshotToolbarShow(const ScreenshotRect* selection, ToolbarCallback callback, void* userData) {
    int totalWidth;
    int windowHeight;
    int x;
    int y;
    HRGN region;

    if (!g_initialized) {
        LOG_ERROR("[工具栏] 模块未初始化");
        return false;
    }

    if (g_toolbar.isVisible) {
        ScreenshotToolbarUpdatePosition(selection);
        EnsureToolbarAboveOwner(&g_toolbar);
        return true;
    }

    g_callback = callback;
    g_callbackData = userData;

    totalWidth = g_toolbar.buttonCount * (BUTTON_WIDTH + BUTTON_MARGIN) + BUTTON_PADDING * 2 - BUTTON_MARGIN;
    windowHeight = BUTTON_HEIGHT + BUTTON_PADDING * 2;

    if (selection != NULL) {
        x = selection->x + (selection->width - totalWidth) / 2;
        y = selection->y - windowHeight - 5;
        if (y < 0) {
            y = selection->y + selection->height + 5;
        }
    } else {
        x = (GetSystemMetrics(SM_CXSCREEN) - totalWidth) / 2;
        y = GetSystemMetrics(SM_CYSCREEN) / 2;
    }

    g_toolbar.hwnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        WINDOW_CLASS,
        "ScreenshotToolbar",
        WS_POPUP,
        x, y, totalWidth, windowHeight,
        g_toolbar.ownerHwnd, NULL, GetModuleHandle(NULL), NULL);

    if (g_toolbar.hwnd == NULL) {
        LOG_ERROR("[工具栏] 创建窗口失败: %d", GetLastError());
        return false;
    }

    SetLayeredWindowAttributes(g_toolbar.hwnd, 0, 240, LWA_ALPHA);
    region = CreateRoundRectRgn(0, 0, totalWidth + 1, windowHeight + 1, 6, 6);
    SetWindowRgn(g_toolbar.hwnd, region, TRUE);
    CreateToolbarTooltip(&g_toolbar);

    g_toolbar.isVisible = true;
    ShowWindow(g_toolbar.hwnd, SW_SHOWNOACTIVATE);
    EnsureToolbarAboveOwner(&g_toolbar);
    UpdateWindow(g_toolbar.hwnd);

    LOG_INFO("[工具栏] 工具栏已显示: (%d, %d) %dx%d", x, y, totalWidth, windowHeight);
    return true;
}

void ScreenshotToolbarHide(void) {
    if (!g_toolbar.isVisible) {
        return;
    }

    DestroyToolbarTooltip(&g_toolbar);
    if (g_toolbar.hwnd != NULL) {
        DestroyWindow(g_toolbar.hwnd);
        g_toolbar.hwnd = NULL;
    }

    g_toolbar.isVisible = false;
    g_toolbar.hoveredButton = -1;
    g_toolbar.tooltipButton = -1;
    g_toolbar.pressedButton = -1;
    g_toolbar.trackingMouse = false;
    LOG_INFO("[工具栏] 工具栏已隐藏");
}

void ScreenshotToolbarSetOwner(HWND ownerHwnd) {
    g_toolbar.ownerHwnd = ownerHwnd;
    EnsureToolbarAboveOwner(&g_toolbar);
}

bool ScreenshotToolbarIsVisible(void) {
    return g_toolbar.isVisible;
}

HWND ScreenshotToolbarGetWindow(void) {
    return g_toolbar.hwnd;
}

void ScreenshotToolbarUpdatePosition(const ScreenshotRect* selection) {
    int totalWidth;
    int windowHeight;
    int x;
    int y;

    if (!g_toolbar.isVisible || g_toolbar.hwnd == NULL || selection == NULL) {
        return;
    }

    totalWidth = g_toolbar.buttonCount * (BUTTON_WIDTH + BUTTON_MARGIN) + BUTTON_PADDING * 2 - BUTTON_MARGIN;
    windowHeight = BUTTON_HEIGHT + BUTTON_PADDING * 2;
    x = selection->x + (selection->width - totalWidth) / 2;
    y = selection->y - windowHeight - 5;
    if (y < 0) {
        y = selection->y + selection->height + 5;
    }

    SetWindowPos(g_toolbar.hwnd, HWND_TOPMOST, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static void EnsureToolbarAboveOwner(ToolbarContext* ctx) {
    if (ctx == NULL || ctx->hwnd == NULL || !IsWindow(ctx->hwnd)) {
        return;
    }

    if (ctx->ownerHwnd != NULL && IsWindow(ctx->ownerHwnd)) {
        SetWindowLongPtr(ctx->hwnd, GWLP_HWNDPARENT, (LONG_PTR)ctx->ownerHwnd);
    }

    SetWindowPos(ctx->hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static RECT GetButtonRect(int index) {
    RECT rect;
    int x = BUTTON_PADDING + index * (BUTTON_WIDTH + BUTTON_MARGIN);
    int y = BUTTON_PADDING;
    rect.left = x;
    rect.top = y;
    rect.right = x + BUTTON_WIDTH;
    rect.bottom = y + BUTTON_HEIGHT;
    return rect;
}

static int GetButtonAtPoint(ToolbarContext* ctx, int x, int y) {
    for (int i = 0; i < ctx->buttonCount; i++) {
        RECT rect = GetButtonRect(i);
        if (x >= rect.left && x < rect.right &&
            y >= rect.top && y < rect.bottom &&
            ctx->buttons[i].enabled) {
            return i;
        }
    }
    return -1;
}

static void CreateToolbarTooltip(ToolbarContext* ctx) {
    if (ctx == NULL || ctx->tooltipHwnd != NULL) {
        return;
    }

    ctx->tooltipHwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        TOOLTIP_WINDOW_CLASS,
        NULL,
        WS_POPUP,
        0, 0, 1, 1,
        ctx->hwnd, NULL, GetModuleHandle(NULL), NULL);

    if (ctx->tooltipHwnd != NULL) {
        SetLayeredWindowAttributes(ctx->tooltipHwnd, 0, 245, LWA_ALPHA);
        SetWindowPos(ctx->tooltipHwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_HIDEWINDOW);
        ctx->tooltipButton = -1;
    }
}

static void DestroyToolbarTooltip(ToolbarContext* ctx) {
    if (ctx != NULL && ctx->tooltipHwnd != NULL) {
        DestroyWindow(ctx->tooltipHwnd);
        ctx->tooltipHwnd = NULL;
    }
    if (ctx != NULL) {
        ctx->tooltipButton = -1;
    }
}

static void ShowToolbarTooltip(ToolbarContext* ctx, int buttonIndex) {
    RECT toolbarRect;
    RECT buttonRect;
    SIZE textSize;
    int width;
    int height;
    int x;
    int y;
    HRGN region;

    if (ctx == NULL || ctx->tooltipHwnd == NULL || buttonIndex < 0 || buttonIndex >= ctx->buttonCount) {
        return;
    }

    wcsncpy(g_tooltipText, ctx->buttons[buttonIndex].tooltip, 127);
    g_tooltipText[127] = L'\0';
    textSize = MeasureTooltipText(g_tooltipText);
    width = textSize.cx + 20;
    height = textSize.cy + 12;

    GetWindowRect(ctx->hwnd, &toolbarRect);
    buttonRect = GetButtonRect(buttonIndex);
    x = toolbarRect.left + buttonRect.left + (BUTTON_WIDTH - width) / 2;
    y = toolbarRect.bottom + 8;

    region = CreateRoundRectRgn(0, 0, width + 1, height + 1, 8, 8);
    SetWindowRgn(ctx->tooltipHwnd, region, TRUE);
    SetWindowPos(ctx->tooltipHwnd, HWND_TOPMOST, x, y, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(ctx->tooltipHwnd, NULL, TRUE);
    ctx->tooltipButton = buttonIndex;
}

static void HideToolbarTooltip(ToolbarContext* ctx) {
    if (ctx != NULL && ctx->tooltipHwnd != NULL) {
        ShowWindow(ctx->tooltipHwnd, SW_HIDE);
        ctx->tooltipButton = -1;
    }
}

static SIZE MeasureTooltipText(const wchar_t* text) {
    SIZE size = {0};
    HDC hdc = GetDC(NULL);
    HFONT font;
    HFONT oldFont;
    RECT rect = {0, 0, 0, 0};

    if (hdc == NULL || text == NULL) {
        return size;
    }

    font = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
    oldFont = (HFONT)SelectObject(hdc, font);
    DrawTextW(hdc, text, -1, &rect, DT_CALCRECT | DT_SINGLELINE);
    size.cx = rect.right - rect.left;
    size.cy = rect.bottom - rect.top;
    SelectObject(hdc, oldFont);
    DeleteObject(font);
    ReleaseDC(NULL, hdc);
    return size;
}

static void DrawTooltip(HDC hdc) {
    RECT clientRect;
    HFONT font;
    HFONT oldFont;
    HBRUSH bgBrush;
    HPEN borderPen;
    HPEN oldPen;
    HBRUSH oldBrush;

    if (hdc == NULL) {
        return;
    }

    GetClientRect(g_toolbar.tooltipHwnd, &clientRect);
    bgBrush = CreateSolidBrush(RGB(31, 41, 55));
    borderPen = CreatePen(PS_SOLID, 1, RGB(108, 122, 143));
    oldBrush = (HBRUSH)SelectObject(hdc, bgBrush);
    oldPen = (HPEN)SelectObject(hdc, borderPen);
    RoundRect(hdc, clientRect.left, clientRect.top, clientRect.right, clientRect.bottom, 8, 8);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(borderPen);
    DeleteObject(bgBrush);

    InflateRect(&clientRect, -10, -6);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    font = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
    oldFont = (HFONT)SelectObject(hdc, font);
    DrawTextW(hdc, g_tooltipText, -1, &clientRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
    DeleteObject(font);
}

static void DrawToolbar(HDC hdc, ToolbarContext* ctx) {
    RECT clientRect;
    HDC hdcMem;
    HBITMAP hBitmap;
    HBITMAP hOldBitmap;
    HBRUSH hBgBrush;

    GetClientRect(ctx->hwnd, &clientRect);
    hdcMem = CreateCompatibleDC(hdc);
    hBitmap = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
    hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);

    hBgBrush = CreateSolidBrush(RGB(45, 45, 45));
    FillRect(hdcMem, &clientRect, hBgBrush);
    DeleteObject(hBgBrush);

    for (int i = 0; i < ctx->buttonCount; i++) {
        ToolbarButton* btn = &ctx->buttons[i];
        RECT btnRect = GetButtonRect(i);
        bool isHovered = (i == ctx->hoveredButton);
        COLORREF iconColor = btn->enabled ? RGB(245, 245, 245) : RGB(135, 135, 135);
        DrawRoundedButtonBackground(hdcMem, &btnRect, isHovered);
        DrawButtonIcon(hdcMem, btn->type, &btnRect, iconColor);
    }

    BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, hdcMem, 0, 0, SRCCOPY);
    SelectObject(hdcMem, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
}

static void DrawRoundedButtonBackground(HDC hdc, const RECT* rect, bool hovered) {
    HBRUSH brush = CreateSolidBrush(hovered ? RGB(66, 78, 98) : RGB(45, 45, 45));
    HPEN pen = CreatePen(PS_SOLID, 1, hovered ? RGB(108, 122, 143) : RGB(45, 45, 45));
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, brush);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    RoundRect(hdc, rect->left + 1, rect->top + 1, rect->right - 1, rect->bottom - 1, 7, 7);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

static void DrawCenteredTextIcon(HDC hdc, const wchar_t* text, const wchar_t* fontName, int fontSize, int fontWeight, const RECT* rect, COLORREF color) {
    HFONT font = CreateFontW(fontSize, 0, 0, 0, fontWeight, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, fontName);
    HFONT oldFont = (HFONT)SelectObject(hdc, font);
    RECT textRect = *rect;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    DrawTextW(hdc, text, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
    DeleteObject(font);
}

static void DrawButtonIcon(HDC hdc, ToolbarButtonType type, const RECT* rect, COLORREF color) {
    int cx = (rect->left + rect->right) / 2;
    int cy = (rect->top + rect->bottom) / 2;
    HPEN hPen = CreatePen(PS_SOLID, 1, color);
    HPEN hPenBold = CreatePen(PS_SOLID, 2, color);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));

    switch (type) {
        case TOOLBAR_BTN_SAVE:
            DrawCenteredTextIcon(hdc, L"\xE74E", L"Segoe MDL2 Assets", 18, FW_NORMAL, rect, color);
            break;
        case TOOLBAR_BTN_COPY:
            DrawCenteredTextIcon(hdc, L"\xE8C8", L"Segoe MDL2 Assets", 18, FW_NORMAL, rect, color);
            break;
        case TOOLBAR_BTN_PIN:
            DrawCenteredTextIcon(hdc, L"\xE718", L"Segoe MDL2 Assets", 18, FW_NORMAL, rect, color);
            break;
        case TOOLBAR_BTN_PENCIL:
            DrawCenteredTextIcon(hdc, L"\xE70F", L"Segoe MDL2 Assets", 18, FW_NORMAL, rect, color);
            break;
        case TOOLBAR_BTN_CLOSE:
            DrawCenteredTextIcon(hdc, L"\xE711", L"Segoe MDL2 Assets", 18, FW_NORMAL, rect, color);
            break;
        case TOOLBAR_BTN_RECT:
            SelectObject(hdc, hPenBold);
            RoundRect(hdc, cx - 10, cy - 7, cx + 10, cy + 7, 3, 3);
            break;
        case TOOLBAR_BTN_ARROW: {
            POINT head[3];
            SelectObject(hdc, hPenBold);
            MoveToEx(hdc, cx - 9, cy + 7, NULL);
            LineTo(hdc, cx + 7, cy - 7);
            head[0].x = cx + 8; head[0].y = cy - 8;
            head[1].x = cx + 1; head[1].y = cy - 7;
            head[2].x = cx + 7; head[2].y = cy;
            Polygon(hdc, head, 3);
            break;
        }
        case TOOLBAR_BTN_CIRCLE:
            SelectObject(hdc, hPenBold);
            Ellipse(hdc, cx - 9, cy - 9, cx + 9, cy + 9);
            break;
        case TOOLBAR_BTN_TEXT:
            DrawCenteredTextIcon(hdc, L"T", L"Georgia", 20, FW_BOLD, rect, color);
            break;
        case TOOLBAR_BTN_OCR:
            DrawCenteredTextIcon(hdc, L"OCR", L"Segoe UI", 9, FW_BOLD, rect, color);
            break;
        default:
            break;
    }

    SelectObject(hdc, hOldBrush);
    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
    DeleteObject(hPenBold);
}

static void ExecuteButton(ToolbarButtonType type) {
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
            if (!g_toolbar.trackingMouse) {
                TRACKMOUSEEVENT tme;
                ZeroMemory(&tme, sizeof(tme));
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                g_toolbar.trackingMouse = TrackMouseEvent(&tme) ? true : false;
            }
            if (hovered != g_toolbar.hoveredButton) {
                g_toolbar.hoveredButton = hovered;
                if (hovered >= 0) {
                    ShowToolbarTooltip(&g_toolbar, hovered);
                } else {
                    HideToolbarTooltip(&g_toolbar);
                }
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            g_toolbar.trackingMouse = false;
            if (g_toolbar.hoveredButton != -1) {
                g_toolbar.hoveredButton = -1;
                HideToolbarTooltip(&g_toolbar);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        case WM_LBUTTONDOWN: {
            int clicked;
            HideToolbarTooltip(&g_toolbar);
            clicked = GetButtonAtPoint(&g_toolbar, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            g_toolbar.pressedButton = clicked;
            if (clicked >= 0) {
                SetCapture(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            int pressed = g_toolbar.pressedButton;
            int released = GetButtonAtPoint(&g_toolbar, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            if (GetCapture() == hwnd) {
                ReleaseCapture();
            }
            g_toolbar.pressedButton = -1;
            InvalidateRect(hwnd, NULL, FALSE);
            if (pressed >= 0 && pressed == released && pressed < g_toolbar.buttonCount) {
                ExecuteButton(g_toolbar.buttons[pressed].type);
                EnsureToolbarAboveOwner(&g_toolbar);
            }
            return 0;
        }
        case WM_CAPTURECHANGED:
            g_toolbar.pressedButton = -1;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        case WM_DESTROY:
            DestroyToolbarTooltip(&g_toolbar);
            g_toolbar.hwnd = NULL;
            g_toolbar.isVisible = false;
            g_toolbar.trackingMouse = false;
            g_toolbar.pressedButton = -1;
            return 0;
        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

static LRESULT CALLBACK TooltipWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            DrawTooltip(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

int ScreenshotToolbarTest(void) {
    MSG msg;
    DWORD startTime;
    ScreenshotRect selection = {100, 100, 300, 200};

    if (!ScreenshotToolbarInit()) {
        return 1;
    }

    if (!ScreenshotToolbarShow(&selection, NULL, NULL)) {
        ScreenshotToolbarCleanup();
        return 1;
    }

    startTime = GetTickCount();
    while (GetTickCount() - startTime < 3000 && g_toolbar.isVisible) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Sleep(10);
    }

    ScreenshotToolbarHide();
    ScreenshotToolbarCleanup();
    return 0;
}
