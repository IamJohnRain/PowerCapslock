#include "screenshot_overlay.h"
#include "screenshot.h"
#include "screenshot_manager.h"
#include "logger.h"
#include <gdiplus.h>
#include <imm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windowsx.h>

static const WCHAR* WINDOW_CLASS = L"PowerCapslockScreenshotOverlay";

static OverlayContext g_overlay = {0};
static bool g_initialized = false;
static ULONG_PTR g_gdiplusToken = 0;
static bool g_gdiplusReady = false;

// 前向声明
static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static void UpdateSelectionRect(OverlayContext* ctx);
static bool HasEditableSelection(const OverlayContext* ctx);
static OverlayResizeHandle HitTestSelectionResizeHandle(const OverlayContext* ctx, POINT pt);
static void SetResizeCursor(OverlayResizeHandle handle);
static void StartSelectionResize(OverlayContext* ctx, POINT pt, OverlayResizeHandle handle);
static void UpdateSelectionResize(OverlayContext* ctx, POINT pt);
static void FinishSelectionResize(OverlayContext* ctx);
static bool CanMoveSelectionAtPoint(const OverlayContext* ctx, POINT pt);
static void StartSelectionMove(OverlayContext* ctx, POINT pt);
static void UpdateSelectionMove(OverlayContext* ctx, POINT pt);
static void FinishSelectionMove(OverlayContext* ctx);
static void DrawOverlay(HDC hdc, OverlayContext* ctx);
static void DrawSelectionRect(HDC hdc, OverlayContext* ctx);
static void DrawHoveredWindow(HDC hdc, OverlayContext* ctx);
static void DrawSizeTip(HDC hdc, OverlayContext* ctx);
static void DrawAnnotations(HDC hdc, OverlayContext* ctx);
static void DrawAnnotation(HDC hdc, const OverlayAnnotation* annotation, int offsetX, int offsetY);
static bool DrawAnnotationGdiPlus(HDC hdc, const OverlayAnnotation* annotation, int offsetX, int offsetY);
static void DrawArrowGdiPlus(GpGraphics* graphics, GpPen* pen, GpBrush* brush, POINT start, POINT end, int lineWidth);
static void DrawArrow(HDC hdc, POINT start, POINT end);
static void DrawTextSelection(HDC hdc, const OverlayContext* ctx);
static void DrawTextCaret(HDC hdc, const OverlayContext* ctx);
static HFONT CreateTextAnnotationFont(int fontHeight);
static POINT TextOriginFromClick(POINT pt);
static SIZE MeasureTextSize(const WCHAR* text, int len, int fontHeight);
static int MeasureTextWidth(const WCHAR* text, int len, int fontHeight);
static RECT GetTextAnnotationRect(const OverlayAnnotation* annotation);
static int GetTextCaretIndexFromPoint(const OverlayAnnotation* annotation, POINT pt);
static int HitTestTextAnnotation(const OverlayContext* ctx, POINT pt);
static OverlayAnnotation BuildEditingTextAnnotation(const OverlayContext* ctx);
static bool PointInEditingText(const OverlayContext* ctx, POINT pt);
static void NormalizeTextSelection(const OverlayContext* ctx, int* start, int* end);
static void SetEditingCaret(OverlayContext* ctx, int caretIndex, bool keepSelection);
static void StartTextSelection(OverlayContext* ctx, POINT pt);
static void UpdateTextSelection(OverlayContext* ctx, POINT pt);
static bool DeleteSelectedText(OverlayContext* ctx);
static void RemoveAnnotationAt(OverlayContext* ctx, int index);
static void BeginTextEdit(OverlayContext* ctx, POINT origin, const WCHAR* initialText, int annotationIndex, int caretIndex);
static void FocusOverlayForTextInput(HWND hwnd);
static void DeleteTextBeforeCaret(OverlayContext* ctx);
static void DeleteTextAtCaret(OverlayContext* ctx);
static void NormalizeRectPoints(POINT start, POINT end, int* x, int* y, int* width, int* height);
static bool PointInSelection(OverlayContext* ctx, POINT pt);
static void ResetAnnotations(OverlayContext* ctx);
static void StartAnnotation(OverlayContext* ctx, POINT pt);
static void UpdateAnnotation(OverlayContext* ctx, POINT pt);
static void FinishAnnotation(OverlayContext* ctx, POINT pt);
static void CancelTextEdit(OverlayContext* ctx);
static void CommitTextEdit(OverlayContext* ctx);
static void AppendEditingText(OverlayContext* ctx, const WCHAR* text, int len);
static bool HandleImeComposition(HWND hwnd, LPARAM lParam);
static void ApplyAnnotationsToImage(ScreenshotImage* image, const ScreenshotRect* selection);
static HCURSOR CreateSelectionCursor(void);
static HWND GetWindowFromPointEx(POINT pt);
static void CaptureScreenToContext(OverlayContext* ctx);

#define TEXT_CARET_TIMER_ID 1001
#define TEXT_CARET_BLINK_MS 500
#define DEFAULT_ANNOTATION_COLOR RGB(255, 64, 64)
#define DEFAULT_ANNOTATION_LINE_WIDTH 3
#define DEFAULT_TEXT_ANNOTATION_FONT_HEIGHT 24
#define MIN_TEXT_ANNOTATION_FONT_HEIGHT 16
#define MAX_TEXT_ANNOTATION_FONT_HEIGHT 48
#define TEXT_ANNOTATION_MIN_WIDTH 48
#define TEXT_ANNOTATION_PADDING_X 14
#define TEXT_ANNOTATION_PADDING_Y 8
#define TEXT_ANNOTATION_OVERHANG_X 18
#define TEXT_ANNOTATION_MAX_DRAW_WIDTH 4096
#define SELECTION_RESIZE_HIT_SIZE 8
#define SELECTION_RESIZE_MIN_SIZE 12

static int ClampTextFontHeight(int fontHeight) {
    if (fontHeight < MIN_TEXT_ANNOTATION_FONT_HEIGHT) {
        return MIN_TEXT_ANNOTATION_FONT_HEIGHT;
    }
    if (fontHeight > MAX_TEXT_ANNOTATION_FONT_HEIGHT) {
        return MAX_TEXT_ANNOTATION_FONT_HEIGHT;
    }
    return fontHeight;
}

static int GetAnnotationLineWidth(const OverlayAnnotation* annotation) {
    if (annotation == NULL || annotation->lineWidth <= 0) {
        return DEFAULT_ANNOTATION_LINE_WIDTH;
    }
    return annotation->lineWidth;
}

static int GetTextFontHeight(const OverlayAnnotation* annotation) {
    if (annotation == NULL || annotation->textFontHeight <= 0) {
        return DEFAULT_TEXT_ANNOTATION_FONT_HEIGHT;
    }
    return ClampTextFontHeight(annotation->textFontHeight);
}

static int GetTextLineHeight(int fontHeight) {
    return ClampTextFontHeight(fontHeight) + 6;
}

static ARGB ColorRefToArgb(COLORREF color) {
    return 0xFF000000 |
           ((ARGB)GetRValue(color) << 16) |
           ((ARGB)GetGValue(color) << 8) |
           (ARGB)GetBValue(color);
}

