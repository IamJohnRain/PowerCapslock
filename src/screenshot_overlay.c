#include "screenshot_overlay.h"
#include "screenshot.h"
#include "screenshot_manager.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
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
static void DrawAnnotations(HDC hdc, OverlayContext* ctx);
static void DrawAnnotation(HDC hdc, const OverlayAnnotation* annotation, int offsetX, int offsetY);
static void DrawArrow(HDC hdc, POINT start, POINT end);
static bool PointInSelection(OverlayContext* ctx, POINT pt);
static void ResetAnnotations(OverlayContext* ctx);
static void StartAnnotation(OverlayContext* ctx, POINT pt);
static void UpdateAnnotation(OverlayContext* ctx, POINT pt);
static void FinishAnnotation(OverlayContext* ctx, POINT pt);
static void CancelTextEdit(OverlayContext* ctx);
static void CommitTextEdit(OverlayContext* ctx);
static void ApplyAnnotationsToImage(ScreenshotImage* image, const ScreenshotRect* selection);
static HCURSOR CreateSelectionCursor(void);
static HWND GetWindowFromPointEx(POINT pt);
static void CaptureScreenToContext(OverlayContext* ctx);

static HCURSOR CreateSelectionCursor(void) {
    enum { CURSOR_SIZE = 24, CURSOR_CORE_THICKNESS = 3, CURSOR_OUTLINE_THICKNESS = 5, CURSOR_MARGIN = 1 };
    enum { MASK_ROW_BYTES = ((CURSOR_SIZE + 15) / 16) * 2 };
    BITMAPINFO bmi;
    void* bits = NULL;
    HDC hdc;
    HBITMAP colorBitmap;
    HBITMAP maskBitmap;
    ICONINFO iconInfo;
    HCURSOR cursor;
    BYTE maskBits[MASK_ROW_BYTES * CURSOR_SIZE];
    DWORD* pixels;
    int center = CURSOR_SIZE / 2;
    int halfCoreThickness = CURSOR_CORE_THICKNESS / 2;
    int halfOutlineThickness = CURSOR_OUTLINE_THICKNESS / 2;

    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = CURSOR_SIZE;
    bmi.bmiHeader.biHeight = -CURSOR_SIZE;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    hdc = GetDC(NULL);
    if (hdc == NULL) {
        return NULL;
    }

    colorBitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, hdc);
    if (colorBitmap == NULL || bits == NULL) {
        return NULL;
    }

    pixels = (DWORD*)bits;
    ZeroMemory(pixels, CURSOR_SIZE * CURSOR_SIZE * sizeof(DWORD));
    memset(maskBits, 0xFF, sizeof(maskBits));

    for (int y = CURSOR_MARGIN; y < CURSOR_SIZE - CURSOR_MARGIN; y++) {
        for (int x = CURSOR_MARGIN; x < CURSOR_SIZE - CURSOR_MARGIN; x++) {
            bool onOutlineVertical = abs(x - center) <= halfOutlineThickness;
            bool onOutlineHorizontal = abs(y - center) <= halfOutlineThickness;
            bool onCoreVertical = abs(x - center) <= halfCoreThickness;
            bool onCoreHorizontal = abs(y - center) <= halfCoreThickness;
            if (onOutlineVertical || onOutlineHorizontal) {
                pixels[y * CURSOR_SIZE + x] =
                    (onCoreVertical || onCoreHorizontal) ? 0xFF000000 : 0xFFFFFFFF;
                maskBits[y * MASK_ROW_BYTES + x / 8] &= (BYTE)~(0x80 >> (x % 8));
            }
        }
    }

    maskBitmap = CreateBitmap(CURSOR_SIZE, CURSOR_SIZE, 1, 1, maskBits);
    if (maskBitmap == NULL) {
        DeleteObject(colorBitmap);
        return NULL;
    }

    ZeroMemory(&iconInfo, sizeof(iconInfo));
    iconInfo.fIcon = FALSE;
    iconInfo.xHotspot = center;
    iconInfo.yHotspot = center;
    iconInfo.hbmMask = maskBitmap;
    iconInfo.hbmColor = colorBitmap;

    cursor = CreateIconIndirect(&iconInfo);
    DeleteObject(maskBitmap);
    DeleteObject(colorBitmap);
    return cursor;
}

