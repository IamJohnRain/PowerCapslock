#include "screenshot_float.h"
#include "screenshot.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windowsx.h>

static const char* WINDOW_CLASS = "PowerCapslockScreenshotFloat";

static FloatWindowContext g_float = {0};
static bool g_initialized = false;

// 默认窗口尺寸限制
#define MIN_WINDOW_WIDTH  100
#define MIN_WINDOW_HEIGHT 80
#define MAX_WINDOW_WIDTH  800
#define MAX_WINDOW_HEIGHT 600
#define DEFAULT_OPACITY   0.95f

// 前向声明
static LRESULT CALLBACK FloatWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static void DrawFloatWindow(HDC hdc, FloatWindowContext* ctx);
static void CalculateWindowSize(FloatWindowContext* ctx, int* width, int* height);

bool ScreenshotFloatInit(void) {
    if (g_initialized) {
        LOG_DEBUG("[浮动窗口] 模块已初始化");
        return true;
    }

    LOG_DEBUG("[浮动窗口] 开始初始化...");

    // 注册窗口类
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = FloatWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = WINDOW_CLASS;

    if (!RegisterClassExA(&wc)) {
        DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            LOG_ERROR("[浮动窗口] 注册窗口类失败: %d", error);
            return false;
        }
    }

    g_float.opacity = DEFAULT_OPACITY;
    g_float.isPinned = false;

    g_initialized = true;
    LOG_INFO("[浮动窗口] 模块初始化成功");
    return true;
}

void ScreenshotFloatCleanup(void) {
    if (!g_initialized) {
        return;
    }

    LOG_DEBUG("[浮动窗口] 开始清理...");

    if (g_float.hwnd != NULL) {
        DestroyWindow(g_float.hwnd);
        g_float.hwnd = NULL;
    }

    if (g_float.image != NULL) {
        ScreenshotImageFree(g_float.image);
        g_float.image = NULL;
    }

    g_initialized = false;
    LOG_INFO("[浮动窗口] 模块已清理");
}

bool ScreenshotFloatShow(const ScreenshotImage* image, int x, int y) {
    if (!g_initialized) {
        LOG_ERROR("[浮动窗口] 模块未初始化");
        return false;
    }

    if (image == NULL) {
        LOG_ERROR("[浮动窗口] 图像为空");
        return false;
    }

    LOG_DEBUG("[浮动窗口] 显示浮动窗口...");

    // 如果已有窗口，先关闭
    if (g_float.hwnd != NULL) {
        DestroyWindow(g_float.hwnd);
        g_float.hwnd = NULL;
    }

    // 释放旧图像
    if (g_float.image != NULL) {
        ScreenshotImageFree(g_float.image);
    }

    // 复制图像
    g_float.image = ScreenshotImageDup(image);
    if (g_float.image == NULL) {
        LOG_ERROR("[浮动窗口] 复制图像失败");
        return false;
    }

    // 计算窗口大小
    int width, height;
    CalculateWindowSize(&g_float, &width, &height);

    // 计算窗口位置
    if (x < 0 || y < 0) {
        // 默认位置：屏幕右下角
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        x = screenWidth - width - 20;
        y = screenHeight - height - 60;
    }

    // 创建窗口
    g_float.hwnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        WINDOW_CLASS,
        "ScreenshotFloat",
        WS_POPUP | WS_THICKFRAME,
        x, y, width, height,
        NULL, NULL, GetModuleHandle(NULL), NULL);

    if (g_float.hwnd == NULL) {
        LOG_ERROR("[浮动窗口] 创建窗口失败: %d", GetLastError());
        ScreenshotImageFree(g_float.image);
        g_float.image = NULL;
        return false;
    }

    // 设置窗口区域（圆角）
    HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, 8, 8);
    SetWindowRgn(g_float.hwnd, region, TRUE);

    // 设置透明度
    BYTE alpha = (BYTE)(g_float.opacity * 255);
    SetLayeredWindowAttributes(g_float.hwnd, 0, alpha, LWA_ALPHA);

    // 显示窗口
    ShowWindow(g_float.hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(g_float.hwnd);

    LOG_INFO("[浮动窗口] 窗口已显示: (%d, %d) %dx%d", x, y, width, height);
    return true;
}