static HCURSOR CreateSelectionCursor(void) {
    enum { CURSOR_SIZE = 16, CURSOR_CORE_THICKNESS = 2, CURSOR_OUTLINE_THICKNESS = 3, CURSOR_MARGIN = 1 };
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
    int coreStart = (CURSOR_SIZE - CURSOR_CORE_THICKNESS) / 2;
    int coreEnd = coreStart + CURSOR_CORE_THICKNESS;
    int outlineStart = (CURSOR_SIZE - CURSOR_OUTLINE_THICKNESS) / 2;
    int outlineEnd = outlineStart + CURSOR_OUTLINE_THICKNESS;

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
            bool onOutlineVertical = (x >= outlineStart && x < outlineEnd);
            bool onOutlineHorizontal = (y >= outlineStart && y < outlineEnd);
            bool onCoreVertical = (x >= coreStart && x < coreEnd);
            bool onCoreHorizontal = (y >= coreStart && y < coreEnd);
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

    if (!g_gdiplusReady) {
        GdiplusStartupInput gdiplusInput;
        ZeroMemory(&gdiplusInput, sizeof(gdiplusInput));
        gdiplusInput.GdiplusVersion = 1;
        if (GdiplusStartup(&g_gdiplusToken, &gdiplusInput, NULL) == Ok) {
            g_gdiplusReady = true;
        } else {
            LOG_WARN("[overlay] GDI+ init failed, falling back to GDI annotation drawing");
        }
    }

    if (g_overlay.crossCursor == NULL) {
        g_overlay.crossCursor = CreateSelectionCursor();
    }
    if (g_overlay.crossCursor == NULL) {
        g_overlay.crossCursor = LoadCursor(NULL, IDC_CROSS);
    }

    // 注册窗口类
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = g_overlay.crossCursor;
    wc.hbrBackground = NULL;
    wc.lpszClassName = WINDOW_CLASS;

    if (!RegisterClassExW(&wc)) {
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

    if (g_gdiplusReady) {
        GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
        g_gdiplusReady = false;
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
    g_overlay.hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE,
        WINDOW_CLASS,
        L"ScreenshotOverlay",
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

    if (g_overlay.isEditingText) {
        CommitTextEdit(&g_overlay);
    }
    g_overlay.annotateTool = tool;
    g_overlay.isDrawingAnnotation = false;

    if (g_overlay.hwnd != NULL) {
        SetCursor(g_overlay.crossCursor);
        InvalidateRect(g_overlay.hwnd, NULL, FALSE);
    }

    LOG_INFO("[选区窗口] 标注工具: %d", tool);
}

OverlayAnnotateTool ScreenshotOverlayGetAnnotationTool(void) {
    return g_overlay.annotateTool;
}

void ScreenshotOverlaySetAnnotationColor(COLORREF color) {
    g_overlay.currentAnnotationColor = color;
    if (g_overlay.isDrawingAnnotation) {
        g_overlay.currentAnnotation.color = color;
    }
    if (g_overlay.isEditingText) {
        g_overlay.editingTextColor = color;
        FocusOverlayForTextInput(g_overlay.hwnd);
    }
    if (g_overlay.hwnd != NULL) {
        InvalidateRect(g_overlay.hwnd, NULL, FALSE);
    }
}

COLORREF ScreenshotOverlayGetAnnotationColor(void) {
    return g_overlay.currentAnnotationColor;
}

void ScreenshotOverlaySetTextFontHeight(int fontHeight) {
    fontHeight = ClampTextFontHeight(fontHeight);
    g_overlay.currentTextFontHeight = fontHeight;
    if (g_overlay.isEditingText) {
        g_overlay.editingTextFontHeight = fontHeight;
        FocusOverlayForTextInput(g_overlay.hwnd);
    }
    if (g_overlay.hwnd != NULL) {
        InvalidateRect(g_overlay.hwnd, NULL, FALSE);
    }
}

int ScreenshotOverlayGetTextFontHeight(void) {
    if (g_overlay.isEditingText && g_overlay.editingTextFontHeight > 0) {
        return ClampTextFontHeight(g_overlay.editingTextFontHeight);
    }
    if (g_overlay.currentTextFontHeight <= 0) {
        return DEFAULT_TEXT_ANNOTATION_FONT_HEIGHT;
    }
    return ClampTextFontHeight(g_overlay.currentTextFontHeight);
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

static bool HasEditableSelection(const OverlayContext* ctx) {
    return ctx != NULL &&
           ctx->selection.width > 0 &&
           ctx->selection.height > 0 &&
           (ctx->state == OVERLAY_STATE_SELECTED ||
            ctx->state == OVERLAY_STATE_TOOLBAR);
}

static OverlayResizeHandle HitTestSelectionResizeHandle(const OverlayContext* ctx, POINT pt) {
    int left;
    int right;
    int top;
    int bottom;
    bool inExpandedBounds;
    bool nearLeft;
    bool nearRight;
    bool nearTop;
    bool nearBottom;

    if (!HasEditableSelection(ctx)) {
        return OVERLAY_RESIZE_NONE;
    }

    left = ctx->selection.x;
    right = ctx->selection.x + ctx->selection.width;
    top = ctx->selection.y;
    bottom = ctx->selection.y + ctx->selection.height;

    inExpandedBounds =
        pt.x >= left - SELECTION_RESIZE_HIT_SIZE &&
        pt.x <= right + SELECTION_RESIZE_HIT_SIZE &&
        pt.y >= top - SELECTION_RESIZE_HIT_SIZE &&
        pt.y <= bottom + SELECTION_RESIZE_HIT_SIZE;
    if (!inExpandedBounds) {
        return OVERLAY_RESIZE_NONE;
    }

    nearLeft = abs(pt.x - left) <= SELECTION_RESIZE_HIT_SIZE;
    nearRight = abs(pt.x - right) <= SELECTION_RESIZE_HIT_SIZE;
    nearTop = abs(pt.y - top) <= SELECTION_RESIZE_HIT_SIZE;
    nearBottom = abs(pt.y - bottom) <= SELECTION_RESIZE_HIT_SIZE;

    if (nearLeft && nearTop) return OVERLAY_RESIZE_TOP_LEFT;
    if (nearRight && nearTop) return OVERLAY_RESIZE_TOP_RIGHT;
    if (nearLeft && nearBottom) return OVERLAY_RESIZE_BOTTOM_LEFT;
    if (nearRight && nearBottom) return OVERLAY_RESIZE_BOTTOM_RIGHT;
    if (nearLeft) return OVERLAY_RESIZE_LEFT;
    if (nearRight) return OVERLAY_RESIZE_RIGHT;
    if (nearTop) return OVERLAY_RESIZE_TOP;
    if (nearBottom) return OVERLAY_RESIZE_BOTTOM;
    return OVERLAY_RESIZE_NONE;
}

static void SetResizeCursor(OverlayResizeHandle handle) {
    LPCTSTR cursorId = NULL;

    switch (handle) {
        case OVERLAY_RESIZE_LEFT:
        case OVERLAY_RESIZE_RIGHT:
            cursorId = IDC_SIZEWE;
            break;
        case OVERLAY_RESIZE_TOP:
        case OVERLAY_RESIZE_BOTTOM:
            cursorId = IDC_SIZENS;
            break;
        case OVERLAY_RESIZE_TOP_LEFT:
        case OVERLAY_RESIZE_BOTTOM_RIGHT:
            cursorId = IDC_SIZENWSE;
            break;
        case OVERLAY_RESIZE_TOP_RIGHT:
        case OVERLAY_RESIZE_BOTTOM_LEFT:
            cursorId = IDC_SIZENESW;
            break;
        default:
            break;
    }

    if (cursorId != NULL) {
        SetCursor(LoadCursor(NULL, cursorId));
    } else if (g_overlay.crossCursor != NULL) {
        SetCursor(g_overlay.crossCursor);
    }
}

static void StartSelectionResize(OverlayContext* ctx, POINT pt, OverlayResizeHandle handle) {
    if (ctx == NULL || handle == OVERLAY_RESIZE_NONE) {
        return;
    }

    if (ctx->isEditingText) {
        CommitTextEdit(ctx);
    }
    ctx->isResizingSelection = true;
    ctx->resizeHandle = handle;
    ctx->resizeStartPoint = pt;
    ctx->resizeStartSelection = ctx->selection;
    SetResizeCursor(handle);
    SetCapture(ctx->hwnd);
}

static void UpdateSelectionResize(OverlayContext* ctx, POINT pt) {
    int left;
    int right;
    int top;
    int bottom;
    int dx;
    int dy;
    int maxWidth;
    int maxHeight;

    if (ctx == NULL || !ctx->isResizingSelection) {
        return;
    }

    dx = pt.x - ctx->resizeStartPoint.x;
    dy = pt.y - ctx->resizeStartPoint.y;
    left = ctx->resizeStartSelection.x;
    top = ctx->resizeStartSelection.y;
    right = ctx->resizeStartSelection.x + ctx->resizeStartSelection.width;
    bottom = ctx->resizeStartSelection.y + ctx->resizeStartSelection.height;

    switch (ctx->resizeHandle) {
        case OVERLAY_RESIZE_LEFT:
        case OVERLAY_RESIZE_TOP_LEFT:
        case OVERLAY_RESIZE_BOTTOM_LEFT:
            left += dx;
            break;
        default:
            break;
    }
    switch (ctx->resizeHandle) {
        case OVERLAY_RESIZE_RIGHT:
        case OVERLAY_RESIZE_TOP_RIGHT:
        case OVERLAY_RESIZE_BOTTOM_RIGHT:
            right += dx;
            break;
        default:
            break;
    }
    switch (ctx->resizeHandle) {
        case OVERLAY_RESIZE_TOP:
        case OVERLAY_RESIZE_TOP_LEFT:
        case OVERLAY_RESIZE_TOP_RIGHT:
            top += dy;
            break;
        default:
            break;
    }
    switch (ctx->resizeHandle) {
        case OVERLAY_RESIZE_BOTTOM:
        case OVERLAY_RESIZE_BOTTOM_LEFT:
        case OVERLAY_RESIZE_BOTTOM_RIGHT:
            bottom += dy;
            break;
        default:
            break;
    }

    maxWidth = ctx->screenImage != NULL ? ctx->screenImage->width : GetSystemMetrics(SM_CXVIRTUALSCREEN);
    maxHeight = ctx->screenImage != NULL ? ctx->screenImage->height : GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > maxWidth) right = maxWidth;
    if (bottom > maxHeight) bottom = maxHeight;

    if (right - left < SELECTION_RESIZE_MIN_SIZE) {
        switch (ctx->resizeHandle) {
            case OVERLAY_RESIZE_LEFT:
            case OVERLAY_RESIZE_TOP_LEFT:
            case OVERLAY_RESIZE_BOTTOM_LEFT:
                left = right - SELECTION_RESIZE_MIN_SIZE;
                if (left < 0) {
                    left = 0;
                    right = left + SELECTION_RESIZE_MIN_SIZE;
                }
                break;
            default:
                right = left + SELECTION_RESIZE_MIN_SIZE;
                if (right > maxWidth) {
                    right = maxWidth;
                    left = right - SELECTION_RESIZE_MIN_SIZE;
                }
                break;
        }
    }

    if (bottom - top < SELECTION_RESIZE_MIN_SIZE) {
        switch (ctx->resizeHandle) {
            case OVERLAY_RESIZE_TOP:
            case OVERLAY_RESIZE_TOP_LEFT:
            case OVERLAY_RESIZE_TOP_RIGHT:
                top = bottom - SELECTION_RESIZE_MIN_SIZE;
                if (top < 0) {
                    top = 0;
                    bottom = top + SELECTION_RESIZE_MIN_SIZE;
                }
                break;
            default:
                bottom = top + SELECTION_RESIZE_MIN_SIZE;
                if (bottom > maxHeight) {
                    bottom = maxHeight;
                    top = bottom - SELECTION_RESIZE_MIN_SIZE;
                }
                break;
        }
    }

    ctx->selection.x = left;
    ctx->selection.y = top;
    ctx->selection.width = right - left;
    ctx->selection.height = bottom - top;
    InvalidateRect(ctx->hwnd, NULL, FALSE);
}

static void FinishSelectionResize(OverlayContext* ctx) {
    if (ctx == NULL || !ctx->isResizingSelection) {
        return;
    }

    ctx->isResizingSelection = false;
    ctx->resizeHandle = OVERLAY_RESIZE_NONE;
    ReleaseCapture();
    ScreenshotManagerOnSelectionComplete();
    InvalidateRect(ctx->hwnd, NULL, FALSE);
}

static bool CanMoveSelectionAtPoint(const OverlayContext* ctx, POINT pt) {
    return HasEditableSelection(ctx) &&
           ctx->annotateTool == OVERLAY_ANNOTATE_NONE &&
           HitTestSelectionResizeHandle(ctx, pt) == OVERLAY_RESIZE_NONE &&
           pt.x >= ctx->selection.x &&
           pt.x <= ctx->selection.x + ctx->selection.width &&
           pt.y >= ctx->selection.y &&
           pt.y <= ctx->selection.y + ctx->selection.height;
}

static void StartSelectionMove(OverlayContext* ctx, POINT pt) {
    if (ctx == NULL || !CanMoveSelectionAtPoint(ctx, pt)) {
        return;
    }

    if (ctx->isEditingText) {
        CommitTextEdit(ctx);
    }

    ctx->isMovingSelection = true;
    ctx->moveStartPoint = pt;
    ctx->moveStartSelection = ctx->selection;
    SetCursor(LoadCursor(NULL, IDC_SIZEALL));
    SetCapture(ctx->hwnd);
}

static void UpdateSelectionMove(OverlayContext* ctx, POINT pt) {
    int dx;
    int dy;
    int maxWidth;
    int maxHeight;
    int x;
    int y;

    if (ctx == NULL || !ctx->isMovingSelection) {
        return;
    }

    dx = pt.x - ctx->moveStartPoint.x;
    dy = pt.y - ctx->moveStartPoint.y;
    maxWidth = ctx->screenImage != NULL ? ctx->screenImage->width : GetSystemMetrics(SM_CXVIRTUALSCREEN);
    maxHeight = ctx->screenImage != NULL ? ctx->screenImage->height : GetSystemMetrics(SM_CYVIRTUALSCREEN);
    x = ctx->moveStartSelection.x + dx;
    y = ctx->moveStartSelection.y + dy;

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + ctx->moveStartSelection.width > maxWidth) {
        x = maxWidth - ctx->moveStartSelection.width;
    }
    if (y + ctx->moveStartSelection.height > maxHeight) {
        y = maxHeight - ctx->moveStartSelection.height;
    }
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    ctx->selection.x = x;
    ctx->selection.y = y;
    ctx->selection.width = ctx->moveStartSelection.width;
    ctx->selection.height = ctx->moveStartSelection.height;
    InvalidateRect(ctx->hwnd, NULL, FALSE);
}

static void FinishSelectionMove(OverlayContext* ctx) {
    if (ctx == NULL || !ctx->isMovingSelection) {
        return;
    }

    ctx->isMovingSelection = false;
    ReleaseCapture();
    ScreenshotManagerOnSelectionComplete();
    InvalidateRect(ctx->hwnd, NULL, FALSE);
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
    ctx->isResizingSelection = false;
    ctx->isMovingSelection = false;
    ctx->isSelectingText = false;
    ctx->currentAnnotationColor = DEFAULT_ANNOTATION_COLOR;
    ctx->currentLineWidth = DEFAULT_ANNOTATION_LINE_WIDTH;
    ctx->currentTextFontHeight = DEFAULT_TEXT_ANNOTATION_FONT_HEIGHT;
    ctx->editingTextLen = 0;
    ctx->editingText[0] = L'\0';
    ctx->editingAnnotationIndex = -1;
    ctx->editingCaretIndex = 0;
    ctx->editingTextColor = ctx->currentAnnotationColor;
    ctx->editingTextFontHeight = ctx->currentTextFontHeight;
    ctx->textSelectionAnchor = 0;
    ctx->textSelectionStart = 0;
    ctx->textSelectionEnd = 0;
    ctx->resizeHandle = OVERLAY_RESIZE_NONE;
    ctx->hoverResizeHandle = OVERLAY_RESIZE_NONE;
    if (ctx->hwnd != NULL) {
        KillTimer(ctx->hwnd, TEXT_CARET_TIMER_ID);
    }
    ZeroMemory(&ctx->currentAnnotation, sizeof(ctx->currentAnnotation));
}

static void StartAnnotation(OverlayContext* ctx, POINT pt) {
    int textIndex;

    if (ctx == NULL || ctx->annotateTool == OVERLAY_ANNOTATE_NONE || !PointInSelection(ctx, pt)) {
        return;
    }

    if (ctx->isEditingText) {
        CommitTextEdit(ctx);
    }

    if (ctx->annotateTool == OVERLAY_ANNOTATE_TEXT) {
        textIndex = HitTestTextAnnotation(ctx, pt);
        if (textIndex >= 0) {
            BeginTextEdit(ctx,
                          ctx->annotations[textIndex].startPoint,
                          ctx->annotations[textIndex].text,
                          textIndex,
                          GetTextCaretIndexFromPoint(&ctx->annotations[textIndex], pt));
        } else {
            BeginTextEdit(ctx, TextOriginFromClick(pt), L"", -1, 0);
        }
        return;
    }

    ZeroMemory(&ctx->currentAnnotation, sizeof(ctx->currentAnnotation));
    ctx->currentAnnotation.tool = ctx->annotateTool;
    ctx->currentAnnotation.color = ctx->currentAnnotationColor;
    ctx->currentAnnotation.lineWidth = ctx->currentLineWidth;
    ctx->currentAnnotation.textFontHeight = ctx->currentTextFontHeight;
    ctx->currentAnnotation.startPoint = pt;
    ctx->currentAnnotation.endPoint = pt;

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
    ctx->isSelectingText = false;
    ctx->editingTextLen = 0;
    ctx->editingText[0] = L'\0';
    ctx->editingAnnotationIndex = -1;
    ctx->editingCaretIndex = 0;
    ctx->editingTextColor = ctx->currentAnnotationColor;
    ctx->editingTextFontHeight = ctx->currentTextFontHeight;
    ctx->textSelectionAnchor = 0;
    ctx->textSelectionStart = 0;
    ctx->textSelectionEnd = 0;
    if (ctx->hwnd != NULL && GetCapture() == ctx->hwnd) {
        ReleaseCapture();
    }
    if (ctx->hwnd != NULL) {
        KillTimer(ctx->hwnd, TEXT_CARET_TIMER_ID);
    }
}

static void CommitTextEdit(OverlayContext* ctx) {
    OverlayAnnotation annotation;

    if (ctx == NULL || !ctx->isEditingText) {
        return;
    }

    if (ctx->editingAnnotationIndex >= 0 &&
        ctx->editingAnnotationIndex < ctx->annotationCount) {
        if (ctx->editingTextLen > 0) {
            ctx->annotations[ctx->editingAnnotationIndex].color = ctx->editingTextColor;
            ctx->annotations[ctx->editingAnnotationIndex].lineWidth = ctx->currentLineWidth;
            ctx->annotations[ctx->editingAnnotationIndex].textFontHeight = ctx->editingTextFontHeight;
            ctx->annotations[ctx->editingAnnotationIndex].startPoint = ctx->textAnchor;
            ctx->annotations[ctx->editingAnnotationIndex].endPoint = ctx->textAnchor;
            wcsncpy(ctx->annotations[ctx->editingAnnotationIndex].text,
                    ctx->editingText,
                    OVERLAY_TEXT_MAX - 1);
            ctx->annotations[ctx->editingAnnotationIndex].text[OVERLAY_TEXT_MAX - 1] = L'\0';
        } else {
            RemoveAnnotationAt(ctx, ctx->editingAnnotationIndex);
        }
    } else if (ctx->editingTextLen > 0 && ctx->annotationCount < OVERLAY_MAX_ANNOTATIONS) {
        ZeroMemory(&annotation, sizeof(annotation));
        annotation.tool = OVERLAY_ANNOTATE_TEXT;
        annotation.color = ctx->editingTextColor;
        annotation.lineWidth = ctx->currentLineWidth;
        annotation.textFontHeight = ctx->editingTextFontHeight;
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

static void AppendEditingText(OverlayContext* ctx, const WCHAR* text, int len) {
    int insertCount = 0;

    if (ctx == NULL || text == NULL || len <= 0 || !ctx->isEditingText) {
        return;
    }

    if (ctx->editingCaretIndex < 0) {
        ctx->editingCaretIndex = 0;
    }
    if (ctx->editingCaretIndex > ctx->editingTextLen) {
        ctx->editingCaretIndex = ctx->editingTextLen;
    }
    DeleteSelectedText(ctx);

    for (int i = 0; i < len && ctx->editingTextLen + insertCount < OVERLAY_TEXT_MAX - 1; i++) {
        WCHAR ch = text[i];
        if (ch == L'\0') {
            break;
        }
        if (ch == L'\r' || ch == L'\n' || ch == VK_ESCAPE) {
            continue;
        }
        if (ch >= 32) {
            insertCount++;
        }
    }

    if (insertCount <= 0) {
        return;
    }

    memmove(ctx->editingText + ctx->editingCaretIndex + insertCount,
            ctx->editingText + ctx->editingCaretIndex,
            (size_t)(ctx->editingTextLen - ctx->editingCaretIndex + 1) * sizeof(WCHAR));

    insertCount = 0;
    for (int i = 0; i < len && ctx->editingTextLen + insertCount < OVERLAY_TEXT_MAX - 1; i++) {
        WCHAR ch = text[i];
        if (ch == L'\0') {
            break;
        }
        if (ch == L'\r' || ch == L'\n' || ch == VK_ESCAPE) {
            continue;
        }
        if (ch >= 32) {
            ctx->editingText[ctx->editingCaretIndex + insertCount++] = ch;
        }
    }

    ctx->editingTextLen += insertCount;
    SetEditingCaret(ctx, ctx->editingCaretIndex + insertCount, false);
    ctx->editingText[ctx->editingTextLen] = L'\0';
}

static HFONT CreateTextAnnotationFont(int fontHeight) {
    fontHeight = ClampTextFontHeight(fontHeight);
    return CreateFontW(fontHeight, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
}

static POINT TextOriginFromClick(POINT pt) {
    POINT origin = pt;
    origin.y -= GetTextLineHeight(g_overlay.currentTextFontHeight);
    if (origin.y < g_overlay.selection.y) {
        origin.y = g_overlay.selection.y;
    }
    return origin;
}

static SIZE MeasureTextSize(const WCHAR* text, int len, int fontHeight) {
    SIZE textSize = {0};
    HDC hdc;
    HFONT font;
    HFONT oldFont;

    fontHeight = ClampTextFontHeight(fontHeight);

    if (text == NULL || len <= 0) {
        return textSize;
    }

    hdc = GetDC(NULL);
    font = CreateTextAnnotationFont(fontHeight);
    if (hdc != NULL && font != NULL) {
        oldFont = (HFONT)SelectObject(hdc, font);
        GetTextExtentPoint32W(hdc, text, len, &textSize);
        SelectObject(hdc, oldFont);
    }
    if (font != NULL) {
        DeleteObject(font);
    }
    if (hdc != NULL) {
        ReleaseDC(NULL, hdc);
    }

    return textSize;
}

static int MeasureTextWidth(const WCHAR* text, int len, int fontHeight) {
    return MeasureTextSize(text, len, fontHeight).cx;
}

static RECT GetTextAnnotationRect(const OverlayAnnotation* annotation) {
    RECT rect = {0};
    SIZE textSize;
    int textWidth;
    int textHeight;
    int fontHeight;
    int lineHeight;

    if (annotation == NULL || annotation->tool != OVERLAY_ANNOTATE_TEXT) {
        return rect;
    }

    fontHeight = GetTextFontHeight(annotation);
    lineHeight = GetTextLineHeight(fontHeight);
    textSize = MeasureTextSize(annotation->text, (int)wcslen(annotation->text), fontHeight);
    textWidth = textSize.cx;
    textHeight = textSize.cy;
    if (textWidth < TEXT_ANNOTATION_MIN_WIDTH) {
        textWidth = TEXT_ANNOTATION_MIN_WIDTH;
    }
    if (textHeight < lineHeight) {
        textHeight = lineHeight;
    }

    rect.left = annotation->startPoint.x - TEXT_ANNOTATION_PADDING_X - TEXT_ANNOTATION_OVERHANG_X;
    rect.top = annotation->startPoint.y - TEXT_ANNOTATION_PADDING_Y;
    rect.right = annotation->startPoint.x + textWidth + TEXT_ANNOTATION_PADDING_X + TEXT_ANNOTATION_OVERHANG_X;
    rect.bottom = annotation->startPoint.y + textHeight + TEXT_ANNOTATION_PADDING_Y;
    return rect;
}

static int GetTextCaretIndexFromPoint(const OverlayAnnotation* annotation, POINT pt) {
    int len;
    int relativeX;
    int previousWidth = 0;
    int bestIndex = 0;
    int bestDistance = 0x7fffffff;
    int fontHeight;

    if (annotation == NULL || annotation->tool != OVERLAY_ANNOTATE_TEXT) {
        return 0;
    }

    len = (int)wcslen(annotation->text);
    fontHeight = GetTextFontHeight(annotation);
    relativeX = pt.x - annotation->startPoint.x;
    if (relativeX <= 0) {
        return 0;
    }

    for (int i = 1; i <= len; i++) {
        int currentWidth = MeasureTextWidth(annotation->text, i, fontHeight);
        int center = previousWidth + (currentWidth - previousWidth) / 2;
        int distance = abs(relativeX - center);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = i;
        }
        previousWidth = currentWidth;
    }

    return bestIndex;
}

static int HitTestTextAnnotation(const OverlayContext* ctx, POINT pt) {
    if (ctx == NULL) {
        return -1;
    }

    for (int i = ctx->annotationCount - 1; i >= 0; i--) {
        RECT rect;
        if (ctx->annotations[i].tool != OVERLAY_ANNOTATE_TEXT) {
            continue;
        }
        rect = GetTextAnnotationRect(&ctx->annotations[i]);
        if (PtInRect(&rect, pt)) {
            return i;
        }
    }
    return -1;
}

static OverlayAnnotation BuildEditingTextAnnotation(const OverlayContext* ctx) {
    OverlayAnnotation annotation;

    ZeroMemory(&annotation, sizeof(annotation));
    if (ctx == NULL) {
        return annotation;
    }

    annotation.tool = OVERLAY_ANNOTATE_TEXT;
    annotation.color = ctx->editingTextColor;
    annotation.lineWidth = ctx->currentLineWidth;
    annotation.textFontHeight = ctx->editingTextFontHeight;
    annotation.startPoint = ctx->textAnchor;
    annotation.endPoint = ctx->textAnchor;
    wcsncpy(annotation.text, ctx->editingText, OVERLAY_TEXT_MAX - 1);
    annotation.text[OVERLAY_TEXT_MAX - 1] = L'\0';
    return annotation;
}

static bool PointInEditingText(const OverlayContext* ctx, POINT pt) {
    OverlayAnnotation annotation;
    RECT rect;

    if (ctx == NULL || !ctx->isEditingText) {
        return false;
    }

    annotation = BuildEditingTextAnnotation(ctx);
    rect = GetTextAnnotationRect(&annotation);
    return PtInRect(&rect, pt) ? true : false;
}

static void NormalizeTextSelection(const OverlayContext* ctx, int* start, int* end) {
    int selectionStart;
    int selectionEnd;

    if (start == NULL || end == NULL) {
        return;
    }

    if (ctx == NULL || !ctx->isEditingText) {
        *start = 0;
        *end = 0;
        return;
    }

    selectionStart = ctx->textSelectionStart;
    selectionEnd = ctx->textSelectionEnd;
    if (selectionStart > selectionEnd) {
        int tmp = selectionStart;
        selectionStart = selectionEnd;
        selectionEnd = tmp;
    }
    if (selectionStart < 0) selectionStart = 0;
    if (selectionEnd < 0) selectionEnd = 0;
    if (selectionStart > ctx->editingTextLen) selectionStart = ctx->editingTextLen;
    if (selectionEnd > ctx->editingTextLen) selectionEnd = ctx->editingTextLen;

    *start = selectionStart;
    *end = selectionEnd;
}

static void SetEditingCaret(OverlayContext* ctx, int caretIndex, bool keepSelection) {
    if (ctx == NULL || !ctx->isEditingText) {
        return;
    }

    if (caretIndex < 0) {
        caretIndex = 0;
    }
    if (caretIndex > ctx->editingTextLen) {
        caretIndex = ctx->editingTextLen;
    }

    ctx->editingCaretIndex = caretIndex;
    if (keepSelection) {
        ctx->textSelectionStart = ctx->textSelectionAnchor < caretIndex ? ctx->textSelectionAnchor : caretIndex;
        ctx->textSelectionEnd = ctx->textSelectionAnchor > caretIndex ? ctx->textSelectionAnchor : caretIndex;
    } else {
        ctx->textSelectionAnchor = caretIndex;
        ctx->textSelectionStart = caretIndex;
        ctx->textSelectionEnd = caretIndex;
    }
}

static void StartTextSelection(OverlayContext* ctx, POINT pt) {
    OverlayAnnotation annotation;
    int caretIndex;

    if (ctx == NULL || !ctx->isEditingText) {
        return;
    }

    annotation = BuildEditingTextAnnotation(ctx);
    caretIndex = GetTextCaretIndexFromPoint(&annotation, pt);
    SetEditingCaret(ctx, caretIndex, false);
    ctx->isSelectingText = true;
    SetCapture(ctx->hwnd);
    FocusOverlayForTextInput(ctx->hwnd);
    InvalidateRect(ctx->hwnd, NULL, FALSE);
}

static void UpdateTextSelection(OverlayContext* ctx, POINT pt) {
    OverlayAnnotation annotation;
    int caretIndex;

    if (ctx == NULL || !ctx->isEditingText || !ctx->isSelectingText) {
        return;
    }

    annotation = BuildEditingTextAnnotation(ctx);
    caretIndex = GetTextCaretIndexFromPoint(&annotation, pt);
    SetEditingCaret(ctx, caretIndex, true);
    InvalidateRect(ctx->hwnd, NULL, FALSE);
}

static bool DeleteSelectedText(OverlayContext* ctx) {
    int start;
    int end;

    if (ctx == NULL || !ctx->isEditingText) {
        return false;
    }

    NormalizeTextSelection(ctx, &start, &end);
    if (start == end) {
        return false;
    }

    memmove(ctx->editingText + start,
            ctx->editingText + end,
            (size_t)(ctx->editingTextLen - end + 1) * sizeof(WCHAR));
    ctx->editingTextLen -= (end - start);
    SetEditingCaret(ctx, start, false);
    return true;
}

static void RemoveAnnotationAt(OverlayContext* ctx, int index) {
    if (ctx == NULL || index < 0 || index >= ctx->annotationCount) {
        return;
    }

    for (int i = index; i < ctx->annotationCount - 1; i++) {
        ctx->annotations[i] = ctx->annotations[i + 1];
    }
    ctx->annotationCount--;
}

static void FocusOverlayForTextInput(HWND hwnd) {
    if (hwnd == NULL || !IsWindow(hwnd)) {
        return;
    }

    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
}

static void BeginTextEdit(OverlayContext* ctx, POINT origin, const WCHAR* initialText, int annotationIndex, int caretIndex) {
    if (ctx == NULL) {
        return;
    }

    ctx->isEditingText = true;
    ctx->textAnchor = origin;
    ctx->editingAnnotationIndex = annotationIndex;
    ctx->editingTextLen = 0;
    ctx->editingText[0] = L'\0';
    ctx->editingTextColor = ctx->currentAnnotationColor;
    ctx->editingTextFontHeight = ctx->currentTextFontHeight;
    if (annotationIndex >= 0 && annotationIndex < ctx->annotationCount) {
        ctx->editingTextColor = ctx->annotations[annotationIndex].color;
        ctx->editingTextFontHeight = GetTextFontHeight(&ctx->annotations[annotationIndex]);
    }
    if (initialText != NULL && initialText[0] != L'\0') {
        wcsncpy(ctx->editingText, initialText, OVERLAY_TEXT_MAX - 1);
        ctx->editingText[OVERLAY_TEXT_MAX - 1] = L'\0';
        ctx->editingTextLen = (int)wcslen(ctx->editingText);
    }
    if (caretIndex < 0) {
        caretIndex = 0;
    }
    if (caretIndex > ctx->editingTextLen) {
        caretIndex = ctx->editingTextLen;
    }
    ctx->editingCaretIndex = caretIndex;
    ctx->isSelectingText = false;
    ctx->textSelectionAnchor = caretIndex;
    ctx->textSelectionStart = caretIndex;
    ctx->textSelectionEnd = caretIndex;

    SetTimer(ctx->hwnd, TEXT_CARET_TIMER_ID, TEXT_CARET_BLINK_MS, NULL);
    FocusOverlayForTextInput(ctx->hwnd);
    InvalidateRect(ctx->hwnd, NULL, FALSE);
}

static void DeleteTextBeforeCaret(OverlayContext* ctx) {
    if (ctx == NULL || !ctx->isEditingText || ctx->editingCaretIndex <= 0) {
        if (ctx != NULL) {
            DeleteSelectedText(ctx);
        }
        return;
    }

    if (DeleteSelectedText(ctx)) {
        return;
    }

    if (ctx->editingCaretIndex > ctx->editingTextLen) {
        ctx->editingCaretIndex = ctx->editingTextLen;
    }

    memmove(ctx->editingText + ctx->editingCaretIndex - 1,
            ctx->editingText + ctx->editingCaretIndex,
            (size_t)(ctx->editingTextLen - ctx->editingCaretIndex + 1) * sizeof(WCHAR));
    ctx->editingTextLen--;
    SetEditingCaret(ctx, ctx->editingCaretIndex - 1, false);
}

static void DeleteTextAtCaret(OverlayContext* ctx) {
    if (ctx == NULL || !ctx->isEditingText ||
        ctx->editingCaretIndex < 0 ||
        ctx->editingCaretIndex >= ctx->editingTextLen) {
        if (ctx != NULL) {
            DeleteSelectedText(ctx);
        }
        return;
    }

    if (DeleteSelectedText(ctx)) {
        return;
    }

    memmove(ctx->editingText + ctx->editingCaretIndex,
            ctx->editingText + ctx->editingCaretIndex + 1,
            (size_t)(ctx->editingTextLen - ctx->editingCaretIndex) * sizeof(WCHAR));
    ctx->editingTextLen--;
    SetEditingCaret(ctx, ctx->editingCaretIndex, false);
}

static bool HandleImeComposition(HWND hwnd, LPARAM lParam) {
    HIMC himc;
    LONG byteCount;
    WCHAR* buffer;
    bool handled = false;

    if (!g_overlay.isEditingText || (lParam & GCS_RESULTSTR) == 0) {
        return false;
    }

    himc = ImmGetContext(hwnd);
    if (himc == NULL) {
        return false;
    }

    byteCount = ImmGetCompositionStringW(himc, GCS_RESULTSTR, NULL, 0);
    if (byteCount > 0) {
        buffer = (WCHAR*)calloc((size_t)byteCount + sizeof(WCHAR), 1);
        if (buffer != NULL) {
            LONG copied = ImmGetCompositionStringW(himc, GCS_RESULTSTR, buffer, (DWORD)byteCount);
            if (copied > 0) {
                AppendEditingText(&g_overlay, buffer, copied / (LONG)sizeof(WCHAR));
                InvalidateRect(hwnd, NULL, FALSE);
                handled = true;
            }
            free(buffer);
        }
    }

    ImmReleaseContext(hwnd, himc);
    return handled;
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

    // 混合遮罩层
    AlphaBlend(hdcMem, 0, 0, clientRect.right, clientRect.bottom,
               hdcMask, 0, 0, clientRect.right, clientRect.bottom, blend);

    // 选区内不叠加白色半透明层，保留原图 100% 可见，避免文字和图标被洗白。
    if (hasSelection && ctx->screenDC != NULL) {
        BitBlt(hdcMem,
               ctx->selection.x, ctx->selection.y,
               ctx->selection.width, ctx->selection.height,
               ctx->screenDC,
               ctx->selection.x, ctx->selection.y,
               SRCCOPY);
    }

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
        if (ctx->isEditingText && i == ctx->editingAnnotationIndex) {
            continue;
        }
        DrawAnnotation(hdc, &ctx->annotations[i], 0, 0);
    }

    if (ctx->isDrawingAnnotation) {
        DrawAnnotation(hdc, &ctx->currentAnnotation, 0, 0);
    }

    if (ctx->isEditingText) {
        if (ctx->editingTextLen > 0) {
            OverlayAnnotation textPreview;
            ZeroMemory(&textPreview, sizeof(textPreview));
            textPreview.tool = OVERLAY_ANNOTATE_TEXT;
            textPreview.color = ctx->editingTextColor;
            textPreview.lineWidth = ctx->currentLineWidth;
            textPreview.textFontHeight = ctx->editingTextFontHeight;
            textPreview.startPoint = ctx->textAnchor;
            wcsncpy(textPreview.text, ctx->editingText, OVERLAY_TEXT_MAX - 1);
            DrawTextSelection(hdc, ctx);
            DrawAnnotation(hdc, &textPreview, 0, 0);
        }
        DrawTextCaret(hdc, ctx);
    }

    RestoreDC(hdc, saved);
}

static void DrawTextSelection(HDC hdc, const OverlayContext* ctx) {
    int start;
    int end;
    int prefixWidth;
    int selectedWidth;
    int lineHeight;
    RECT rect;
    HBRUSH brush;

    if (hdc == NULL || ctx == NULL || !ctx->isEditingText) {
        return;
    }

    NormalizeTextSelection(ctx, &start, &end);
    if (start == end) {
        return;
    }

    prefixWidth = MeasureTextWidth(ctx->editingText, start, ctx->editingTextFontHeight);
    selectedWidth = MeasureTextWidth(ctx->editingText, end, ctx->editingTextFontHeight) - prefixWidth;
    if (selectedWidth <= 0) {
        return;
    }

    lineHeight = GetTextLineHeight(ctx->editingTextFontHeight);
    rect.left = ctx->textAnchor.x + prefixWidth;
    rect.top = ctx->textAnchor.y;
    rect.right = rect.left + selectedWidth;
    rect.bottom = rect.top + lineHeight;

    brush = CreateSolidBrush(RGB(0, 120, 215));
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);
}

static void DrawTextCaret(HDC hdc, const OverlayContext* ctx) {
    HFONT font;
    HFONT oldFont;
    HPEN pen;
    HPEN oldPen;
    int textWidth = 0;
    int x;
    int y;
    int fontHeight;
    int caretHeight;
    int caretPenWidth;
    int caretCapHalfWidth;
    int selectionStart;
    int selectionEnd;
    TEXTMETRICW textMetric;

    if (hdc == NULL || ctx == NULL || !ctx->isEditingText) {
        return;
    }

    NormalizeTextSelection(ctx, &selectionStart, &selectionEnd);
    if (selectionStart != selectionEnd) {
        return;
    }

    if (((GetTickCount() / TEXT_CARET_BLINK_MS) % 2) == 0) {
        return;
    }

    fontHeight = ClampTextFontHeight(ctx->editingTextFontHeight);
    font = CreateTextAnnotationFont(fontHeight);
    if (font == NULL) {
        return;
    }
    oldFont = (HFONT)SelectObject(hdc, font);
    ZeroMemory(&textMetric, sizeof(textMetric));
    if (GetTextMetricsW(hdc, &textMetric)) {
        caretHeight = textMetric.tmHeight;
    } else {
        caretHeight = GetTextLineHeight(fontHeight);
    }
    if (caretHeight <= 0) {
        caretHeight = GetTextLineHeight(fontHeight);
    }
    caretPenWidth = fontHeight / 14;
    if (caretPenWidth < 1) {
        caretPenWidth = 1;
    }
    if (caretPenWidth > 4) {
        caretPenWidth = 4;
    }
    caretCapHalfWidth = fontHeight / 6;
    if (caretCapHalfWidth < 3) {
        caretCapHalfWidth = 3;
    }
    if (caretCapHalfWidth > 8) {
        caretCapHalfWidth = 8;
    }
    if (ctx->editingCaretIndex > 0) {
        int len = ctx->editingCaretIndex;
        if (len > ctx->editingTextLen) {
            len = ctx->editingTextLen;
        }
        textWidth = MeasureTextWidth(ctx->editingText, len, fontHeight);
    }

    x = ctx->textAnchor.x + textWidth;
    y = ctx->textAnchor.y;

    pen = CreatePen(PS_SOLID, caretPenWidth, ctx->editingTextColor);
    oldPen = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, x, y, NULL);
    LineTo(hdc, x, y + caretHeight);
    MoveToEx(hdc, x - caretCapHalfWidth, y, NULL);
    LineTo(hdc, x + caretCapHalfWidth, y);
    MoveToEx(hdc, x - caretCapHalfWidth, y + caretHeight, NULL);
    LineTo(hdc, x + caretCapHalfWidth, y + caretHeight);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    SelectObject(hdc, oldFont);
    DeleteObject(font);
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
    COLORREF annotationColor;
    int lineWidth;
    int textFontHeight;
    int textLineHeight;

    if (hdc == NULL || annotation == NULL || annotation->tool == OVERLAY_ANNOTATE_NONE) {
        return;
    }

    if (DrawAnnotationGdiPlus(hdc, annotation, offsetX, offsetY)) {
        return;
    }

    start.x = annotation->startPoint.x - offsetX;
    start.y = annotation->startPoint.y - offsetY;
    end.x = annotation->endPoint.x - offsetX;
    end.y = annotation->endPoint.y - offsetY;
    annotationColor = annotation->color;
    lineWidth = GetAnnotationLineWidth(annotation);
    textFontHeight = GetTextFontHeight(annotation);
    textLineHeight = GetTextLineHeight(textFontHeight);

    pen = CreatePen(PS_SOLID, lineWidth, annotationColor);
    oldPen = (HPEN)SelectObject(hdc, pen);
    oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, annotationColor);

    switch (annotation->tool) {
        case OVERLAY_ANNOTATE_RECT:
            Rectangle(hdc, start.x, start.y, end.x, end.y);
            break;
        case OVERLAY_ANNOTATE_CIRCLE:
            Ellipse(hdc, start.x, start.y, end.x, end.y);
            break;
        case OVERLAY_ANNOTATE_ARROW:
        {
            HBRUSH arrowBrush = CreateSolidBrush(annotationColor);
            HBRUSH previousBrush = (HBRUSH)SelectObject(hdc, arrowBrush);
            DrawArrow(hdc, start, end);
            SelectObject(hdc, previousBrush);
            DeleteObject(arrowBrush);
            break;
        }
        case OVERLAY_ANNOTATE_PENCIL:
            if (annotation->pointCount > 1) {
                MoveToEx(hdc, annotation->points[0].x - offsetX, annotation->points[0].y - offsetY, NULL);
                for (int i = 1; i < annotation->pointCount; i++) {
                    LineTo(hdc, annotation->points[i].x - offsetX, annotation->points[i].y - offsetY);
                }
            }
            break;
        case OVERLAY_ANNOTATE_TEXT:
            font = CreateTextAnnotationFont(textFontHeight);
            if (font == NULL) {
                break;
            }
            oldFont = (HFONT)SelectObject(hdc, font);
            rect.left = start.x;
            rect.top = start.y;
            rect.right = start.x + TEXT_ANNOTATION_MAX_DRAW_WIDTH;
            rect.bottom = start.y + textLineHeight + TEXT_ANNOTATION_PADDING_Y * 2;
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

static void NormalizeRectPoints(POINT start, POINT end, int* x, int* y, int* width, int* height) {
    int left = start.x < end.x ? start.x : end.x;
    int top = start.y < end.y ? start.y : end.y;
    int right = start.x > end.x ? start.x : end.x;
    int bottom = start.y > end.y ? start.y : end.y;

    *x = left;
    *y = top;
    *width = right - left;
    *height = bottom - top;
}

static bool DrawAnnotationGdiPlus(HDC hdc, const OverlayAnnotation* annotation, int offsetX, int offsetY) {
    GpGraphics* graphics = NULL;
    GpPen* pen = NULL;
    GpSolidFill* brush = NULL;
    POINT start;
    POINT end;
    int x;
    int y;
    int width;
    int height;
    int lineWidth;
    ARGB annotationColor;
    bool drawn = false;

    if (!g_gdiplusReady || hdc == NULL || annotation == NULL ||
        annotation->tool == OVERLAY_ANNOTATE_NONE) {
        return false;
    }
    if (annotation->tool == OVERLAY_ANNOTATE_TEXT) {
        return false;
    }

    if (GdipCreateFromHDC(hdc, &graphics) != Ok || graphics == NULL) {
        return false;
    }

    lineWidth = GetAnnotationLineWidth(annotation);
    annotationColor = ColorRefToArgb(annotation->color);

    if (GdipCreatePen1(annotationColor, (REAL)lineWidth, UnitPixel, &pen) != Ok ||
        GdipCreateSolidFill(annotationColor, &brush) != Ok) {
        goto cleanup;
    }

    GdipSetSmoothingMode(graphics, SmoothingModeAntiAlias8x8);
    GdipSetPixelOffsetMode(graphics, PixelOffsetModeHalf);
    GdipSetTextRenderingHint(graphics, TextRenderingHintClearTypeGridFit);
    GdipSetPenStartCap(pen, LineCapRound);
    GdipSetPenEndCap(pen, LineCapRound);
    GdipSetPenLineJoin(pen, LineJoinRound);

    start.x = annotation->startPoint.x - offsetX;
    start.y = annotation->startPoint.y - offsetY;
    end.x = annotation->endPoint.x - offsetX;
    end.y = annotation->endPoint.y - offsetY;

    switch (annotation->tool) {
        case OVERLAY_ANNOTATE_RECT:
            NormalizeRectPoints(start, end, &x, &y, &width, &height);
            if (width > 0 && height > 0) {
                GdipDrawRectangleI(graphics, pen, x, y, width, height);
                drawn = true;
            }
            break;
        case OVERLAY_ANNOTATE_CIRCLE:
            NormalizeRectPoints(start, end, &x, &y, &width, &height);
            if (width > 0 && height > 0) {
                GdipDrawEllipseI(graphics, pen, x, y, width, height);
                drawn = true;
            }
            break;
        case OVERLAY_ANNOTATE_ARROW:
            DrawArrowGdiPlus(graphics, pen, (GpBrush*)brush, start, end, lineWidth);
            drawn = true;
            break;
        case OVERLAY_ANNOTATE_PENCIL:
            if (annotation->pointCount > 1) {
                GpPoint points[OVERLAY_MAX_PENCIL_POINTS];
                int count = annotation->pointCount;
                if (count > OVERLAY_MAX_PENCIL_POINTS) {
                    count = OVERLAY_MAX_PENCIL_POINTS;
                }
                for (int i = 0; i < count; i++) {
                    points[i].X = annotation->points[i].x - offsetX;
                    points[i].Y = annotation->points[i].y - offsetY;
                }
                GdipDrawLinesI(graphics, pen, points, count);
                drawn = true;
            }
            break;
        case OVERLAY_ANNOTATE_TEXT:
            break;
        default:
            break;
    }

cleanup:
    if (brush != NULL) {
        GdipDeleteBrush((GpBrush*)brush);
    }
    if (pen != NULL) {
        GdipDeletePen(pen);
    }
    if (graphics != NULL) {
        GdipDeleteGraphics(graphics);
    }

    return drawn;
}

static void DrawArrowGdiPlus(GpGraphics* graphics, GpPen* pen, GpBrush* brush, POINT start, POINT end, int lineWidth) {
    int dx = end.x - start.x;
    int dy = end.y - start.y;
    int len = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
    int headLength;
    int headHalfWidth;
    int baseX;
    int baseY;
    int perpX;
    int perpY;
    GpPoint head[3];

    if (graphics == NULL || pen == NULL || brush == NULL || len < 1) {
        return;
    }

    if (lineWidth <= 0) {
        lineWidth = DEFAULT_ANNOTATION_LINE_WIDTH;
    }
    headLength = 10 + lineWidth * 2;
    headHalfWidth = 4 + lineWidth;

    baseX = end.x - dx * headLength / len;
    baseY = end.y - dy * headLength / len;
    perpX = -dy * headHalfWidth / len;
    perpY = dx * headHalfWidth / len;

    head[0].X = end.x;
    head[0].Y = end.y;
    head[1].X = baseX + perpX;
    head[1].Y = baseY + perpY;
    head[2].X = baseX - perpX;
    head[2].Y = baseY - perpY;

    GdipDrawLineI(graphics, pen, start.x, start.y, end.x, end.y);
    GdipFillPolygonI(graphics, brush, head, 3, FillModeAlternate);
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
        if (g_overlay.isEditingText && i == g_overlay.editingAnnotationIndex) {
            continue;
        }
        DrawAnnotation(hdcMem, &g_overlay.annotations[i], selection->x, selection->y);
    }

    if (g_overlay.isEditingText && g_overlay.editingTextLen > 0) {
        OverlayAnnotation textAnnotation;
        ZeroMemory(&textAnnotation, sizeof(textAnnotation));
        textAnnotation.tool = OVERLAY_ANNOTATE_TEXT;
        textAnnotation.color = g_overlay.editingTextColor;
        textAnnotation.lineWidth = g_overlay.currentLineWidth;
        textAnnotation.textFontHeight = g_overlay.editingTextFontHeight;
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
            if (LOWORD(lParam) == HTCLIENT) {
                POINT pt;
                OverlayResizeHandle handle;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                handle = g_overlay.isResizingSelection
                    ? g_overlay.resizeHandle
                    : HitTestSelectionResizeHandle(&g_overlay, pt);
                if (handle != OVERLAY_RESIZE_NONE) {
                    SetResizeCursor(handle);
                } else if (g_overlay.isMovingSelection ||
                           CanMoveSelectionAtPoint(&g_overlay, pt)) {
                    SetCursor(LoadCursor(NULL, IDC_SIZEALL));
                } else if (g_overlay.crossCursor != NULL) {
                    SetCursor(g_overlay.crossCursor);
                }
                return TRUE;
            }
            break;

        case WM_LBUTTONDOWN: {
            POINT pt;
            OverlayResizeHandle resizeHandle;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);

            if (g_overlay.isEditingText &&
                g_overlay.annotateTool == OVERLAY_ANNOTATE_TEXT &&
                PointInEditingText(&g_overlay, pt)) {
                StartTextSelection(&g_overlay, pt);
                return 0;
            }

            resizeHandle = HitTestSelectionResizeHandle(&g_overlay, pt);
            if (resizeHandle != OVERLAY_RESIZE_NONE) {
                StartSelectionResize(&g_overlay, pt, resizeHandle);
                return 0;
            }

            if (CanMoveSelectionAtPoint(&g_overlay, pt)) {
                StartSelectionMove(&g_overlay, pt);
                return 0;
            }

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
            OverlayResizeHandle resizeHandle;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);

            if (g_overlay.isSelectingText) {
                UpdateTextSelection(&g_overlay, pt);
            } else if (g_overlay.isResizingSelection) {
                UpdateSelectionResize(&g_overlay, pt);
                SetResizeCursor(g_overlay.resizeHandle);
            } else if (g_overlay.isMovingSelection) {
                UpdateSelectionMove(&g_overlay, pt);
                SetCursor(LoadCursor(NULL, IDC_SIZEALL));
            } else if (g_overlay.isSelecting) {
                g_overlay.currentPoint = pt;
                UpdateSelectionRect(&g_overlay);
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (g_overlay.isDrawingAnnotation) {
                UpdateAnnotation(&g_overlay, pt);
            } else {
                // 窗口识别
                resizeHandle = HitTestSelectionResizeHandle(&g_overlay, pt);
                if (resizeHandle != g_overlay.hoverResizeHandle) {
                    g_overlay.hoverResizeHandle = resizeHandle;
                }
                if (resizeHandle != OVERLAY_RESIZE_NONE) {
                    SetResizeCursor(resizeHandle);
                    return 0;
                }
                if (CanMoveSelectionAtPoint(&g_overlay, pt)) {
                    SetCursor(LoadCursor(NULL, IDC_SIZEALL));
                    return 0;
                }
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

            if (g_overlay.isSelectingText) {
                UpdateTextSelection(&g_overlay, pt);
                g_overlay.isSelectingText = false;
                ReleaseCapture();
                FocusOverlayForTextInput(hwnd);
            } else if (g_overlay.isResizingSelection) {
                UpdateSelectionResize(&g_overlay, pt);
                FinishSelectionResize(&g_overlay);
            } else if (g_overlay.isMovingSelection) {
                UpdateSelectionMove(&g_overlay, pt);
                FinishSelectionMove(&g_overlay);
            } else if (g_overlay.isDrawingAnnotation) {
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
                if (g_overlay.isResizingSelection) {
                    g_overlay.isResizingSelection = false;
                    g_overlay.resizeHandle = OVERLAY_RESIZE_NONE;
                    ReleaseCapture();
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                if (g_overlay.isMovingSelection) {
                    g_overlay.isMovingSelection = false;
                    ReleaseCapture();
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
            if (g_overlay.isEditingText) {
                if (wParam == VK_LEFT) {
                    int selectionStart;
                    int selectionEnd;
                    NormalizeTextSelection(&g_overlay, &selectionStart, &selectionEnd);
                    if (selectionStart != selectionEnd) {
                        SetEditingCaret(&g_overlay, selectionStart, false);
                        InvalidateRect(hwnd, NULL, FALSE);
                    } else if (g_overlay.editingCaretIndex > 0) {
                        SetEditingCaret(&g_overlay, g_overlay.editingCaretIndex - 1, false);
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                    return 0;
                }
                if (wParam == VK_RIGHT) {
                    int selectionStart;
                    int selectionEnd;
                    NormalizeTextSelection(&g_overlay, &selectionStart, &selectionEnd);
                    if (selectionStart != selectionEnd) {
                        SetEditingCaret(&g_overlay, selectionEnd, false);
                        InvalidateRect(hwnd, NULL, FALSE);
                    } else if (g_overlay.editingCaretIndex < g_overlay.editingTextLen) {
                        SetEditingCaret(&g_overlay, g_overlay.editingCaretIndex + 1, false);
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                    return 0;
                }
                if (wParam == VK_HOME) {
                    SetEditingCaret(&g_overlay, 0, false);
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                if (wParam == VK_END) {
                    SetEditingCaret(&g_overlay, g_overlay.editingTextLen, false);
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                if (wParam == VK_DELETE) {
                    DeleteTextAtCaret(&g_overlay);
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
            }
            break;
        }

        case WM_TIMER:
            if (wParam == TEXT_CARET_TIMER_ID && g_overlay.isEditingText) {
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            break;

        case WM_IME_COMPOSITION:
            if (HandleImeComposition(hwnd, lParam)) {
                return 0;
            }
            break;

        case WM_CHAR: {
            if (g_overlay.isEditingText) {
                if (wParam == VK_BACK) {
                    DeleteTextBeforeCaret(&g_overlay);
                } else if (wParam == VK_RETURN || wParam == VK_ESCAPE) {
                    return 0;
                } else {
                    WCHAR ch = (WCHAR)wParam;
                    AppendEditingText(&g_overlay, &ch, 1);
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
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
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
