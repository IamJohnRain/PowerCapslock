#include "screenshot_overlay.h"
#include "screenshot.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windowsx.h>

static const char* WINDOW_CLASS = "PowerCapslockScreenshotOverlay";

static OverlayContext g_overlay = {0};
static bool g_initialized = false;

// 前向声明
static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static void UpdateSelectionRect(OverlayContext* ctx);
static void DrawOverlay(HDC hdc, OverlayContext* ctx);
static void DrawSelectionRect(HDC hdc, OverlayContext* ctx);
static void DrawHoveredWindow(HDC hdc, OverlayContext* ctx);
static void DrawSizeTip(HDC hdc, OverlayContext* ctx);
static HWND GetWindowFromPointEx(POINT pt);
static void CaptureScreenToContext(OverlayContext* ctx);

bool ScreenshotOverlayInit(void) {
    if (g_initialized) {
        LOG_DEBUG("[选区窗口] 模块已初始化");
        return true;
    }

    LOG_DEBUG("[选区窗口] 开始初始化...");

    // 注册窗口类
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_CROSS);
    wc.hbrBackground = NULL;
    wc.lpszClassName = WINDOW_CLASS;

    if (!RegisterClassExA(&wc)) {
        DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            LOG_ERROR("[选区窗口] 注册窗口类失败: %d", error);
            return false;
        }
    }

    // 加载十字光标
    g_overlay.crossCursor = LoadCursor(NULL, IDC_CROSS);

    g_initialized = true;
    LOG_INFO("[选区窗口] 模块初始化成功");
    return true;
}

void ScreenshotOverlayCleanup(void) {
    if (!g_initialized) {
        return;
    }

    LOG_DEBUG("[选区窗口] 开始清理...");

    if (g_overlay.hwnd != NULL) {
        DestroyWindow(g_overlay.hwnd);
        g_overlay.hwnd = NULL;
    }

    if (g_overlay.screenBitmap != NULL) {
        DeleteObject(g_overlay.screenBitmap);
        g_overlay.screenBitmap = NULL;
    }

    if (g_overlay.screenDC != NULL) {
        DeleteDC(g_overlay.screenDC);
        g_overlay.screenDC = NULL;
    }

    if (g_overlay.screenImage != NULL) {
        ScreenshotImageFree(g_overlay.screenImage);
        g_overlay.screenImage = NULL;
    }

    g_initialized = false;
    LOG_INFO("[选区窗口] 模块已清理");
}

bool ScreenshotOverlayShow(void) {
    if (!g_initialized) {
        LOG_ERROR("[选区窗口] 模块未初始化");
        return false;
    }

    if (g_overlay.isActive) {
        LOG_DEBUG("[选区窗口] 窗口已激活，跳过");
        return true;
    }

    LOG_DEBUG("[选区窗口] 开始显示选区窗口...");

    // 获取虚拟屏幕尺寸
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    LOG_DEBUG("[选区窗口] 虚拟屏幕: (%d, %d) %dx%d", x, y, width, height);

    // 截取屏幕
    CaptureScreenToContext(&g_overlay);
    if (g_overlay.screenImage == NULL) {
        LOG_ERROR("[选区窗口] 截取屏幕失败");
        return false;
    }

    // 创建窗口
    g_overlay.hwnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE,
        WINDOW_CLASS,
        "ScreenshotOverlay",
        WS_POPUP,
        x, y, width, height,
        NULL, NULL, GetModuleHandle(NULL), NULL);

    if (g_overlay.hwnd == NULL) {
        LOG_ERROR("[选区窗口] 创建窗口失败: %d", GetLastError());
        return false;
    }

    // 设置窗口透明度
    SetLayeredWindowAttributes(g_overlay.hwnd, 0, 255, LWA_ALPHA);

    // 初始化状态
    g_overlay.isActive = true;
    g_overlay.isSelecting = false;
    g_overlay.state = OVERLAY_STATE_IDLE;
    g_overlay.selection.x = 0;
    g_overlay.selection.y = 0;
    g_overlay.selection.width = 0;
    g_overlay.selection.height = 0;

    // 显示窗口
    ShowWindow(g_overlay.hwnd, SW_SHOWNA);
    UpdateWindow(g_overlay.hwnd);

    // 设置焦点到窗口（用于接收键盘事件）
    SetForegroundWindow(g_overlay.hwnd);
    SetFocus(g_overlay.hwnd);

    LOG_INFO("[选区窗口] 窗口已显示");
    return true;
}

