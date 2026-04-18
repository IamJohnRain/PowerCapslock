/**
 * screenshot_ocr_tesseract.c
 *
 * Tesseract OCR backend for PowerCapslock.
 *
 * Tesseract is a mature open-source OCR engine that provides:
 * - Word-level bounding boxes
 * - Confidence scores
 * - Multi-language support
 * - Active community and development
 *
 * To enable this backend:
 * 1. Download Tesseract 5.x Windows build from:
 *    https://github.com/UB-Mannheim/tesseract/wiki
 * 2. Install it (e.g., to C:\Program Files\Tesseract-OCR)
 * 3. Download tessdata (language trained data) if not included
 * 4. Set TESSDATA_PATH in environment or config
 * 5. Rebuild with -DUSE_TESSERACT compile flag
 *
 * The mock OCR backend remains the default, providing consistent
 * word-level data for UI development and testing.
 */

#include "screenshot_ocr.h"
#include "logger.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>

#ifdef USE_TESSERACT

/* Tesseract API - requires tesseract and leptonica headers */
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>

#define MODULE_NAME "OCR"

static BOOL g_ocrInitialized = FALSE;
static TessBaseAPI* g_tessApi = NULL;
static char* g_tessdataPath = NULL;

/**
 * Convert ScreenshotImage to Leptonica PIX format
 * Tesseract uses Leptonica's PIX format for image data
 */
static PIX* CreatePixFromScreenshotImage(const ScreenshotImage* image) {
    if (image == NULL || image->pixels == NULL) {
        LOG_ERROR("[%s] 无效的图像数据", MODULE_NAME);
        return NULL;
    }

    /* 创建 32-bit RGBA PIX */
    PIX* pix = pixCreate(image->width, image->height, 32);
    if (pix == NULL) {
        LOG_ERROR("[%s] 创建 PIX 对象失败", MODULE_NAME);
        return NULL;
    }

    /* 复制像素数据 */
    int y;
    for (y = 0; y < image->height; y++) {
        DWORD* line = (DWORD*)pixGetData(pix) + y * pixGetWpl(pix);
        memcpy(line, image->pixels + y * image->width * 4, image->width * 4);
    }

    LOG_DEBUG("[%s] 创建 PIX: %dx%d", MODULE_NAME, image->width, image->height);
    return pix;
}

/**
 * Create OCRResults from Tesseract recognition
 */
static OCRResults* CreateOCRResultsFromTesseract(TessBaseAPI* api, int imgWidth, int imgHeight) {
    if (api == NULL) {
        return NULL;
    }

    OCRResults* results = (OCRResults*)malloc(sizeof(OCRResults));
    if (results == NULL) {
        return NULL;
    }
    memset(results, 0, sizeof(OCRResults));
    results->imageWidth = imgWidth;
    results->imageHeight = imgHeight;

    /* 获取完整文本 */
    char* fullText = TessBaseAPIGetUTF8Text(api);
    if (fullText != NULL) {
        results->fullText = strdup(fullText);
        TessDeleteText(fullText);
    }

    /* 遍历单词提取边界框 */
    int maxWords = 512;
    int wordCount = 0;
    results->words = (OCRWord*)malloc(maxWords * sizeof(OCRWord));
    if (results->words == NULL) {
        OCRFreeResults(results);
        return NULL;
    }

    TessResultIterator* ri = TessBaseAPIGetIterator(api);
    if (ri != NULL) {
        do {
            const char* word = TessResultIteratorGetUTF8Text(ri, RIL_WORD);
            if (word != NULL && strlen(word) > 0) {
                int x1, y1, x2, y2;
                if (TessResultIteratorBoundingBox(ri, RIL_WORD, &x1, &y1, &x2, &y2)) {
                    if (wordCount < maxWords) {
                        results->words[wordCount].text = strdup(word);
                        results->words[wordCount].boundingBox.left = x1;
                        results->words[wordCount].boundingBox.top = y1;
                        results->words[wordCount].boundingBox.right = x2;
                        results->words[wordCount].boundingBox.bottom = y2;
                        results->words[wordCount].confidence = (float)TessResultIteratorConfidence(ri, RIL_WORD);
                        results->words[wordCount].isLineBreak = FALSE;
                        wordCount++;
                    }
                }
            }
            TessDeleteText(word);
        } while (TessResultIteratorNext(ri, RIL_WORD));

        TessResultIteratorDelete(ri);
    }

    results->wordCount = wordCount;
    return results;
}

