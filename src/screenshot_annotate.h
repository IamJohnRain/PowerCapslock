#ifndef SCREENSHOT_ANNOTATE_H
#define SCREENSHOT_ANNOTATE_H

#include "screenshot.h"
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ANNOTATE_TOOL_RECT = 0,
    ANNOTATE_TOOL_ARROW,
    ANNOTATE_TOOL_PENCIL,
    ANNOTATE_TOOL_CIRCLE,
    ANNOTATE_TOOL_TEXT,
    ANNOTATE_TOOL_COUNT
} AnnotateToolType;

typedef struct {
    COLORREF color;
    int lineWidth;
    BOOL filled;
} AnnotateStyle;

typedef struct {
    AnnotateToolType tool;
    AnnotateStyle style;
    POINT startPoint;
    POINT endPoint;
    BOOL isDrawing;
    HDC memDC;
    HBITMAP memBitmap;
    int width;
    int height;
} AnnotateContext;

BOOL AnnotateInit(AnnotateContext* ctx, const ScreenshotImage* image);
void AnnotateCleanup(AnnotateContext* ctx);
void AnnotateSetTool(AnnotateContext* ctx, AnnotateToolType tool);
void AnnotateSetStyle(AnnotateContext* ctx, const AnnotateStyle* style);
void AnnotateStartDraw(AnnotateContext* ctx, int x, int y);
void AnnotateUpdateDraw(AnnotateContext* ctx, int x, int y);
void AnnotateEndDraw(AnnotateContext* ctx, int x, int y);
void AnnotateDrawArrow(HDC hdc, int fromX, int fromY, int toX, int toY,
                       COLORREF color, int width);
BOOL AnnotateGetImage(AnnotateContext* ctx, ScreenshotImage* outImage);
int AnnotateTest(void);

#ifdef __cplusplus
}
#endif

#endif