void ScreenshotOverlayHide(void) {
    if (!g_overlay.isActive) {
        return;
    }

    LOG_DEBUG("[选区窗口] 隐藏选区窗口");

    if (g_overlay.hwnd != NULL) {
        DestroyWindow(g_overlay.hwnd);
        g_overlay.hwnd = NULL;
    }

    if (g_overlay.screenBitmap != NULL) {
        DeleteObject(g_overlay.screenBitmap);
        g_overlay.screenBitmap = NULL;
    }

    if (g_overlay.screenDC != NULL) {
        DeleteDC(g_overlay.screenDC);
        g_overlay.screenDC = NULL;
    }

    if (g_overlay.screenImage != NULL) {
        ScreenshotImageFree(g_overlay.screenImage);
        g_overlay.screenImage = NULL;
    }

    g_overlay.isActive = false;
    g_overlay.isSelecting = false;
    g_overlay.state = OVERLAY_STATE_IDLE;

    LOG_INFO("[选区窗口] 窗口已隐藏");
}

bool ScreenshotOverlayIsActive(void) {
    return g_overlay.isActive;
}

const ScreenshotRect* ScreenshotOverlayGetSelection(void) {
    if (!g_overlay.isActive) {
        return NULL;
    }
    return &g_overlay.selection;
}

ScreenshotImage* ScreenshotOverlayGetSelectionImage(void) {
    if (!g_overlay.isActive || g_overlay.screenImage == NULL) {
        return NULL;
    }

    if (g_overlay.selection.width <= 0 || g_overlay.selection.height <= 0) {
        return NULL;
    }

    return ScreenshotImageCrop(g_overlay.screenImage,
                               g_overlay.selection.x, g_overlay.selection.y,
                               g_overlay.selection.width, g_overlay.selection.height);
}

static void CaptureScreenToContext(OverlayContext* ctx) {
    LOG_DEBUG("[选区窗口] 截取屏幕...");

    ctx->screenImage = ScreenshotCaptureAllMonitors();
    if (ctx->screenImage == NULL) {
        LOG_ERROR("[选区窗口] 截取屏幕失败");
        return;
    }

    // 创建内存 DC 和位图
    HDC hdcScreen = GetDC(NULL);
    ctx->screenDC = CreateCompatibleDC(hdcScreen);
    ctx->screenBitmap = CreateCompatibleBitmap(hdcScreen, ctx->screenImage->width, ctx->screenImage->height);
    ReleaseDC(NULL, hdcScreen);

    if (ctx->screenBitmap == NULL || ctx->screenDC == NULL) {
        LOG_ERROR("[选区窗口] 创建位图失败");
        return;
    }

    SelectObject(ctx->screenDC, ctx->screenBitmap);

    // 设置位图数据
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = ctx->screenImage->width;
    bmi.bmiHeader.biHeight = -ctx->screenImage->height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    SetDIBits(ctx->screenDC, ctx->screenBitmap, 0, ctx->screenImage->height,
              ctx->screenImage->pixels, &bmi, DIB_RGB_COLORS);

    LOG_DEBUG("[选区窗口] 屏幕截图完成: %dx%d", ctx->screenImage->width, ctx->screenImage->height);
}

static void UpdateSelectionRect(OverlayContext* ctx) {
    int x1 = ctx->startPoint.x;
    int y1 = ctx->startPoint.y;
    int x2 = ctx->currentPoint.x;
    int y2 = ctx->currentPoint.y;

    // 规范化矩形（确保 left < right, top < bottom）
    ctx->selection.x = (x1 < x2) ? x1 : x2;
    ctx->selection.y = (y1 < y2) ? y1 : y2;
    ctx->selection.width = abs(x2 - x1);
    ctx->selection.height = abs(y2 - y1);
}

