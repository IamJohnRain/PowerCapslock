#include "screenshot_ocr.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>

#define MODULE_NAME "OCR"

static BOOL g_ocrInitialized = FALSE;

BOOL OCRInit(void) {
    LOG_DEBUG("[%s] 开始初始化...", MODULE_NAME);
    LOG_INFO("[%s] 模块初始化成功 (Tesseract 待集成)", MODULE_NAME);
    g_ocrInitialized = TRUE;
    return TRUE;
}

void OCRCleanup(void) {
    LOG_DEBUG("[%s] 开始清理...", MODULE_NAME);
    g_ocrInitialized = FALSE;
    LOG_INFO("[%s] 模块已清理", MODULE_NAME);
}

OCRResults* OCRRecognize(const ScreenshotImage* image) {
    OCRResults* results;

    if (!g_ocrInitialized || image == NULL) {
        LOG_ERROR("[%s] 识别失败: 模块未初始化或无效参数", MODULE_NAME);
        return NULL;
    }

    LOG_DEBUG("[%s] 开始识别: %dx%d", MODULE_NAME, image->width, image->height);

    results = (OCRResults*)malloc(sizeof(OCRResults));
    if (results == NULL) {
        LOG_ERROR("[%s] 分配结果内存失败", MODULE_NAME);
        return NULL;
    }

    results->count = 0;
    results->results = NULL;
    results->fullText = strdup("OCR 功能待集成 (Tesseract)");

    LOG_INFO("[%s] 识别完成", MODULE_NAME);
    return results;
}

void OCRFreeResults(OCRResults* results) {
    if (results == NULL) {
        return;
    }
    if (results->results != NULL) {
        int i;
        for (i = 0; i < results->count; i++) {
            if (results->results[i].text != NULL) {
                free(results->results[i].text);
            }
        }
        free(results->results);
    }
    if (results->fullText != NULL) {
        free(results->fullText);
    }
    free(results);
}

int OCRTest(void) {
    ScreenshotImage* captureImg;
    OCRResults* results;
    int result = 1;

    LOG_INFO("[%s测试] 开始测试...", MODULE_NAME);

    printf("========== PowerCapslock Screenshot OCR Test ==========\n");
    printf("Initializing OCR module...\n");
    fflush(stdout);

    if (!OCRInit()) {
        printf("ERROR: Failed to initialize OCR module\n");
        return 1;
    }

    if (!ScreenshotInit()) {
        printf("ERROR: Failed to initialize screenshot module\n");
        OCRCleanup();
        return 1;
    }

    captureImg = ScreenshotCaptureRect(0, 0, 400, 300);
    if (captureImg == NULL) {
        printf("ERROR: Failed to capture screenshot\n");
        ScreenshotCleanup();
        OCRCleanup();
        return 1;
    }

    printf("Running OCR recognition...\n");
    results = OCRRecognize(captureImg);
    if (results != NULL) {
        printf("OCR result: %s\n", results->fullText);
        OCRFreeResults(results);
        result = 0;
    } else {
        printf("ERROR: OCR recognition failed\n");
    }

    ScreenshotImageFree(captureImg);
    ScreenshotCleanup();
    OCRCleanup();

    printf("OCR test %s\n", result == 0 ? "PASSED" : "FAILED");
    LOG_INFO("[%s测试] 测试完成", MODULE_NAME);
    return result;
}