void ScreenshotFloatHide(void) {
    if (g_float.hwnd == NULL) {
        return;
    }

    LOG_DEBUG("[浮动窗口] 隐藏浮动窗口");

    DestroyWindow(g_float.hwnd);
    g_float.hwnd = NULL;

    if (g_float.image != NULL) {
        ScreenshotImageFree(g_float.image);
        g_float.image = NULL;
    }

    LOG_INFO("[浮动窗口] 窗口已隐藏");
}

bool ScreenshotFloatIsVisible(void) {
    return g_float.hwnd != NULL && IsWindowVisible(g_float.hwnd);
}

void ScreenshotFloatSetPinned(bool pinned) {
    g_float.isPinned = pinned;

    if (g_float.hwnd != NULL) {
        SetWindowPos(g_float.hwnd,
                     pinned ? HWND_TOPMOST : HWND_NOTOPMOST,
                     0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        LOG_DEBUG("[浮动窗口] 置顶状态: %s", pinned ? "是" : "否");
    }
}

void ScreenshotFloatSetOpacity(float opacity) {
    g_float.opacity = opacity;

    if (g_float.hwnd != NULL) {
        BYTE alpha = (BYTE)(opacity * 255);
        SetLayeredWindowAttributes(g_float.hwnd, 0, alpha, LWA_ALPHA);
    }
}

const ScreenshotImage* ScreenshotFloatGetImage(void) {
    return g_float.image;
}

static void CalculateWindowSize(FloatWindowContext* ctx, int* width, int* height) {
    if (ctx->image == NULL) {
        *width = MIN_WINDOW_WIDTH;
        *height = MIN_WINDOW_HEIGHT;
        return;
    }

    // 按比例缩放到合适大小
    int imgWidth = ctx->image->width;
    int imgHeight = ctx->image->height;

    // 如果图像太大，缩小
    if (imgWidth > MAX_WINDOW_WIDTH) {
        float scale = (float)MAX_WINDOW_WIDTH / imgWidth;
        imgWidth = MAX_WINDOW_WIDTH;
        imgHeight = (int)(imgHeight * scale);
    }
    if (imgHeight > MAX_WINDOW_HEIGHT) {
        float scale = (float)MAX_WINDOW_HEIGHT / imgHeight;
        imgHeight = MAX_WINDOW_HEIGHT;
        imgWidth = (int)(imgWidth * scale);
    }

    // 确保最小尺寸
    if (imgWidth < MIN_WINDOW_WIDTH) imgWidth = MIN_WINDOW_WIDTH;
    if (imgHeight < MIN_WINDOW_HEIGHT) imgHeight = MIN_WINDOW_HEIGHT;

    *width = imgWidth;
    *height = imgHeight;
}

static void DrawFloatWindow(HDC hdc, FloatWindowContext* ctx) {
    if (ctx->image == NULL) {
        return;
    }

    RECT clientRect;
    GetClientRect(ctx->hwnd, &clientRect);

    // 创建内存 DC
    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);

    // 绘制背景
    HBRUSH hBgBrush = CreateSolidBrush(RGB(30, 30, 30));
    FillRect(hdcMem, &clientRect, hBgBrush);
    DeleteObject(hBgBrush);

    // 绘制图像
    HDC hdcImage = CreateCompatibleDC(hdc);
    HBITMAP hImageBitmap = CreateCompatibleBitmap(hdc, ctx->image->width, ctx->image->height);
    HBITMAP hOldImageBitmap = (HBITMAP)SelectObject(hdcImage, hImageBitmap);

    // 设置位图数据
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = ctx->image->width;
    bmi.bmiHeader.biHeight = -ctx->image->height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    SetDIBits(hdcImage, hImageBitmap, 0, ctx->image->height, ctx->image->pixels, &bmi, DIB_RGB_COLORS);

    // 计算绘制位置（居中）
    int imgWidth = ctx->image->width;
    int imgHeight = ctx->image->height;

    // 缩放
    int winWidth = clientRect.right;
    int winHeight = clientRect.bottom;
    float scaleX = (float)winWidth / imgWidth;
    float scaleY = (float)winHeight / imgHeight;
    float scale = scaleX < scaleY ? scaleX : scaleY;

    int drawWidth = (int)(imgWidth * scale);
    int drawHeight = (int)(imgHeight * scale);
    int drawX = (winWidth - drawWidth) / 2;
    int drawY = (winHeight - drawHeight) / 2;

    // 缩放绘制
    SetStretchBltMode(hdcMem, HALFTONE);
    StretchBlt(hdcMem, drawX, drawY, drawWidth, drawHeight,
               hdcImage, 0, 0, imgWidth, imgHeight, SRCCOPY);

    // 清理
    SelectObject(hdcImage, hOldImageBitmap);
    DeleteObject(hImageBitmap);
    DeleteDC(hdcImage);

    // 复制到屏幕
    BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, hdcMem, 0, 0, SRCCOPY);

    // 清理
    SelectObject(hdcMem, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
}