bool ScreenshotOverlayInit(void) {
    if (g_initialized) {
        LOG_DEBUG("[选区窗口] 模块已初始化");
        return true;
    }

    LOG_DEBUG("[选区窗口] 开始初始化...");

    if (g_overlay.crossCursor == NULL) {
        g_overlay.crossCursor = CreateSelectionCursor();
    }
    if (g_overlay.crossCursor == NULL) {
        g_overlay.crossCursor = LoadCursor(NULL, IDC_CROSS);
    }

    // 注册窗口类
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = g_overlay.crossCursor;
    wc.hbrBackground = NULL;
    wc.lpszClassName = WINDOW_CLASS;

    if (!RegisterClassExA(&wc)) {
        DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            LOG_ERROR("[选区窗口] 注册窗口类失败: %d", error);
            return false;
        }
    }

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
    ResetAnnotations(&g_overlay);

    // 显示窗口
    ShowWindow(g_overlay.hwnd, SW_SHOWNA);
    UpdateWindow(g_overlay.hwnd);

    // 设置焦点到窗口（用于接收键盘事件）
    SetForegroundWindow(g_overlay.hwnd);
    SetFocus(g_overlay.hwnd);
    SetCursor(g_overlay.crossCursor);

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
    ResetAnnotations(&g_overlay);
    ScreenshotManagerOnOverlayCancelled();

    LOG_INFO("[选区窗口] 窗口已隐藏");
}

bool ScreenshotOverlayIsActive(void) {
    return g_overlay.isActive;
}

