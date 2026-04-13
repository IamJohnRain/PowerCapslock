#include "screenshot_annotate.h"
#include "logger.h"
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

#define MODULE_NAME "标注"

void AnnotateSetTool(AnnotateContext* ctx, AnnotateToolType tool) {
    if (ctx != NULL) {
        ctx->tool = tool;
        LOG_DEBUG("[%s] 设置工具: %d", MODULE_NAME, tool);
    }
}

void AnnotateSetStyle(AnnotateContext* ctx, const AnnotateStyle* style) {
    if (ctx != NULL && style != NULL) {
        ctx->style = *style;
        LOG_DEBUG("[%s] 设置样式: color=0x%06X, lineWidth=%d, filled=%d",
                  MODULE_NAME, (unsigned int)style->color, style->lineWidth, style->filled);
    }
}

BOOL AnnotateInit(AnnotateContext* ctx, const ScreenshotImage* image) {
    HDC screenDC;
    HBITMAP hBitmap;
    BITMAPINFO bmi;
    void* bits;

    if (ctx == NULL || image == NULL || image->pixels == NULL) {
        LOG_ERROR("[%s] 初始化失败: 无效参数", MODULE_NAME);
        return FALSE;
    }

    LOG_DEBUG("[%s] 开始初始化...", MODULE_NAME);

    ZeroMemory(ctx, sizeof(AnnotateContext));
    ctx->width = image->width;
    ctx->height = image->height;
    ctx->tool = ANNOTATE_TOOL_RECT;
    ctx->style.color = RGB(255, 0, 0);
    ctx->style.lineWidth = 3;
    ctx->style.filled = FALSE;

    screenDC = GetDC(NULL);
    if (screenDC == NULL) {
        LOG_ERROR("[%s] 获取屏幕DC失败", MODULE_NAME);
        return FALSE;
    }

    ctx->memDC = CreateCompatibleDC(screenDC);
    if (ctx->memDC == NULL) {
        LOG_ERROR("[%s] 创建内存DC失败", MODULE_NAME);
        ReleaseDC(NULL, screenDC);
        return FALSE;
    }

    ZeroMemory(&bmi, sizeof(BITMAPINFO));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = image->width;
    bmi.bmiHeader.biHeight = -image->height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    hBitmap = CreateDIBSection(ctx->memDC, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (hBitmap == NULL) {
        LOG_ERROR("[%s] 创建DIB段失败", MODULE_NAME);
        DeleteDC(ctx->memDC);
        ReleaseDC(NULL, screenDC);
        return FALSE;
    }

    ctx->memBitmap = hBitmap;
    SelectObject(ctx->memDC, hBitmap);

    if (image->pixels != NULL) {
        memcpy(bits, image->pixels, (size_t)image->width * (size_t)image->height * 4);
    }

    ReleaseDC(NULL, screenDC);

    LOG_INFO("[%s] 模块初始化成功", MODULE_NAME);
    return TRUE;
}

void AnnotateCleanup(AnnotateContext* ctx) {
    if (ctx == NULL) {
        return;
    }

    LOG_DEBUG("[%s] 开始清理...", MODULE_NAME);

    if (ctx->memDC != NULL) {
        DeleteDC(ctx->memDC);
        ctx->memDC = NULL;
    }
    if (ctx->memBitmap != NULL) {
        DeleteObject(ctx->memBitmap);
        ctx->memBitmap = NULL;
    }

    LOG_INFO("[%s] 模块已清理", MODULE_NAME);
}

void AnnotateDrawArrow(HDC hdc, int fromX, int fromY, int toX, int toY,
                       COLORREF color, int width) {
    HPEN hPen, hOldPen;
    double angle;
    double arrowLen = 15.0;
    POINT arrowPoints[3];
    int dx, dy;

    hPen = CreatePen(PS_SOLID, width, color);
    hOldPen = (HPEN)SelectObject(hdc, hPen);

    MoveToEx(hdc, fromX, fromY, NULL);
    LineTo(hdc, toX, toY);

    dx = toX - fromX;
    dy = toY - fromY;
    angle = atan2((double)dy, (double)dx);

    arrowPoints[0].x = toX;
    arrowPoints[0].y = toY;
    arrowPoints[1].x = toX - (int)(arrowLen * cos(angle - 0.5));
    arrowPoints[1].y = toY - (int)(arrowLen * sin(angle - 0.5));
    arrowPoints[2].x = toX - (int)(arrowLen * cos(angle + 0.5));
    arrowPoints[2].y = toY - (int)(arrowLen * sin(angle + 0.5));

    Polygon(hdc, arrowPoints, 3);

    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
}

void AnnotateStartDraw(AnnotateContext* ctx, int x, int y) {
    if (ctx == NULL) {
        return;
    }
    ctx->startPoint.x = x;
    ctx->startPoint.y = y;
    ctx->endPoint.x = x;
    ctx->endPoint.y = y;
    ctx->isDrawing = TRUE;
    LOG_DEBUG("[%s] 开始绘制: (%d, %d)", MODULE_NAME, x, y);
}

void AnnotateUpdateDraw(AnnotateContext* ctx, int x, int y) {
    if (ctx == NULL || !ctx->isDrawing) {
        return;
    }
    ctx->endPoint.x = x;
    ctx->endPoint.y = y;
}

void AnnotateEndDraw(AnnotateContext* ctx, int x, int y) {
    HPEN hPen, hOldPen;
    HBRUSH hBrush, hOldBrush;
    int x1, y1, x2, y2;

    if (ctx == NULL || !ctx->isDrawing) {
        return;
    }

    ctx->endPoint.x = x;
    ctx->endPoint.y = y;
    ctx->isDrawing = FALSE;

    x1 = ctx->startPoint.x;
    y1 = ctx->startPoint.y;
    x2 = ctx->endPoint.x;
    y2 = ctx->endPoint.y;

    hPen = CreatePen(PS_SOLID, ctx->style.lineWidth, ctx->style.color);
    hOldPen = (HPEN)SelectObject(ctx->memDC, hPen);

    if (ctx->style.filled) {
        hBrush = CreateSolidBrush(ctx->style.color);
    } else {
        hBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
    }
    hOldBrush = (HBRUSH)SelectObject(ctx->memDC, hBrush);

    switch (ctx->tool) {
        case ANNOTATE_TOOL_RECT:
            Rectangle(ctx->memDC, x1, y1, x2, y2);
            break;
        case ANNOTATE_TOOL_CIRCLE:
            Ellipse(ctx->memDC, x1, y1, x2, y2);
            break;
        case ANNOTATE_TOOL_ARROW:
            AnnotateDrawArrow(ctx->memDC, x1, y1, x2, y2, ctx->style.color, ctx->style.lineWidth);
            break;
        case ANNOTATE_TOOL_PENCIL:
        case ANNOTATE_TOOL_TEXT:
        default:
            MoveToEx(ctx->memDC, x1, y1, NULL);
            LineTo(ctx->memDC, x2, y2);
            break;
    }

    SelectObject(ctx->memDC, hOldPen);
    SelectObject(ctx->memDC, hOldBrush);
    DeleteObject(hPen);
    if (ctx->style.filled) {
        DeleteObject(hBrush);
    }

    LOG_DEBUG("[%s] 完成绘制: 工具=%d, (%d, %d) -> (%d, %d)", MODULE_NAME, ctx->tool, x1, y1, x2, y2);
}

BOOL AnnotateGetImage(AnnotateContext* ctx, ScreenshotImage* outImage) {
    BITMAPINFO bmi;
    HDC hdc;
    HBITMAP hOldBitmap;

    if (ctx == NULL || outImage == NULL || ctx->memDC == NULL) {
        LOG_ERROR("[%s] 获取图像失败: 无效参数", MODULE_NAME);
        return FALSE;
    }

    ZeroMemory(outImage, sizeof(ScreenshotImage));
    outImage->width = ctx->width;
    outImage->height = ctx->height;
    outImage->stride = ctx->width * 4;
    outImage->pixels = (BYTE*)malloc((size_t)outImage->stride * (size_t)outImage->height);
    if (outImage->pixels == NULL) {
        LOG_ERROR("[%s] 分配图像内存失败", MODULE_NAME);
        return FALSE;
    }
    outImage->ownsData = true;

    ZeroMemory(&bmi, sizeof(BITMAPINFO));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = ctx->width;
    bmi.bmiHeader.biHeight = -ctx->height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    hdc = GetDC(NULL);
    hOldBitmap = (HBITMAP)SelectObject(ctx->memDC, ctx->memBitmap);
    GetDIBits(hdc, ctx->memBitmap, 0, ctx->height, outImage->pixels, &bmi, DIB_RGB_COLORS);
    SelectObject(ctx->memDC, hOldBitmap);
    ReleaseDC(NULL, hdc);

    outImage->hBitmap = (HBITMAP)CopyImage(ctx->memBitmap, IMAGE_BITMAP, 0, 0, LR_COPYRETURNORG);

    LOG_DEBUG("[%s] 获取标注图像: %dx%d", MODULE_NAME, outImage->width, outImage->height);
    return TRUE;
}

int AnnotateTest(void) {
    AnnotateContext ctx;
    ScreenshotImage* captureImg;
    ScreenshotImage outImg;
    int result = 1;

    LOG_INFO("[%s测试] 开始测试...", MODULE_NAME);

    printf("========== PowerCapslock Screenshot Annotate Test ==========\n");
    printf("Initializing annotation module...\n");
    fflush(stdout);

    if (!ScreenshotInit()) {
        printf("ERROR: Failed to initialize screenshot module\n");
        return 1;
    }

    captureImg = ScreenshotCaptureRect(0, 0, 400, 300);
    if (captureImg == NULL) {
        printf("ERROR: Failed to capture screenshot\n");
        ScreenshotCleanup();
        return 1;
    }

    if (!AnnotateInit(&ctx, captureImg)) {
        printf("ERROR: Failed to initialize annotation context\n");
        ScreenshotImageFree(captureImg);
        ScreenshotCleanup();
        return 1;
    }

    printf("Drawing rectangle...\n");
    AnnotateStartDraw(&ctx, 50, 50);
    AnnotateEndDraw(&ctx, 150, 150);

    printf("Drawing arrow...\n");
    AnnotateSetTool(&ctx, ANNOTATE_TOOL_ARROW);
    AnnotateStartDraw(&ctx, 200, 100);
    AnnotateEndDraw(&ctx, 300, 200);

    printf("Drawing circle...\n");
    AnnotateSetTool(&ctx, ANNOTATE_TOOL_CIRCLE);
    AnnotateStartDraw(&ctx, 250, 50);
    AnnotateEndDraw(&ctx, 350, 150);

    if (AnnotateGetImage(&ctx, &outImg)) {
        printf("Saving annotated image...\n");
        if (ScreenshotSaveToFile(&outImg, "test_annotate.bmp", SCREENSHOT_FORMAT_BMP, 100)) {
            printf("Annotated image saved: test_annotate.bmp\n");
            result = 0;
        }
        if (outImg.ownsData && outImg.pixels != NULL) {
            free(outImg.pixels);
        }
        if (outImg.hBitmap != NULL) {
            DeleteObject(outImg.hBitmap);
        }
    }

    AnnotateCleanup(&ctx);
    ScreenshotImageFree(captureImg);
    ScreenshotCleanup();

    printf("Annotate test %s\n", result == 0 ? "PASSED" : "FAILED");
    LOG_INFO("[%s测试] 测试完成", MODULE_NAME);
    return result;
}