static LRESULT CALLBACK FloatWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            DrawFloatWindow(hdc, &g_float);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_LBUTTONDOWN: {
            g_float.isDragging = true;
            g_float.dragStart.x = GET_X_LPARAM(lParam);
            g_float.dragStart.y = GET_Y_LPARAM(lParam);
            SetCapture(hwnd);
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (g_float.isDragging) {
                POINT pt;
                GetCursorPos(&pt);

                RECT rect;
                GetWindowRect(hwnd, &rect);

                int dx = pt.x - rect.left - g_float.dragStart.x;
                int dy = pt.y - rect.top - g_float.dragStart.y;

                SetWindowPos(hwnd, NULL, rect.left + dx, rect.top + dy, 0, 0,
                            SWP_NOSIZE | SWP_NOZORDER);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (g_float.isDragging) {
                g_float.isDragging = false;
                ReleaseCapture();
            }
            return 0;
        }

        case WM_RBUTTONUP: {
            // 右键菜单
            POINT pt;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
            ClientToScreen(hwnd, &pt);

            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, 1, L"复制");
            AppendMenuW(hMenu, MF_STRING, 2, L"保存");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, g_float.isPinned ? MF_CHECKED : MF_UNCHECKED, 3, L"置顶");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, 4, L"关闭");

            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY,
                                     pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);

            switch (cmd) {
                case 1: // 复制
                    if (g_float.image) {
                        ScreenshotCopyToClipboard(g_float.image);
                        LOG_INFO("[浮动窗口] 已复制到剪贴板");
                    }
                    break;
                case 2: // 保存
                    // TODO: 保存对话框
                    break;
                case 3: // 置顶
                    ScreenshotFloatSetPinned(!g_float.isPinned);
                    break;
                case 4: // 关闭
                    ScreenshotFloatHide();
                    break;
            }
            return 0;
        }

        case WM_DESTROY:
            g_float.hwnd = NULL;
            LOG_DEBUG("[浮动窗口] 窗口已销毁");
            return 0;

        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

int ScreenshotFloatTest(void) {
    LOG_INFO("[浮动窗口测试] 开始测试...");

    if (!ScreenshotFloatInit()) {
        LOG_ERROR("[浮动窗口测试] 初始化失败");
        return 1;
    }

    printf("Capturing screen and showing float window for 5 seconds...\n");
    printf("Right-click for menu, drag to move.\n");

    // 初始化截图模块
    if (!ScreenshotInit()) {
        LOG_ERROR("[浮动窗口测试] 截图初始化失败");
        ScreenshotFloatCleanup();
        return 1;
    }

    // 截取屏幕
    ScreenshotImage* image = ScreenshotCaptureRect(0, 0, 400, 300);
    if (image == NULL) {
        LOG_ERROR("[浮动窗口测试] 截图失败");
        ScreenshotCleanup();
        ScreenshotFloatCleanup();
        return 1;
    }

    // 显示浮动窗口
    if (!ScreenshotFloatShow(image, -1, -1)) {
        LOG_ERROR("[浮动窗口测试] 显示浮动窗口失败");
        ScreenshotImageFree(image);
        ScreenshotCleanup();
        ScreenshotFloatCleanup();
        return 1;
    }

    ScreenshotImageFree(image);

    // 消息循环
    MSG msg;
    DWORD startTime = GetTickCount();
    while (GetTickCount() - startTime < 5000 && ScreenshotFloatIsVisible()) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Sleep(10);
    }

    ScreenshotFloatHide();
    ScreenshotCleanup();
    ScreenshotFloatCleanup();

    LOG_INFO("[浮动窗口测试] 测试完成");
    printf("Test completed.\n");
    return 0;
}
