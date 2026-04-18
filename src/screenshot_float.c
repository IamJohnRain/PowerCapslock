#include "screenshot_float.h"
#include "screenshot.h"
#include "screenshot_ocr.h"
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
#define TOAST_DURATION_MS 2000

// OCR 完成消息
#define WM_FLOAT_OCR_COMPLETE (WM_APP + 10)

// 前向声明
static LRESULT CALLBACK FloatWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static void DrawFloatWindow(HDC hdc, FloatWindowContext* ctx);
static void CalculateWindowSize(FloatWindowContext* ctx, int* width, int* height);
static void ClampWindowToVirtualScreen(int* x, int* y, int width, int height);
static void CalculateImageLayout(FloatWindowContext* ctx, RECT clientRect);
static POINT ImageToWindowPoint(FloatWindowContext* ctx, POINT imagePoint);
static POINT WindowToImagePoint(FloatWindowContext* ctx, POINT windowPoint);
static RECT ImageToWindowRect(FloatWindowContext* ctx, RECT imageRect);
static int FindWordAtImagePoint(FloatWindowContext* ctx, POINT imagePoint);
static void DrawOcrOverlays(HDC hdc, FloatWindowContext* ctx);
static void DrawWordHighlight(HDC hdc, FloatWindowContext* ctx, int wordIndex, COLORREF color, BYTE alpha);
static bool HasTextSelection(FloatWindowContext* ctx);
static void GetSelectedWordRange(FloatWindowContext* ctx, int* start, int* end);
static char* BuildSelectedTextUtf8(FloatWindowContext* ctx);
static bool CopyUtf8TextToClipboard(HWND hwnd, const char* text);