static void DrawOverlay(HDC hdc, OverlayContext* ctx) {
    RECT clientRect;
    GetClientRect(ctx->hwnd, &clientRect);

    // 创建内存 DC 双缓冲
    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);

    // 绘制屏幕截图作为背景
    if (ctx->screenDC != NULL) {
        BitBlt(hdcMem, 0, 0, clientRect.right, clientRect.bottom,
               ctx->screenDC, 0, 0, SRCCOPY);
    }

    // 绘制半透明遮罩
    BLENDFUNCTION blend = {0};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 128;
    blend.AlphaFormat = 0;

    // 创建半透明遮罩层
    HDC hdcMask = CreateCompatibleDC(hdc);
    HBITMAP hMaskBitmap = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
    HBITMAP hOldMask = (HBITMAP)SelectObject(hdcMask, hMaskBitmap);

    // 填充半透明黑色
    RECT maskRect = clientRect;
    FillRect(hdcMask, &maskRect, (HBRUSH)GetStockObject(BLACK_BRUSH));

    // 如果有选区，清除选区内的遮罩
    if (ctx->isSelecting && ctx->selection.width > 0 && ctx->selection.height > 0) {
        RECT selRect;
        selRect.left = ctx->selection.x;
        selRect.top = ctx->selection.y;
        selRect.right = ctx->selection.x + ctx->selection.width;
        selRect.bottom = ctx->selection.y + ctx->selection.height;
        FillRect(hdcMask, &selRect, (HBRUSH)GetStockObject(WHITE_BRUSH));
    }

    // 混合遮罩层
    AlphaBlend(hdcMem, 0, 0, clientRect.right, clientRect.bottom,
               hdcMask, 0, 0, clientRect.right, clientRect.bottom, blend);

    // 清理遮罩资源
    SelectObject(hdcMask, hOldMask);
    DeleteObject(hMaskBitmap);
    DeleteDC(hdcMask);

    // 绘制悬停窗口高亮
    DrawHoveredWindow(hdcMem, ctx);

    // 绘制选区
    if (ctx->isSelecting) {
        DrawSelectionRect(hdcMem, ctx);
        DrawSizeTip(hdcMem, ctx);
    }

    // 复制到屏幕
    BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, hdcMem, 0, 0, SRCCOPY);

    // 清理
    SelectObject(hdcMem, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
}

static void DrawSelectionRect(HDC hdc, OverlayContext* ctx) {
    if (ctx->selection.width <= 0 || ctx->selection.height <= 0) {
        return;
    }

    // 创建虚线画笔
    LOGBRUSH lb = {0};
    lb.lbStyle = BS_SOLID;
    lb.lbColor = RGB(0, 120, 215);
    HPEN hPen = ExtCreatePen(PS_COSMETIC | PS_ALTERNATE, 1, &lb, 0, NULL);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

    // 选区矩形
    RECT selRect;
    selRect.left = ctx->selection.x;
    selRect.top = ctx->selection.y;
    selRect.right = ctx->selection.x + ctx->selection.width;
    selRect.bottom = ctx->selection.y + ctx->selection.height;

    // 绘制边框
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, selRect.left, selRect.top, selRect.right, selRect.bottom);
    SelectObject(hdc, hOldBrush);

    // 绘制四角标记
    int cornerSize = 10;
    HPEN hCornerPen = CreatePen(PS_SOLID, 2, RGB(0, 120, 215));
    SelectObject(hdc, hCornerPen);

    // 左上角
    MoveToEx(hdc, selRect.left, selRect.top + cornerSize, NULL);
    LineTo(hdc, selRect.left, selRect.top);
    LineTo(hdc, selRect.left + cornerSize, selRect.top);

    // 右上角
    MoveToEx(hdc, selRect.right - cornerSize, selRect.top, NULL);
    LineTo(hdc, selRect.right, selRect.top);
    LineTo(hdc, selRect.right, selRect.top + cornerSize);

    // 右下角
    MoveToEx(hdc, selRect.right, selRect.bottom - cornerSize, NULL);
    LineTo(hdc, selRect.right, selRect.bottom);
    LineTo(hdc, selRect.right - cornerSize, selRect.bottom);

    // 左下角
    MoveToEx(hdc, selRect.left + cornerSize, selRect.bottom, NULL);
    LineTo(hdc, selRect.left, selRect.bottom);
    LineTo(hdc, selRect.left, selRect.bottom - cornerSize);

    // 清理
    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
    DeleteObject(hCornerPen);
}

static void DrawHoveredWindow(HDC hdc, OverlayContext* ctx) {
    if (ctx->hoveredWindow == NULL || ctx->isSelecting) {
        return;
    }

    // 绘制窗口边界高亮
    HPEN hPen = CreatePen(PS_SOLID, 2, RGB(255, 0, 0));
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));

    Rectangle(hdc, ctx->hoveredRect.left, ctx->hoveredRect.top,
              ctx->hoveredRect.right, ctx->hoveredRect.bottom);

    SelectObject(hdc, hOldBrush);
    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
}