/**
 * Initialize Tesseract OCR backend
 */
BOOL OCRInitTesseract(const char* tessdataPath) {
    if (g_ocrInitialized) {
        LOG_DEBUG("[%s] Tesseract 已初始化", MODULE_NAME);
        return TRUE;
    }

    if (tessdataPath == NULL) {
        /* 尝试从环境变量获取 */
        const char* envPath = getenv("TESSDATA_PREFIX");
        if (envPath != NULL) {
            tessdataPath = envPath;
        }
    }

    LOG_DEBUG("[%s] 初始化 Tesseract, tessdata: %s",
              MODULE_NAME, tessdataPath ? tessdataPath : "(default)");

    g_tessApi = TessBaseAPICreate();
    if (g_tessApi == NULL) {
        LOG_ERROR("[%s] 创建 Tesseract API 失败", MODULE_NAME);
        return FALSE;
    }

    /* 初始化 Tesseract - 使用英语 */
    if (TessBaseAPIInit(g_tessApi, tessdataPath, "eng", OEM_LSTM_ONLY, NULL, 0) != 0) {
        LOG_ERROR("[%s] Tesseract 初始化失败", MODULE_NAME);
        TessBaseAPIDelete(g_tessApi);
        g_tessApi = NULL;
        return FALSE;
    }

    /* 设置识别模式 */
    TessBaseAPISetPageSegMode(g_tessApi, PSM_AUTO);

    g_tessdataPath = (tessdataPath != NULL) ? strdup(tessdataPath) : NULL;
    g_ocrInitialized = TRUE;

    LOG_INFO("[%s] Tesseract OCR 初始化成功", MODULE_NAME);
    return TRUE;
}

/**
 * Cleanup Tesseract OCR backend
 */
void OCRCleanupTesseract(void) {
    if (!g_ocrInitialized) {
        return;
    }

    LOG_DEBUG("[%s] 清理 Tesseract...", MODULE_NAME);

    if (g_tessApi != NULL) {
        TessBaseAPIDelete(g_tessApi);
        g_tessApi = NULL;
    }

    if (g_tessdataPath != NULL) {
        free(g_tessdataPath);
        g_tessdataPath = NULL;
    }

    g_ocrInitialized = FALSE;
}

/**
 * Recognize text using Tesseract
 */
OCRResults* OCRRecognizeTesseract(const ScreenshotImage* image) {
    if (!g_ocrInitialized || image == NULL || g_tessApi == NULL) {
        LOG_ERROR("[%s] Tesseract 识别失败: 未初始化", MODULE_NAME);
        return NULL;
    }

    LOG_DEBUG("[%s] Tesseract 识别: %dx%d", MODULE_NAME, image->width, image->height);

    /* 转换为 PIX 格式 */
    PIX* pix = CreatePixFromScreenshotImage(image);
    if (pix == NULL) {
        return NULL;
    }

    /* 设置图像 */
    TessBaseAPISetImage(g_tessApi, pix);

    /* 执行识别 */
    if (TessBaseAPIRecognize(g_tessApi, NULL) != 0) {
        LOG_ERROR("[%s] Tesseract 识别执行失败", MODULE_NAME);
        pixDestroy(&pix);
        return NULL;
    }

    /* 创建结果 */
    OCRResults* results = CreateOCRResultsFromTesseract(g_tessApi, image->width, image->height);

    pixDestroy(&pix);

    if (results != NULL) {
        LOG_INFO("[%s] Tesseract 识别完成: %d 个单词",
                 MODULE_NAME, results->wordCount);
    }

    return results;
}

#else /* !USE_TESSERACT */

#define STUB_MODULE_NAME "OCR"

BOOL OCRInitTesseract(const char* tessdataPath) {
    LOG_DEBUG("[%s] Tesseract 未编译启用", STUB_MODULE_NAME);
    return FALSE;
}

void OCRCleanupTesseract(void) {
    /* nothing to do */
}

OCRResults* OCRRecognizeTesseract(const ScreenshotImage* image) {
    LOG_ERROR("[%s] Tesseract 未启用", STUB_MODULE_NAME);
    return NULL;
}

#endif /* USE_TESSERACT */