bool ScreenshotFloatInit(void) {
    if (g_initialized) {
        LOG_DEBUG("[浮动窗口] 模块已初始化");
        return true;
    }

    LOG_DEBUG("[浮动窗口] 开始初始化...");

    // 注册窗口类
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
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

    if (g_float.ocrResults != NULL) {
        OCRFreeResults(g_float.ocrResults);
        g_float.ocrResults = NULL;
    }

    memset(&g_float, 0, sizeof(FloatWindowContext));
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
    if (x == -1 && y == -1) {
        // 默认位置：屏幕右下角
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        x = screenWidth - width - 20;
        y = screenHeight - height - 60;
    } else {
        ClampWindowToVirtualScreen(&x, &y, width, height);
    }

    // 创建窗口
    g_float.hwnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        WINDOW_CLASS,
        "ScreenshotFloat",
        WS_POPUP,
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

bool ScreenshotFloatShowOcr(const ScreenshotImage* image, int x, int y) {
    if (!g_initialized) {
        LOG_ERROR("[浮动窗口] 模块未初始化");
        return false;
    }

    if (image == NULL) {
        LOG_ERROR("[浮动窗口] 图像为空");
        return false;
    }

    LOG_DEBUG("[浮动窗口] 显示 OCR 浮动窗口...");

    // 如果已有窗口，先关闭
    if (g_float.hwnd != NULL) {
        DestroyWindow(g_float.hwnd);
        g_float.hwnd = NULL;
    }

    // 释放旧图像和 OCR 结果
    if (g_float.image != NULL) {
        ScreenshotImageFree(g_float.image);
    }
    if (g_float.ocrResults != NULL) {
        OCRFreeResults(g_float.ocrResults);
        g_float.ocrResults = NULL;
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
    if (x == -1 && y == -1) {
        // 默认位置：屏幕右下角
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        x = screenWidth - width - 20;
        y = screenHeight - height - 60;
    } else {
        ClampWindowToVirtualScreen(&x, &y, width, height);
    }

    // 创建窗口
    g_float.hwnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        WINDOW_CLASS,
        "ScreenshotFloat",
        WS_POPUP,
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

    // 设置 OCR 模式（在窗口创建后，因为需要 hwnd）
    g_float.ocrMode = true;
    g_float.ocrInProgress = true;
    g_float.ocrSessionId++;
    g_float.hoveredWordIndex = -1;
    g_float.anchorWordIndex = -1;
    g_float.activeWordIndex = -1;
    g_float.isSelectingText = false;
    g_float.showCopiedToast = false;

    // 启动异步 OCR 识别
    LOG_DEBUG("[浮动窗口] 开始异步 OCR 识别, sessionId=%lu", g_float.ocrSessionId);
    if (!OCRRecognizeAsync(image, g_float.hwnd, WM_FLOAT_OCR_COMPLETE)) {
        LOG_ERROR("[浮动窗口] 启动异步 OCR 失败");
        g_float.ocrInProgress = false;
        DestroyWindow(g_float.hwnd);
        g_float.hwnd = NULL;
        ScreenshotImageFree(g_float.image);
        g_float.image = NULL;
        g_float.ocrMode = false;
        return false;
    }

    // 显示窗口
    ShowWindow(g_float.hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(g_float.hwnd);

    LOG_INFO("[浮动窗口] OCR 窗口已显示: (%d, %d) %dx%d, 等待 OCR 完成...", x, y, width, height);
    return true;
}

const char* ScreenshotFloatGetSelectedText(void) {
    static char* selectedText = NULL;

    if (selectedText != NULL) {
        free(selectedText);
        selectedText = NULL;
    }

    if (!g_float.ocrMode || !HasTextSelection(&g_float)) {
        return NULL;
    }

    selectedText = BuildSelectedTextUtf8(&g_float);
    return selectedText;
}

void ScreenshotFloatClearSelection(void) {
    g_float.anchorWordIndex = -1;
    g_float.activeWordIndex = -1;
    g_float.isSelectingText = false;
}

void ScreenshotFloatHide(void) {
    if (g_float.hwnd == NULL) {
        return;
    }

    LOG_DEBUG("[浮动窗口] 隐藏浮动窗口");

    // 取消异步 OCR
    if (g_float.ocrInProgress) {
        OCRCancelAsync();
        // 使 session 无效，这样后续到来的 OCR 结果会被忽略
        g_float.ocrSessionId++;
    }

    DestroyWindow(g_float.hwnd);
    g_float.hwnd = NULL;

    if (g_float.image != NULL) {
        ScreenshotImageFree(g_float.image);
        g_float.image = NULL;
    }

    if (g_float.ocrResults != NULL) {
        OCRFreeResults(g_float.ocrResults);
        g_float.ocrResults = NULL;
    }

    // 重置 OCR 状态
    g_float.ocrMode = false;
    g_float.ocrInProgress = false;
    g_float.hoveredWordIndex = -1;
    g_float.anchorWordIndex = -1;
    g_float.activeWordIndex = -1;
    g_float.isSelectingText = false;

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

    if (imgWidth <= 0) imgWidth = MIN_WINDOW_WIDTH;
    if (imgHeight <= 0) imgHeight = MIN_WINDOW_HEIGHT;

    // OCR 模式可能需要更大的窗口来容纳文字选择 UI
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

    *width = imgWidth;
    *height = imgHeight;
}

static void ClampWindowToVirtualScreen(int* x, int* y, int width, int height) {
    int virtualLeft;
    int virtualTop;
    int virtualRight;
    int virtualBottom;

    if (x == NULL || y == NULL) {
        return;
    }

    virtualLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
    virtualTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
    virtualRight = virtualLeft + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    virtualBottom = virtualTop + GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (*x + width > virtualRight) {
        *x = virtualRight - width;
    }
    if (*y + height > virtualBottom) {
        *y = virtualBottom - height;
    }
    if (*x < virtualLeft) {
        *x = virtualLeft;
    }
    if (*y < virtualTop) {
        *y = virtualTop;
    }
}

static void CalculateImageLayout(FloatWindowContext* ctx, RECT clientRect) {
    if (ctx->image == NULL) {
        ctx->imageScale = 1.0f;
        ctx->imageOffset.x = 0;
        ctx->imageOffset.y = 0;
        ctx->imageDrawSize.cx = 0;
        ctx->imageDrawSize.cy = 0;
        return;
    }

    int imgWidth = ctx->image->width;
    int imgHeight = ctx->image->height;
    int winWidth = clientRect.right;
    int winHeight = clientRect.bottom;

    // 计算缩放比例
    float scaleX = (float)winWidth / imgWidth;
    float scaleY = (float)winHeight / imgHeight;
    ctx->imageScale = (scaleX < scaleY) ? scaleX : scaleY;

    // 计算绘制尺寸
    ctx->imageDrawSize.cx = (int)(imgWidth * ctx->imageScale);
    ctx->imageDrawSize.cy = (int)(imgHeight * ctx->imageScale);

    // 计算居中偏移
    ctx->imageOffset.x = (winWidth - ctx->imageDrawSize.cx) / 2;
    ctx->imageOffset.y = (winHeight - ctx->imageDrawSize.cy) / 2;
}

static POINT ImageToWindowPoint(FloatWindowContext* ctx, POINT imagePoint) {
    POINT wp;
    wp.x = (int)(imagePoint.x * ctx->imageScale + ctx->imageOffset.x);
    wp.y = (int)(imagePoint.y * ctx->imageScale + ctx->imageOffset.y);
    return wp;
}

static POINT WindowToImagePoint(FloatWindowContext* ctx, POINT windowPoint) {
    POINT ip;
    ip.x = (int)((windowPoint.x - ctx->imageOffset.x) / ctx->imageScale);
    ip.y = (int)((windowPoint.y - ctx->imageOffset.y) / ctx->imageScale);
    return ip;
}

static RECT ImageToWindowRect(FloatWindowContext* ctx, RECT imageRect) {
    RECT wr;
    wr.left = (int)(imageRect.left * ctx->imageScale + ctx->imageOffset.x);
    wr.top = (int)(imageRect.top * ctx->imageScale + ctx->imageOffset.y);
    wr.right = (int)(imageRect.right * ctx->imageScale + ctx->imageOffset.x);
    wr.bottom = (int)(imageRect.bottom * ctx->imageScale + ctx->imageOffset.y);
    return wr;
}

static int FindWordAtImagePoint(FloatWindowContext* ctx, POINT imagePoint) {
    if (ctx->ocrResults == NULL || ctx->ocrResults->words == NULL) {
        return -1;
    }

    int i;
    for (i = 0; i < ctx->ocrResults->wordCount; i++) {
        RECT bbox = ctx->ocrResults->words[i].boundingBox;
        if (imagePoint.x >= bbox.left && imagePoint.x <= bbox.right &&
            imagePoint.y >= bbox.top && imagePoint.y <= bbox.bottom) {
            return i;
        }
    }
    return -1;
}

static void DrawFloatWindow(HDC hdc, FloatWindowContext* ctx) {
    if (ctx->image == NULL) {
        return;
    }

    RECT clientRect;
    GetClientRect(ctx->hwnd, &clientRect);

    // 计算统一布局
    CalculateImageLayout(ctx, clientRect);

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

    // 使用统一布局绘制
    SetStretchBltMode(hdcMem, HALFTONE);
    StretchBlt(hdcMem, ctx->imageOffset.x, ctx->imageOffset.y,
               ctx->imageDrawSize.cx, ctx->imageDrawSize.cy,
               hdcImage, 0, 0, ctx->image->width, ctx->image->height, SRCCOPY);

    // 清理
    SelectObject(hdcImage, hOldImageBitmap);
    DeleteObject(hImageBitmap);
    DeleteDC(hdcImage);

    // OCR 模式绘制
    if (ctx->ocrMode) {
        DrawOcrOverlays(hdcMem, ctx);
    }

    // 复制到屏幕
    BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, hdcMem, 0, 0, SRCCOPY);

    // 清理
    SelectObject(hdcMem, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
}

static void DrawOcrOverlays(HDC hdc, FloatWindowContext* ctx) {
    if (ctx->ocrResults == NULL || ctx->ocrResults->words == NULL) {
        return;
    }

    int selStart = -1, selEnd = -1;

    // 绘制选择高亮
    if (HasTextSelection(ctx)) {
        GetSelectedWordRange(ctx, &selStart, &selEnd);
        int i;
        for (i = selStart; i <= selEnd; i++) {
            if (i >= 0 && i < ctx->ocrResults->wordCount) {
                DrawWordHighlight(hdc, ctx, i, RGB(100, 149, 237), 150); // 蓝色高亮
            }
        }
    }

    // 绘制 hover 高亮
    if (ctx->hoveredWordIndex >= 0 && ctx->hoveredWordIndex < ctx->ocrResults->wordCount) {
        if (!HasTextSelection(ctx) || ctx->hoveredWordIndex < selStart || ctx->hoveredWordIndex > selEnd) {
            DrawWordHighlight(hdc, ctx, ctx->hoveredWordIndex, RGB(255, 235, 100), 100); // 黄色高亮
        }
    }
}

static void DrawWordHighlight(HDC hdc, FloatWindowContext* ctx, int wordIndex, COLORREF color, BYTE alpha) {
    if (wordIndex < 0 || wordIndex >= ctx->ocrResults->wordCount) {
        return;
    }

    RECT imageRect = ctx->ocrResults->words[wordIndex].boundingBox;
    RECT winRect = ImageToWindowRect(ctx, imageRect);

    // 创建半透明画刷
    HBRUSH hBrush = CreateSolidBrush(color);
    HPEN hPen = CreatePen(PS_SOLID, 1, color);

    // 绘制填充矩形
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

    // 简单使用不透明填充（Windows GDI 不直接支持半透明矩形）
    Rectangle(hdc, winRect.left, winRect.top, winRect.right, winRect.bottom);

    SelectObject(hdc, hOldBrush);
    SelectObject(hdc, hOldPen);
    DeleteObject(hBrush);
    DeleteObject(hPen);
}

static bool HasTextSelection(FloatWindowContext* ctx) {
    return ctx->anchorWordIndex >= 0 && ctx->activeWordIndex >= 0;
}

static void GetSelectedWordRange(FloatWindowContext* ctx, int* start, int* end) {
    if (start == NULL || end == NULL) {
        return;
    }
    *start = ctx->anchorWordIndex < ctx->activeWordIndex ? ctx->anchorWordIndex : ctx->activeWordIndex;
    *end = ctx->anchorWordIndex < ctx->activeWordIndex ? ctx->activeWordIndex : ctx->anchorWordIndex;
}

static char* BuildSelectedTextUtf8(FloatWindowContext* ctx) {
    if (!HasTextSelection(ctx) || ctx->ocrResults == NULL) {
        return NULL;
    }

    int start, end;
    GetSelectedWordRange(ctx, &start, &end);

    // 计算所需内存
    size_t totalLen = 0;
    int i;
    for (i = start; i <= end; i++) {
        if (i >= 0 && i < ctx->ocrResults->wordCount) {
            totalLen += strlen(ctx->ocrResults->words[i].text);
            if (ctx->ocrResults->words[i].isLineBreak) {
                totalLen += 2; /* \r\n */
            } else if (i < end) {
                totalLen += 1; /* space */
            }
        }
    }

    char* result = (char*)malloc(totalLen + 1);
    if (result == NULL) {
        return NULL;
    }

    char* p = result;
    for (i = start; i <= end; i++) {
        if (i >= 0 && i < ctx->ocrResults->wordCount) {
            OCRWord* word = &ctx->ocrResults->words[i];
            strcpy(p, word->text);
            p += strlen(word->text);
            if (word->isLineBreak) {
                *p++ = '\r';
                *p++ = '\n';
            } else if (i < end) {
                *p++ = ' ';
            }
        }
    }
    *p = '\0';

    return result;
}

static bool CopyUtf8TextToClipboard(HWND hwnd, const char* text) {
    if (text == NULL) {
        return false;
    }

    // 计算宽字符长度
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (wideLen == 0) {
        return false;
    }

    wchar_t* wideText = (wchar_t*)malloc(wideLen * sizeof(wchar_t));
    if (wideText == NULL) {
        return false;
    }

    MultiByteToWideChar(CP_UTF8, 0, text, -1, wideText, wideLen);

    // 打开剪贴板
    if (!OpenClipboard(hwnd)) {
        free(wideText);
        return false;
    }

    EmptyClipboard();

    // 分配全局内存
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, wideLen * sizeof(wchar_t));
    if (hMem == NULL) {
        CloseClipboard();
        free(wideText);
        return false;
    }

    wchar_t* pMem = (wchar_t*)GlobalLock(hMem);
    memcpy(pMem, wideText, wideLen * sizeof(wchar_t));
    GlobalUnlock(hMem);

    SetClipboardData(CF_UNICODETEXT, hMem);

    CloseClipboard();
    free(wideText);

    return true;
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

        case WM_SETCURSOR: {
            if (g_float.ocrMode && g_float.hoveredWordIndex >= 0) {
                SetCursor(LoadCursor(NULL, IDC_IBEAM));
                return TRUE;
            }
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            return TRUE;
        }

        case WM_LBUTTONDOWN: {
            POINT pt;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);

            // OCR 模式下尝试选择文字
            if (g_float.ocrMode && g_float.ocrResults != NULL) {
                POINT imagePt = WindowToImagePoint(&g_float, pt);
                int wordIdx = FindWordAtImagePoint(&g_float, imagePt);

                if (wordIdx >= 0) {
                    // 开始文字选择
                    g_float.isSelectingText = true;
                    g_float.isDragging = false;
                    g_float.anchorWordIndex = wordIdx;
                    g_float.activeWordIndex = wordIdx;
                    SetCapture(hwnd);
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
            }

            // 否则开始窗口拖动
            g_float.isDragging = true;
            g_float.dragStart.x = pt.x;
            g_float.dragStart.y = pt.y;
            SetCapture(hwnd);
            return 0;
        }

        case WM_LBUTTONDBLCLK:
            LOG_INFO("[浮动窗口] 双击关闭");
            ScreenshotFloatHide();
            return 0;

        case WM_MOUSEMOVE: {
            POINT pt;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);

            if (g_float.isSelectingText) {
                // 更新文字选择
                POINT imagePt = WindowToImagePoint(&g_float, pt);
                int wordIdx = FindWordAtImagePoint(&g_float, imagePt);
                if (wordIdx != g_float.activeWordIndex) {
                    g_float.activeWordIndex = wordIdx;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            } else if (g_float.isDragging) {
                POINT screenPt;
                GetCursorPos(&screenPt);

                RECT rect;
                GetWindowRect(hwnd, &rect);

                int dx = screenPt.x - rect.left - g_float.dragStart.x;
                int dy = screenPt.y - rect.top - g_float.dragStart.y;

                SetWindowPos(hwnd, NULL, rect.left + dx, rect.top + dy, 0, 0,
                            SWP_NOSIZE | SWP_NOZORDER);
            }

            // 更新 hover 状态
            if (g_float.ocrMode) {
                POINT imagePt = WindowToImagePoint(&g_float, pt);
                int wordIdx = FindWordAtImagePoint(&g_float, imagePt);
                if (wordIdx != g_float.hoveredWordIndex) {
                    g_float.hoveredWordIndex = wordIdx;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (g_float.isSelectingText) {
                g_float.isSelectingText = false;
                ReleaseCapture();
            }
            if (g_float.isDragging) {
                g_float.isDragging = false;
                ReleaseCapture();
            }
            return 0;
        }

        case WM_KEYDOWN: {
            if (g_float.ocrMode && wParam == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                // Ctrl+C 复制选中的文字
                const char* selectedText = ScreenshotFloatGetSelectedText();
                if (selectedText != NULL) {
                    if (CopyUtf8TextToClipboard(hwnd, selectedText)) {
                        LOG_INFO("[浮动窗口] 已复制文字到剪贴板");
                        g_float.showCopiedToast = true;
                        g_float.toastStartTime = GetTickCount();
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                }
                return 0;
            }
            return DefWindowProcA(hwnd, msg, wParam, lParam);
        }

        case WM_RBUTTONUP: {
            // 右键菜单
            POINT pt;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
            ClientToScreen(hwnd, &pt);

            HMENU hMenu = CreatePopupMenu();

            if (g_float.ocrMode && HasTextSelection(&g_float)) {
                AppendMenuW(hMenu, MF_STRING, 1, L"复制文字");
                AppendMenuW(hMenu, MF_STRING, 2, L"复制图片");
            } else {
                AppendMenuW(hMenu, MF_STRING, 1, L"复制");
            }
            AppendMenuW(hMenu, MF_STRING, 2, L"保存");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, g_float.isPinned ? MF_CHECKED : MF_UNCHECKED, 3, L"置顶");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, 4, L"关闭");

            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY,
                                     pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);

            switch (cmd) {
                case 1: // 复制文字或复制
                    if (g_float.ocrMode && HasTextSelection(&g_float)) {
                        const char* selectedText = ScreenshotFloatGetSelectedText();
                        if (selectedText != NULL && CopyUtf8TextToClipboard(hwnd, selectedText)) {
                            LOG_INFO("[浮动窗口] 已复制文字到剪贴板");
                            g_float.showCopiedToast = true;
                            g_float.toastStartTime = GetTickCount();
                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                    } else if (g_float.image) {
                        ScreenshotCopyToClipboard(g_float.image);
                        LOG_INFO("[浮动窗口] 已复制到剪贴板");
                    }
                    break;
                case 2: // 保存或复制图片
                    if (g_float.ocrMode && HasTextSelection(&g_float)) {
                        // 复制图片
                        if (g_float.image) {
                            ScreenshotCopyToClipboard(g_float.image);
                            LOG_INFO("[浮动窗口] 已复制图片到剪贴板");
                        }
                    }
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

        case WM_FLOAT_OCR_COMPLETE: {
            // 异步 OCR 完成回调
            OcrAsyncMessage* msgData = (OcrAsyncMessage*)lParam;
            if (msgData == NULL) {
                return 0;
            }

            // 检查 session 是否仍然有效
            if (msgData->sessionId != g_float.ocrSessionId) {
                LOG_DEBUG("[浮动窗口] OCR session 不匹配, 忽略结果");
                OCRFreeResults(msgData->results);
                free(msgData);
                return 0;
            }

            // 检查窗口是否仍然有效
            if (!IsWindow(g_float.hwnd)) {
                LOG_DEBUG("[浮动窗口] 窗口已关闭, 忽略 OCR 结果");
                OCRFreeResults(msgData->results);
                free(msgData);
                return 0;
            }

            // 更新 OCR 结果
            if (g_float.ocrResults != NULL) {
                OCRFreeResults(g_float.ocrResults);
            }
            g_float.ocrResults = msgData->results;
            g_float.ocrInProgress = false;

            LOG_INFO("[浮动窗口] 异步 OCR 完成, wordCount=%d", g_float.ocrResults->wordCount);

            free(msgData);

            // 重绘窗口
            InvalidateRect(g_float.hwnd, NULL, FALSE);
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