static void DrawSizeTip(HDC hdc, OverlayContext* ctx) {
    if (ctx->selection.width <= 0 || ctx->selection.height <= 0) {
        return;
    }

    // 创建字体
    HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

    // 准备尺寸文本
    WCHAR sizeText[64];
    swprintf(sizeText, 64, L"%d x %d", ctx->selection.width, ctx->selection.height);

    // 计算文本位置（选区上方）
    POINT pt;
    pt.x = ctx->selection.x;
    pt.y = ctx->selection.y - 25;
    if (pt.y < 0) {
        pt.y = ctx->selection.y + 5;
    }

    // 绘制背景
    SIZE textSize;
    GetTextExtentPoint32W(hdc, sizeText, wcslen(sizeText), &textSize);
    RECT bgRect;
    bgRect.left = pt.x;
    bgRect.top = pt.y;
    bgRect.right = pt.x + textSize.cx + 10;
    bgRect.bottom = pt.y + textSize.cy + 4;

    HBRUSH hBgBrush = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hdc, &bgRect, hBgBrush);
    DeleteObject(hBgBrush);

    // 绘制文本
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    TextOutW(hdc, pt.x + 5, pt.y + 2, sizeText, wcslen(sizeText));

    // 清理
    SelectObject(hdc, hOldFont);
    DeleteObject(hFont);
}

static HWND GetWindowFromPointEx(POINT pt) {
    // 获取鼠标下的窗口
    HWND hwnd = WindowFromPoint(pt);
    if (hwnd == NULL) {
        return NULL;
    }

    // 获取顶层窗口
    HWND hwndRoot = GetAncestor(hwnd, GA_ROOT);
    return hwndRoot;
}

static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            DrawOverlay(hdc, &g_overlay);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_LBUTTONDOWN: {
            g_overlay.isSelecting = true;
            g_overlay.startPoint.x = GET_X_LPARAM(lParam);
            g_overlay.startPoint.y = GET_Y_LPARAM(lParam);
            g_overlay.currentPoint = g_overlay.startPoint;
            g_overlay.state = OVERLAY_STATE_SELECTING;
            SetCapture(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            LOG_DEBUG("[选区窗口] 开始选区: (%d, %d)", g_overlay.startPoint.x, g_overlay.startPoint.y);
            return 0;
        }

        case WM_MOUSEMOVE: {
            POINT pt;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);

            if (g_overlay.isSelecting) {
                g_overlay.currentPoint = pt;
                UpdateSelectionRect(&g_overlay);
                InvalidateRect(hwnd, NULL, FALSE);
            } else {
                // 窗口识别
                HWND hovered = GetWindowFromPointEx(pt);
                if (hovered != g_overlay.hoveredWindow) {
                    g_overlay.hoveredWindow = hovered;
                    if (hovered != NULL) {
                        GetWindowRect(hovered, &g_overlay.hoveredRect);
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (g_overlay.isSelecting) {
                g_overlay.isSelecting = false;
                ReleaseCapture();

                if (g_overlay.selection.width > 5 && g_overlay.selection.height > 5) {
                    g_overlay.state = OVERLAY_STATE_SELECTED;
                    LOG_INFO("[选区窗口] 选区完成: (%d, %d) %dx%d",
                             g_overlay.selection.x, g_overlay.selection.y,
                             g_overlay.selection.width, g_overlay.selection.height);
                    // TODO: 显示工具栏
                } else {
                    LOG_DEBUG("[选区窗口] 选区太小，取消");
                    ScreenshotOverlayHide();
                }
            }
            return 0;
        }

        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP: {
            // 右键取消截图
            LOG_DEBUG("[选区窗口] 右键取消");
            ScreenshotOverlayHide();
            return 0;
        }

        case WM_KEYDOWN: {
            if (wParam == VK_ESCAPE) {
                LOG_DEBUG("[选区窗口] ESC 取消");
                ScreenshotOverlayHide();
                return 0;
            }
            break;
        }

        case WM_DESTROY:
            g_overlay.hwnd = NULL;
            g_overlay.isActive = false;
            LOG_DEBUG("[选区窗口] 窗口已销毁");
            return 0;

        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int ScreenshotOverlayTest(void) {
    LOG_INFO("[选区窗口测试] 开始测试...");

    if (!ScreenshotOverlayInit()) {
        LOG_ERROR("[选区窗口测试] 初始化失败");
        return 1;
    }

    printf("Showing overlay window for 3 seconds...\n");
    printf("Press ESC or right-click to cancel.\n");

    if (!ScreenshotOverlayShow()) {
        LOG_ERROR("[选区窗口测试] 显示窗口失败");
        ScreenshotOverlayCleanup();
        return 1;
    }

    // 消息循环（3秒后自动关闭）
    MSG msg;
    DWORD startTime = GetTickCount();
    while (GetTickCount() - startTime < 3000 && g_overlay.isActive) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Sleep(10);
    }

    ScreenshotOverlayHide();
    ScreenshotOverlayCleanup();

    LOG_INFO("[选区窗口测试] 测试完成");
    printf("Test completed.\n");
    return 0;
}
