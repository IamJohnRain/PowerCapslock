#ifndef SCREENSHOT_OVERLAY_H
#define SCREENSHOT_OVERLAY_H

#include <windows.h>
#include <stdbool.h>
#include "screenshot.h"

// 选区状态
typedef enum {
    OVERLAY_STATE_IDLE = 0,       // 空闲
    OVERLAY_STATE_SELECTING = 1,  // 正在选区
    OVERLAY_STATE_SELECTED = 2,   // 已选区
    OVERLAY_STATE_TOOLBAR = 3     // 显示工具栏
} OverlayState;

typedef enum {
    OVERLAY_ANNOTATE_NONE = 0,
    OVERLAY_ANNOTATE_RECT,
    OVERLAY_ANNOTATE_ARROW,
    OVERLAY_ANNOTATE_PENCIL,
    OVERLAY_ANNOTATE_CIRCLE,
    OVERLAY_ANNOTATE_TEXT
} OverlayAnnotateTool;

typedef enum {
    OVERLAY_RESIZE_NONE = 0,
    OVERLAY_RESIZE_LEFT,
    OVERLAY_RESIZE_RIGHT,
    OVERLAY_RESIZE_TOP,
    OVERLAY_RESIZE_BOTTOM,
    OVERLAY_RESIZE_TOP_LEFT,
    OVERLAY_RESIZE_TOP_RIGHT,
    OVERLAY_RESIZE_BOTTOM_LEFT,
    OVERLAY_RESIZE_BOTTOM_RIGHT
} OverlayResizeHandle;

#define OVERLAY_MAX_ANNOTATIONS 128
#define OVERLAY_MAX_PENCIL_POINTS 256
#define OVERLAY_TEXT_MAX 64

typedef struct {
    OverlayAnnotateTool tool;
    COLORREF color;
    int lineWidth;
    int textFontHeight;
    POINT startPoint;
    POINT endPoint;
    POINT points[OVERLAY_MAX_PENCIL_POINTS];
    int pointCount;
    WCHAR text[OVERLAY_TEXT_MAX];
} OverlayAnnotation;

// 选区窗口状态
typedef struct {
    HWND hwnd;                    // 覆盖层窗口句柄
    bool isActive;                // 是否激活
    bool isSelecting;             // 是否正在选区
    POINT startPoint;             // 选区起点
    POINT currentPoint;           // 当前鼠标位置
    ScreenshotRect selection;     // 当前选区
    ScreenshotImage* screenImage; // 屏幕截图缓存
    HBITMAP screenBitmap;         // 屏幕位图
    HDC screenDC;                 // 屏幕内存 DC
    HCURSOR crossCursor;          // 十字光标
    OverlayState state;           // 当前状态
    OverlayAnnotateTool annotateTool;
    OverlayAnnotation annotations[OVERLAY_MAX_ANNOTATIONS];
    int annotationCount;
    bool isDrawingAnnotation;
    bool isEditingText;
    bool isResizingSelection;
    bool isSelectingText;
    COLORREF currentAnnotationColor;
    int currentLineWidth;
    int currentTextFontHeight;
    OverlayAnnotation currentAnnotation;
    OverlayResizeHandle resizeHandle;
    OverlayResizeHandle hoverResizeHandle;
    ScreenshotRect resizeStartSelection;
    POINT resizeStartPoint;
    POINT textAnchor;
    WCHAR editingText[OVERLAY_TEXT_MAX];
    int editingTextLen;
    int editingAnnotationIndex;
    int editingCaretIndex;
    COLORREF editingTextColor;
    int editingTextFontHeight;
    int textSelectionAnchor;
    int textSelectionStart;
    int textSelectionEnd;

    // 窗口识别
    HWND hoveredWindow;           // 悬停的窗口
    RECT hoveredRect;             // 悬停窗口的边界
} OverlayContext;

// 初始化选区窗口模块
bool ScreenshotOverlayInit(void);

// 清理选区窗口模块
void ScreenshotOverlayCleanup(void);

// 显示选区窗口（开始截图流程）
bool ScreenshotOverlayShow(void);

// 隐藏选区窗口
void ScreenshotOverlayHide(void);

// 检查选区窗口是否激活
bool ScreenshotOverlayIsActive(void);
HWND ScreenshotOverlayGetWindow(void);

// 获取当前选区
const ScreenshotRect* ScreenshotOverlayGetSelection(void);

// 获取选区截图
ScreenshotImage* ScreenshotOverlayGetSelectionImage(void);
ScreenshotImage* ScreenshotOverlayGetAnnotatedSelectionImage(void);
void ScreenshotOverlaySetAnnotationTool(OverlayAnnotateTool tool);
OverlayAnnotateTool ScreenshotOverlayGetAnnotationTool(void);
void ScreenshotOverlaySetAnnotationColor(COLORREF color);
COLORREF ScreenshotOverlayGetAnnotationColor(void);
void ScreenshotOverlaySetTextFontHeight(int fontHeight);
int ScreenshotOverlayGetTextFontHeight(void);
void ScreenshotOverlayClearAnnotations(void);
bool ScreenshotOverlayHasAnnotations(void);

// 测试函数（用于命令行测试）
int ScreenshotOverlayTest(void);

#endif // SCREENSHOT_OVERLAY_H