HWND ScreenshotOverlayGetWindow(void) {
    return g_overlay.hwnd;
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

ScreenshotImage* ScreenshotOverlayGetAnnotatedSelectionImage(void) {
    ScreenshotImage* image = ScreenshotOverlayGetSelectionImage();
    if (image == NULL) {
        return NULL;
    }

    if (g_overlay.annotationCount > 0 || g_overlay.isEditingText) {
        ApplyAnnotationsToImage(image, &g_overlay.selection);
    }

    return image;
}

void ScreenshotOverlaySetAnnotationTool(OverlayAnnotateTool tool) {
    if (tool < OVERLAY_ANNOTATE_NONE || tool > OVERLAY_ANNOTATE_TEXT) {
        tool = OVERLAY_ANNOTATE_NONE;
    }

    g_overlay.annotateTool = tool;
    g_overlay.isDrawingAnnotation = false;
    CancelTextEdit(&g_overlay);

    if (g_overlay.hwnd != NULL) {
        SetCursor(g_overlay.crossCursor);
        InvalidateRect(g_overlay.hwnd, NULL, FALSE);
    }

    LOG_INFO("[选区窗口] 标注工具: %d", tool);
}

OverlayAnnotateTool ScreenshotOverlayGetAnnotationTool(void) {
    return g_overlay.annotateTool;
}

void ScreenshotOverlayClearAnnotations(void) {
    g_overlay.annotationCount = 0;
    g_overlay.isDrawingAnnotation = false;
    CancelTextEdit(&g_overlay);
    if (g_overlay.hwnd != NULL) {
        InvalidateRect(g_overlay.hwnd, NULL, FALSE);
    }
}

bool ScreenshotOverlayHasAnnotations(void) {
    return g_overlay.annotationCount > 0 || (g_overlay.isEditingText && g_overlay.editingTextLen > 0);
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

static bool PointInSelection(OverlayContext* ctx, POINT pt) {
    return ctx != NULL &&
           pt.x >= ctx->selection.x &&
           pt.x <= ctx->selection.x + ctx->selection.width &&
           pt.y >= ctx->selection.y &&
           pt.y <= ctx->selection.y + ctx->selection.height;
}

static void ResetAnnotations(OverlayContext* ctx) {
    if (ctx == NULL) {
        return;
    }
    ctx->annotateTool = OVERLAY_ANNOTATE_NONE;
    ctx->annotationCount = 0;
    ctx->isDrawingAnnotation = false;
    ctx->isEditingText = false;
    ctx->editingTextLen = 0;
    ctx->editingText[0] = L'\0';
    ZeroMemory(&ctx->currentAnnotation, sizeof(ctx->currentAnnotation));
}

static void StartAnnotation(OverlayContext* ctx, POINT pt) {
    if (ctx == NULL || ctx->annotateTool == OVERLAY_ANNOTATE_NONE || !PointInSelection(ctx, pt)) {
        return;
    }

    CancelTextEdit(ctx);
    ZeroMemory(&ctx->currentAnnotation, sizeof(ctx->currentAnnotation));
    ctx->currentAnnotation.tool = ctx->annotateTool;
    ctx->currentAnnotation.startPoint = pt;
    ctx->currentAnnotation.endPoint = pt;

    if (ctx->annotateTool == OVERLAY_ANNOTATE_TEXT) {
        ctx->isEditingText = true;
        ctx->textAnchor = pt;
        ctx->editingTextLen = 0;
        ctx->editingText[0] = L'\0';
        SetFocus(ctx->hwnd);
        InvalidateRect(ctx->hwnd, NULL, FALSE);
        return;
    }

    ctx->isDrawingAnnotation = true;
    if (ctx->annotateTool == OVERLAY_ANNOTATE_PENCIL) {
        ctx->currentAnnotation.points[0] = pt;
        ctx->currentAnnotation.pointCount = 1;
    }
    SetCapture(ctx->hwnd);
    InvalidateRect(ctx->hwnd, NULL, FALSE);
}

static void UpdateAnnotation(OverlayContext* ctx, POINT pt) {
    if (ctx == NULL || !ctx->isDrawingAnnotation) {
        return;
    }

    if (!PointInSelection(ctx, pt)) {
        if (pt.x < ctx->selection.x) pt.x = ctx->selection.x;
        if (pt.y < ctx->selection.y) pt.y = ctx->selection.y;
        if (pt.x > ctx->selection.x + ctx->selection.width) pt.x = ctx->selection.x + ctx->selection.width;
        if (pt.y > ctx->selection.y + ctx->selection.height) pt.y = ctx->selection.y + ctx->selection.height;
    }

    ctx->currentAnnotation.endPoint = pt;
    if (ctx->currentAnnotation.tool == OVERLAY_ANNOTATE_PENCIL &&
        ctx->currentAnnotation.pointCount < OVERLAY_MAX_PENCIL_POINTS) {
        POINT last = ctx->currentAnnotation.points[ctx->currentAnnotation.pointCount - 1];
        if (abs(last.x - pt.x) + abs(last.y - pt.y) >= 2) {
            ctx->currentAnnotation.points[ctx->currentAnnotation.pointCount++] = pt;
        }
    }
    InvalidateRect(ctx->hwnd, NULL, FALSE);
}

static void FinishAnnotation(OverlayContext* ctx, POINT pt) {
    int dx;
    int dy;

    if (ctx == NULL || !ctx->isDrawingAnnotation) {
        return;
    }

    UpdateAnnotation(ctx, pt);
    ctx->isDrawingAnnotation = false;
    ReleaseCapture();

    dx = abs(ctx->currentAnnotation.endPoint.x - ctx->currentAnnotation.startPoint.x);
    dy = abs(ctx->currentAnnotation.endPoint.y - ctx->currentAnnotation.startPoint.y);

    if ((dx > 2 || dy > 2 || ctx->currentAnnotation.tool == OVERLAY_ANNOTATE_PENCIL) &&
        ctx->annotationCount < OVERLAY_MAX_ANNOTATIONS) {
        ctx->annotations[ctx->annotationCount++] = ctx->currentAnnotation;
        LOG_DEBUG("[选区窗口] 添加标注: tool=%d count=%d", ctx->currentAnnotation.tool, ctx->annotationCount);
    }

    ZeroMemory(&ctx->currentAnnotation, sizeof(ctx->currentAnnotation));
    InvalidateRect(ctx->hwnd, NULL, FALSE);
}

static void CancelTextEdit(OverlayContext* ctx) {
    if (ctx == NULL) {
        return;
    }
    ctx->isEditingText = false;
    ctx->editingTextLen = 0;
    ctx->editingText[0] = L'\0';
}

static void CommitTextEdit(OverlayContext* ctx) {
    OverlayAnnotation annotation;

    if (ctx == NULL || !ctx->isEditingText) {
        return;
    }

    if (ctx->editingTextLen > 0 && ctx->annotationCount < OVERLAY_MAX_ANNOTATIONS) {
        ZeroMemory(&annotation, sizeof(annotation));
        annotation.tool = OVERLAY_ANNOTATE_TEXT;
        annotation.startPoint = ctx->textAnchor;
        annotation.endPoint = ctx->textAnchor;
        wcsncpy(annotation.text, ctx->editingText, OVERLAY_TEXT_MAX - 1);
        annotation.text[OVERLAY_TEXT_MAX - 1] = L'\0';
        ctx->annotations[ctx->annotationCount++] = annotation;
        LOG_DEBUG("[选区窗口] 添加文字标注");
    }

    CancelTextEdit(ctx);
    if (ctx->hwnd != NULL) {
        InvalidateRect(ctx->hwnd, NULL, FALSE);
    }
}

static void DrawOverlay(HDC hdc, OverlayContext* ctx) {
    RECT clientRect;
    GetClientRect(ctx->hwnd, &clientRect);
    bool hasSelection = (ctx->selection.width > 0 && ctx->selection.height > 0 &&
                         (ctx->isSelecting ||
                          ctx->state == OVERLAY_STATE_SELECTED ||
                          ctx->state == OVERLAY_STATE_TOOLBAR));

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
    if (hasSelection) {
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

    if (hasSelection) {
        DrawAnnotations(hdcMem, ctx);
    }

    // 绘制选区
    if (hasSelection) {
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

static void DrawAnnotations(HDC hdc, OverlayContext* ctx) {
    int saved;
    RECT selRect;

    if (hdc == NULL || ctx == NULL || ctx->selection.width <= 0 || ctx->selection.height <= 0) {
        return;
    }

    saved = SaveDC(hdc);
    selRect.left = ctx->selection.x;
    selRect.top = ctx->selection.y;
    selRect.right = ctx->selection.x + ctx->selection.width;
    selRect.bottom = ctx->selection.y + ctx->selection.height;
    IntersectClipRect(hdc, selRect.left, selRect.top, selRect.right, selRect.bottom);

    for (int i = 0; i < ctx->annotationCount; i++) {
        DrawAnnotation(hdc, &ctx->annotations[i], 0, 0);
    }

    if (ctx->isDrawingAnnotation) {
        DrawAnnotation(hdc, &ctx->currentAnnotation, 0, 0);
    }

    if (ctx->isEditingText) {
        OverlayAnnotation textPreview;
        ZeroMemory(&textPreview, sizeof(textPreview));
        textPreview.tool = OVERLAY_ANNOTATE_TEXT;
        textPreview.startPoint = ctx->textAnchor;
        if (ctx->editingTextLen > 0) {
            wcsncpy(textPreview.text, ctx->editingText, OVERLAY_TEXT_MAX - 1);
        } else {
            wcsncpy(textPreview.text, L"_", OVERLAY_TEXT_MAX - 1);
        }
        DrawAnnotation(hdc, &textPreview, 0, 0);
    }

    RestoreDC(hdc, saved);
}

static void DrawAnnotation(HDC hdc, const OverlayAnnotation* annotation, int offsetX, int offsetY) {
    HPEN pen;
    HPEN oldPen;
    HBRUSH oldBrush;
    HFONT font;
    HFONT oldFont;
    POINT start;
    POINT end;
    RECT rect;

    if (hdc == NULL || annotation == NULL || annotation->tool == OVERLAY_ANNOTATE_NONE) {
        return;
    }

    start.x = annotation->startPoint.x - offsetX;
    start.y = annotation->startPoint.y - offsetY;
    end.x = annotation->endPoint.x - offsetX;
    end.y = annotation->endPoint.y - offsetY;

    pen = CreatePen(PS_SOLID, 3, RGB(255, 64, 64));
    oldPen = (HPEN)SelectObject(hdc, pen);
    oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 64, 64));

    switch (annotation->tool) {
        case OVERLAY_ANNOTATE_RECT:
            Rectangle(hdc, start.x, start.y, end.x, end.y);
            break;
        case OVERLAY_ANNOTATE_CIRCLE:
            Ellipse(hdc, start.x, start.y, end.x, end.y);
            break;
        case OVERLAY_ANNOTATE_ARROW:
            DrawArrow(hdc, start, end);
            break;
        case OVERLAY_ANNOTATE_PENCIL:
            if (annotation->pointCount > 1) {
                MoveToEx(hdc, annotation->points[0].x - offsetX, annotation->points[0].y - offsetY, NULL);
                for (int i = 1; i < annotation->pointCount; i++) {
                    LineTo(hdc, annotation->points[i].x - offsetX, annotation->points[i].y - offsetY);
                }
            }
            break;
        case OVERLAY_ANNOTATE_TEXT:
            font = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
            oldFont = (HFONT)SelectObject(hdc, font);
            rect.left = start.x;
            rect.top = start.y;
            rect.right = start.x + 400;
            rect.bottom = start.y + 40;
            DrawTextW(hdc, annotation->text, -1, &rect, DT_LEFT | DT_TOP | DT_SINGLELINE);
            SelectObject(hdc, oldFont);
            DeleteObject(font);
            break;
        default:
            break;
    }

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

static void DrawArrow(HDC hdc, POINT start, POINT end) {
    int dx = end.x - start.x;
    int dy = end.y - start.y;
    int len = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
    POINT head[3];
    int baseX;
    int baseY;
    int perpX;
    int perpY;

    if (len < 1 || hdc == NULL) {
        return;
    }

    baseX = end.x - dx * 14 / len;
    baseY = end.y - dy * 14 / len;
    perpX = -dy * 6 / len;
    perpY = dx * 6 / len;

    head[0] = end;
    head[1].x = baseX + perpX;
    head[1].y = baseY + perpY;
    head[2].x = baseX - perpX;
    head[2].y = baseY - perpY;

    MoveToEx(hdc, start.x, start.y, NULL);
    LineTo(hdc, end.x, end.y);
    Polygon(hdc, head, 3);
}

static void ApplyAnnotationsToImage(ScreenshotImage* image, const ScreenshotRect* selection) {
    HDC hdcScreen;
    HDC hdcMem;
    HBITMAP oldBitmap;
    BITMAPINFO bmi;

    if (image == NULL || image->hBitmap == NULL || image->pixels == NULL || selection == NULL) {
        return;
    }

    hdcScreen = GetDC(NULL);
    hdcMem = CreateCompatibleDC(hdcScreen);
    ReleaseDC(NULL, hdcScreen);
    if (hdcMem == NULL) {
        return;
    }

    oldBitmap = (HBITMAP)SelectObject(hdcMem, image->hBitmap);

    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = image->width;
    bmi.bmiHeader.biHeight = -image->height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    SetDIBits(hdcMem, image->hBitmap, 0, image->height, image->pixels, &bmi, DIB_RGB_COLORS);

    for (int i = 0; i < g_overlay.annotationCount; i++) {
        DrawAnnotation(hdcMem, &g_overlay.annotations[i], selection->x, selection->y);
    }

    if (g_overlay.isEditingText && g_overlay.editingTextLen > 0) {
        OverlayAnnotation textAnnotation;
        ZeroMemory(&textAnnotation, sizeof(textAnnotation));
        textAnnotation.tool = OVERLAY_ANNOTATE_TEXT;
        textAnnotation.startPoint = g_overlay.textAnchor;
        textAnnotation.endPoint = g_overlay.textAnchor;
        wcsncpy(textAnnotation.text, g_overlay.editingText, OVERLAY_TEXT_MAX - 1);
        textAnnotation.text[OVERLAY_TEXT_MAX - 1] = L'\0';
        DrawAnnotation(hdcMem, &textAnnotation, selection->x, selection->y);
    }

    GetDIBits(hdcMem, image->hBitmap, 0, image->height, image->pixels, &bmi, DIB_RGB_COLORS);

    SelectObject(hdcMem, oldBitmap);
    DeleteDC(hdcMem);
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

        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT && g_overlay.crossCursor != NULL) {
                SetCursor(g_overlay.crossCursor);
                return TRUE;
            }
            break;

        case WM_LBUTTONDOWN: {
            POINT pt;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);

            if ((g_overlay.state == OVERLAY_STATE_SELECTED ||
                 g_overlay.state == OVERLAY_STATE_TOOLBAR) &&
                g_overlay.annotateTool != OVERLAY_ANNOTATE_NONE) {
                StartAnnotation(&g_overlay, pt);
                return 0;
            }

            if (g_overlay.state == OVERLAY_STATE_SELECTED ||
                g_overlay.state == OVERLAY_STATE_TOOLBAR) {
                return 0;
            }

            g_overlay.isSelecting = true;
            g_overlay.startPoint = pt;
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
            } else if (g_overlay.isDrawingAnnotation) {
                UpdateAnnotation(&g_overlay, pt);
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
            POINT pt;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);

            if (g_overlay.isDrawingAnnotation) {
                FinishAnnotation(&g_overlay, pt);
            } else if (g_overlay.isSelecting) {
                g_overlay.isSelecting = false;
                ReleaseCapture();

                if (g_overlay.selection.width > 5 && g_overlay.selection.height > 5) {
                    g_overlay.state = OVERLAY_STATE_SELECTED;
                    LOG_INFO("[选区窗口] 选区完成: (%d, %d) %dx%d",
                             g_overlay.selection.x, g_overlay.selection.y,
                             g_overlay.selection.width, g_overlay.selection.height);
                    InvalidateRect(hwnd, NULL, FALSE);
                    g_overlay.state = OVERLAY_STATE_TOOLBAR;
                    ScreenshotManagerOnSelectionComplete();
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
                if (g_overlay.isEditingText) {
                    CancelTextEdit(&g_overlay);
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                if (g_overlay.isDrawingAnnotation) {
                    g_overlay.isDrawingAnnotation = false;
                    ReleaseCapture();
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                LOG_DEBUG("[选区窗口] ESC 取消");
                ScreenshotOverlayHide();
                return 0;
            }
            if (wParam == VK_RETURN && g_overlay.isEditingText) {
                CommitTextEdit(&g_overlay);
                return 0;
            }
            break;
        }

        case WM_CHAR: {
            if (g_overlay.isEditingText) {
                if (wParam == VK_BACK) {
                    if (g_overlay.editingTextLen > 0) {
                        g_overlay.editingText[--g_overlay.editingTextLen] = L'\0';
                    }
                } else if (wParam == VK_RETURN || wParam == VK_ESCAPE) {
                    return 0;
                } else if (wParam >= 32 && g_overlay.editingTextLen < OVERLAY_TEXT_MAX - 1) {
                    g_overlay.editingText[g_overlay.editingTextLen++] = (WCHAR)wParam;
                    g_overlay.editingText[g_overlay.editingTextLen] = L'\0';
                }
                InvalidateRect(hwnd, NULL, FALSE);
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
